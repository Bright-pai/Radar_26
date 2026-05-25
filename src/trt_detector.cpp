// trt_detector.cpp —— TensorRT YOLO 目标检测模块完整实现。
// 本文件负责：
//   1. 从 .engine 文件反序列化 TensorRT 推理引擎；
//   2. 将 OpenCV BGR 图像做 letterbox 缩放/填充，拆分为 R/G/B 三平面并上传到 GPU；
//   3. 执行 GPU 推理，下载输出张量；
//   4. 将模型输出解码为 Detection 结构体，执行 CPU 侧 NMS（非极大值抑制）；
//   5. 支持 float 和 half（FP16）两种精度的输入/输出；
//   6. 兼容 TensorRT 8.x（binding-based API）和 10.x（tensor-name-based API）。

#include "trt_detector.hpp"
#include <cuda_fp16.h>
#include <NvInferRuntime.h>
#include <cuda_runtime_api.h>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace radar26 {
namespace {

// ============================================================================
// TrtLogger —— TensorRT 日志回调，将错误和内部错误输出到 stderr。
// ============================================================================
class TrtLogger final : public nvinfer1::ILogger {
public:
    // 参数：
    //   severity - 日志严重级别（kINFO/kWARNING/kERROR/kINTERNAL_ERROR）
    //   msg      - TensorRT 内部生成的日志文本
    // 返回：无
    // 副作用：ERROR 和 INTERNAL_ERROR 级别会写入 stderr，其余级别静默忽略。
    void log(Severity severity, const char* msg) noexcept override {
        if (severity == Severity::kERROR || severity == Severity::kINTERNAL_ERROR)
            std::fprintf(stderr, "[TensorRT] %s\n", msg);
    }
};

// ============================================================================
// TrtDeleter —— TensorRT 对象 RAII 删除器。
// 根据 TensorRT 大版本号选择 delete 或 destroy() 方法来释放资源。
// TensorRT 10.x 使用标准 C++ delete，旧版使用 destroy() 成员函数。
// ============================================================================
template <typename T>
struct TrtDeleter {
    // 参数：
    //   obj - 要释放的 TensorRT 对象指针（IRuntime/ICudaEngine/IExecutionContext）
    // 返回：无
    // 副作用：销毁传入的 TensorRT 对象并释放其占用的 GPU/CPU 资源。
    void operator()(T* obj) const {
        if (obj) {
#if NV_TENSORRT_MAJOR >= 10
            delete obj;
#else
            obj->destroy();
#endif
        }
    }
};

// ============================================================================
// Volume —— 计算一个张量维度所包含的元素总数。
// 将各个维度的尺寸累乘，遇到任何维度 <= 0 返回 0（表示非法维度）。
// ============================================================================
std::size_t Volume(const nvinfer1::Dims& dims) {
    std::size_t v = 1;
    for (int i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] <= 0) return 0;
        v *= static_cast<std::size_t>(dims.d[i]);
    }
    return v;
}

// ============================================================================
// ElementSize —— 返回指定数据类型的每个元素占用的字节数。
// 参数：
//   dtype - TensorRT 数据类型枚举（kFLOAT/kHALF/kINT8/kINT32/kBOOL）
// 返回：每个元素的字节数，未知类型返回 0。
// ============================================================================
std::size_t ElementSize(nvinfer1::DataType dtype) noexcept {
    switch (dtype) {
    case nvinfer1::DataType::kFLOAT:  return 4;
    case nvinfer1::DataType::kHALF:   return 2;
    case nvinfer1::DataType::kINT8:   return 1;
    case nvinfer1::DataType::kINT32:  return 4;
#if NV_TENSORRT_MAJOR >= 8
    case nvinfer1::DataType::kBOOL:   return 1;
#endif
    default: return 0;
    }
}

// ============================================================================
// HasDynamicShape —— 检查张量是否包含动态维度（值为 -1 的维度）。
// 参数：
//   dims - 待检查的张量维度描述
// 返回：如果有任一维度 < 0（即动态），返回 true；否则 false。
// ============================================================================
bool HasDynamicShape(const nvinfer1::Dims& dims) {
    for (int i = 0; i < dims.nbDims; ++i)
        if (dims.d[i] < 0) return true;
    return false;
}

// 动态 batch 时的缓冲区扩容因子。
// kDynamicInitialReserveFactor：首次分配时按实际需求的 8 倍申请，减少后续 realloc 次数。
// kDynamicGrowFactor：当缓冲区不足时按 2 倍扩容（当前未使用此常量，预留扩展）。
constexpr std::size_t kDynamicInitialReserveFactor = 8U;
constexpr std::size_t kDynamicGrowFactor          = 2U;

// ============================================================================
// ExpandElementsForReserve —— 将缓冲区大小按给定因子放大，用于动态 batch 场景。
// 参数：
//   required - 当前实际需要的元素数
//   factor   - 放大因子（>= 1）
// 返回：放大后的元素数。如果乘法会溢出则返回原值。
// ============================================================================
std::size_t ExpandElementsForReserve(std::size_t required, std::size_t factor) {
    if (required == 0U || factor <= 1U) return required;
    if (required > std::numeric_limits<std::size_t>::max() / factor) return required;
    return required * factor;
}

// ============================================================================
// LetterboxMeta —— 记录 letterbox 预处理阶段的几何参数。
//   ratio   - 缩放比例（目标宽/原图宽 与 目标高/原图高 取较小值，保持宽高比）
//   padLeft - 左侧填充的像素宽度
//   padTop  - 顶部填充的像素高度
// 这些参数会在后处理阶段用于将检测框坐标从 letterbox 空间还原到原图空间。
// ============================================================================
struct LetterboxMeta {
    float ratio   = 1.0F;
    float padLeft = 0.0F;
    float padTop  = 0.0F;
};

// ============================================================================
// Candidate —— CPU NMS（非极大值抑制）排序阶段使用的中间候选框结构。
//   classId - 类别索引
//   score   - 置信度分数（obj * class_prob）
//   box     - 在原图坐标系中的矩形框
// ============================================================================
struct Candidate {
    int         classId = -1;
    float       score   = 0.0F;
    cv::Rect2f  box;
};

// ============================================================================
// PreprocessToFloatPlanes —— YOLO 图像预处理：缩放 + 填充 + 通道拆分。
//
// 处理流程：
//   1. 计算缩放比例，使图像不超出目标尺寸且保持宽高比；
//   2. 用 resize 将图像缩放到 unpadded 尺寸；
//   3. 在目标画布上用灰色 (114,114,114) 居中填充剩余区域（letterbox）；
//   4. 将 BGR uint8 图像转为 float [0,1]；
//   5. 拆分 B/G/R 三通道，按 R、G、B 顺序写入 CHW 格式的 float 数组。
//
// 参数：
//   bgr            - 输入的 OpenCV BGR 图像（CV_8UC3）
//   dstW, dstH     - 模型输入尺寸（宽 x 高，如 640x640）
//   resizedScratch - 临时 Mat，用于保存 resize 后的图像（会被覆盖）
//   paddedScratch  - 临时 Mat，用于保存 letterbox 填充后的图像（会被覆盖）
//   chw            - 输出 float 数组指针，长度为 dstW*dstH*3，按 R/G/B 平面排列
//   meta           - [输出] 记录的缩放比和填充量，供后处理还原坐标使用
//   error          - [输出] 失败时写入错误描述字符串，可为 nullptr
//
// 返回：成功返回 true，失败返回 false 并设置 error。
// 副作用：修改 resizedScratch 和 paddedScratch 的内容；
//         将预处理后的 float 数据写入 chw 指向的内存。
// ============================================================================
bool PreprocessToFloatPlanes(const cv::Mat& bgr, int dstW, int dstH,
                             cv::Mat& resizedScratch, cv::Mat& paddedScratch,
                             float* chw, LetterboxMeta& meta, std::string* error) {
    // 输入校验：图像不能为空
    if (bgr.empty()) {
        if (error) *error = "input image is empty";
        return false;
    }

    // 计算缩放比例 r：取宽高方向中较小的缩放比，确保整张图都能放进目标尺寸
    const float r = std::min(static_cast<float>(dstW) / bgr.cols,
                             static_cast<float>(dstH) / bgr.rows);
    const int unpadW = static_cast<int>(std::round(bgr.cols * r));
    const int unpadH = static_cast<int>(std::round(bgr.rows * r));

    cv::resize(bgr, resizedScratch, cv::Size(unpadW, unpadH));

    // 计算左右、上下各需要填充的像素数（居中放置）
    const float dw = static_cast<float>(dstW - unpadW) * 0.5F;
    const float dh = static_cast<float>(dstH - unpadH) * 0.5F;
    const int left   = static_cast<int>(std::round(dw - 0.1F));
    const int top    = static_cast<int>(std::round(dh - 0.1F));

    // 边界校验：填充量不能为负，且填充位置 + 原始尺寸不能超出目标尺寸
    if (left < 0 || top < 0 || left + unpadW > dstW || top + unpadH > dstH) {
        if (error) *error = "invalid letterbox padding values";
        return false;
    }

    // 创建目标画布并用灰色 (114,114,114) 填充（YOLO 惯例填充色）
    paddedScratch.create(dstH, dstW, CV_8UC3);
    paddedScratch.setTo(cv::Scalar(114, 114, 114));
    resizedScratch.copyTo(paddedScratch(cv::Rect(left, top, unpadW, unpadH)));

    // 转为 float [0,1] 后拆分通道，再按 R/G/B 顺序写入输入缓冲。
    cv::Mat floatImg;
    paddedScratch.convertTo(floatImg, CV_32FC3, 1.0 / 255.0);

    std::array<cv::Mat, 3> channels;
    cv::split(floatImg, channels.data());  // OpenCV split 输出顺序：B, G, R

    const std::size_t planeSize = static_cast<std::size_t>(dstW) * dstH;
    // 按 R、G、B 平面顺序写入 CHW 缓冲区
    // channels[2] = R, channels[1] = G, channels[0] = B
    std::memcpy(chw,                 channels[2].data, planeSize * sizeof(float)); // R
    std::memcpy(chw + planeSize,     channels[1].data, planeSize * sizeof(float)); // G
    std::memcpy(chw + planeSize * 2, channels[0].data, planeSize * sizeof(float)); // B

    meta.ratio   = r;
    meta.padLeft = static_cast<float>(left);
    meta.padTop  = static_cast<float>(top);
    return true;
}

// ============================================================================
// IoU —— 计算两个矩形框的交并比（Intersection over Union）。
// 参数：
//   a, b - 两个矩形框（cv::Rect2f，x,y 为左上角，width,height 为宽高）
// 返回：IoU 值，范围 [0, 1]。并集面积为 0 时返回 0。
// ============================================================================
float IoU(const cv::Rect2f& a, const cv::Rect2f& b) {
    // 计算交集区域的左上角和右下角坐标
    const float xx1 = std::max(a.x, b.x);
    const float yy1 = std::max(a.y, b.y);
    const float xx2 = std::min(a.x + a.width,  b.x + b.width);
    const float yy2 = std::min(a.y + a.height, b.y + b.height);

    // 交集宽高（非负截断）
    const float w = std::max(0.0F, xx2 - xx1);
    const float h = std::max(0.0F, yy2 - yy1);
    const float inter = w * h;
    const float uni   = a.area() + b.area() - inter;
    return (uni > 0.0F) ? inter / uni : 0.0F;
}

// ============================================================================
// UndoLetterbox —— 将 letterbox 坐标系中的检测框还原到原始图像坐标系。
//
// 反向操作：
//   1. 减去填充偏移量（padLeft, padTop）；
//   2. 除以缩放比例（ratio）；
//   3. 裁剪到原始图像边界内。
//
// 参数：
//   x1, y1, x2, y2 - letterbox 坐标系下的框左上角和右下角坐标
//   meta            - PreprocessToFloatPlanes 阶段记录的缩放和填充参数
//   srcW, srcH      - 原始图像的宽和高
// 返回：原始图像坐标系下的矩形框（cv::Rect2f 格式：左上角 x,y + 宽高）
// ============================================================================
cv::Rect2f UndoLetterbox(float x1, float y1, float x2, float y2,
                         const LetterboxMeta& meta, int srcW, int srcH) {
    float rx1 = (x1 - meta.padLeft) / meta.ratio;
    float ry1 = (y1 - meta.padTop)  / meta.ratio;
    float rx2 = (x2 - meta.padLeft) / meta.ratio;
    float ry2 = (y2 - meta.padTop)  / meta.ratio;

    // 裁剪到原图边界内 [0, srcW-1] x [0, srcH-1]
    rx1 = std::clamp(rx1, 0.0F, static_cast<float>(srcW - 1));
    ry1 = std::clamp(ry1, 0.0F, static_cast<float>(srcH - 1));
    rx2 = std::clamp(rx2, 0.0F, static_cast<float>(srcW - 1));
    ry2 = std::clamp(ry2, 0.0F, static_cast<float>(srcH - 1));

    float w = std::max(0.0F, rx2 - rx1);
    float h = std::max(0.0F, ry2 - ry1);
    return {rx1, ry1, w, h};
}

// ============================================================================
// DecodeOutputRows —— 将模型输出张量解码为检测结果，并执行 CPU 侧 NMS。
//
// 支持两种输出格式：
//   格式 A（attrs == 6）：TensorRT 内置 NMS 输出，每行 [x1, y1, x2, y2, score, class]
//         无需额外 NMS，直接还原坐标后输出。
//   格式 B（attrs > 6）：常规 YOLO 输出，每行 [cx, cy, w, h, obj, class_0, ..., class_N]
//         先按 obj * class_prob 计算分数，过滤低置信度，再按类别执行 NMS。
//
// 模板参数 T：输出数据的数值类型（float 或 __half）
//
// 参数：
//   output     - 输出张量首指针（flat 一维数组）
//   rowStart   - 起始行偏移（通常为 0）
//   rowCount   - 输出行数（总元素数 / attrs）
//   attrs      - 每行的属性数（格式 A 为 6，格式 B 为 5 + 类别数）
//   confThresh - 置信度阈值，低于此值的检测框被丢弃
//   iouThresh  - NMS IoU 阈值，高于此值的重叠框被抑制
//   maxDet     - 最大检测数上限（0 表示不限制）
//   meta       - letterbox 参数，用于坐标还原
//   srcW, srcH - 原始图像的宽和高
//   classNames - 类别名称列表
//   removed    - NMS 被抑制标记数组（会被清空并重新填充）
//   cands      - 候选框临时缓冲区（会被清空并重新填充）
//   detections - [输出] 最终的检测结果列表
//
// 副作用：清空并填充 detections、cands、removed。
// ============================================================================
template <typename T>
void DecodeOutputRows(const T* output, std::size_t rowStart, std::size_t rowCount,
                      int attrs, float confThresh, float iouThresh, int maxDet,
                      const LetterboxMeta& meta, int srcW, int srcH,
                      const std::vector<std::string>& classNames,
                      std::vector<uint8_t>& removed, std::vector<Candidate>& cands,
                      std::vector<Detection>& detections) {
    detections.clear();

    // 类型转换 lambda：如果是 __half 则调用 __half2float，否则直接返回
    auto toFloat = [](T v) -> float {
        if constexpr (std::is_same_v<T, float>) return v;
        else return __half2float(v);
    };

    // ----- 格式 A：TensorRT 内置 NMS 输出 [x1,y1,x2,y2,score,class] -----
    if (attrs == 6) {
        std::size_t limit = (maxDet > 0) ? std::min(rowCount, static_cast<std::size_t>(maxDet)) : rowCount;
        detections.reserve(limit);
        for (std::size_t i = 0; i < rowCount && detections.size() < limit; ++i) {
            const T* row = output + (rowStart + i) * attrs;
            float score = toFloat(row[4]);          // 第 5 个字段：置信度
            if (score < confThresh) continue;
            int cls = static_cast<int>(std::round(toFloat(row[5])));  // 第 6 个字段：类别 ID
            if (cls < 0) continue;

            // 将框从 letterbox 坐标系还原到原图坐标系
            cv::Rect2f box = UndoLetterbox(toFloat(row[0]), toFloat(row[1]),
                                           toFloat(row[2]), toFloat(row[3]),
                                           meta, srcW, srcH);
            if (box.width < 1.0F || box.height < 1.0F) continue;

            Detection d;
            d.classId    = cls;
            d.confidence = score;
            d.box        = box;
            d.className  = (cls < static_cast<int>(classNames.size()))
                               ? classNames[cls] : std::to_string(cls);
            detections.push_back(std::move(d));
        }
        return;
    }

    // ----- 格式 B：常规 YOLO 输出 [cx, cy, w, h, obj, class_probs...] -----
    // 第一遍遍历：提取所有高于阈值的候选框
    cands.clear();
    cands.reserve(rowCount);
    for (std::size_t i = 0; i < rowCount; ++i) {
        const T* row = output + (rowStart + i) * attrs;
        float obj = toFloat(row[4]);           // 第 5 个字段：objectness（是否存在目标）
        if (obj <= 0.0F) continue;

        // 在所有类别概率中找到最大值
        int bestCls = -1;
        float bestScore = 0.0F;
        const int numClasses = attrs - 5;
        for (int c = 0; c < numClasses; ++c) {
            float s = toFloat(row[5 + c]);
            if (s > bestScore) { bestScore = s; bestCls = c; }
        }
        if (bestCls < 0) continue;

        // 综合分数 = objectness * 类别概率
        float score = obj * bestScore;
        if (score < confThresh) continue;

        // 将中心点格式 (cx,cy,w,h) 转换为左上角格式 (x1,y1,x2,y2) 后还原坐标
        float cx = toFloat(row[0]), cy = toFloat(row[1]);
        float w2 = toFloat(row[2]) * 0.5F, h2 = toFloat(row[3]) * 0.5F;
        cv::Rect2f box = UndoLetterbox(cx - w2, cy - h2, cx + w2, cy + h2, meta, srcW, srcH);
        if (box.width < 1.0F || box.height < 1.0F) continue;

        cands.push_back({bestCls, score, box});
    }

    // ----- CPU 侧 NMS（非极大值抑制）-----
    // 按分数从高到低排序，逐个取最高分框，抑制所有与其 IoU 过高的框
    std::sort(cands.begin(), cands.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    removed.assign(cands.size(), 0);    // 标记数组：0=保留，1=被抑制
    detections.clear();
    detections.reserve(cands.size());

    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (removed[i]) continue;       // 已被前面的高分配框抑制，跳过
        const Candidate& c = cands[i];
        Detection d;
        d.classId    = c.classId;
        d.confidence = c.score;
        d.box        = c.box;
        d.className  = (c.classId >= 0 && c.classId < static_cast<int>(classNames.size()))
                           ? classNames[c.classId] : std::to_string(c.classId);
        detections.push_back(std::move(d));

        // 达到最大检测数上限时提前终止
        if (maxDet > 0 && static_cast<int>(detections.size()) >= maxDet) break;

        // 抑制与当前框 IoU 超过阈值的后续候选框
        for (std::size_t j = i + 1; j < cands.size(); ++j) {
            if (removed[j]) continue;
            if (IoU(c.box, cands[j].box) > iouThresh) removed[j] = 1;
        }
    }
}

}  // anonymous namespace

// ============================================================================
// TrtYoloDetector::Impl —— PIMPL（Pointer to Implementation）内部实现结构体。
//
// 使用 PIMPL 模式将 TensorRT 相关的所有头文件和 GPU 资源封装在 .cpp 中，
// 避免向头文件的使用者暴露 TensorRT / CUDA 依赖。
//
// 成员说明：
//   logger              - TensorRT 日志记录器
//   runtime             - TensorRT 推理运行时对象
//   engine              - 反序列化后的 CUDA 推理引擎
//   context             - 推理执行上下文（绑定输入输出、管理内存地址）
//   inputTensorName     - TensorRT 10.x：输入张量名称
//   outputTensorName    - TensorRT 10.x：输出张量名称
//   inputIndex          - TensorRT 8.x：输入 binding 索引
//   outputIndex         - TensorRT 8.x：输出 binding 索引
//   inputDims/outputDims - 输入/输出张量的维度描述
//   inputType/outputType - 输入/输出张量的数据类型（kFLOAT 或 kHALF）
//   inputW/inputH        - 模型要求的输入宽高（如 640x640）
//   inputElements/outputElements - 输入/输出张量的元素总数
//   inputCapacityElements  - 输入 GPU 缓冲区的实际容量（动态 batch 时大于 elements）
//   outputCapacityElements - 输出 GPU 缓冲区的实际容量（动态 batch 时大于 elements）
//   inputDevice/outputDevice - GPU 显存上的输入/输出缓冲区指针
//   stream               - CUDA 流，用于异步数据传输和推理
//   hostInputFloat       - CPU 端输入数据缓冲区（统一按 float 存储）
//   hostOutputFloat      - CPU 端输出数据缓冲区（float 格式）
//   hostOutputHalf       - CPU 端输出数据缓冲区（half 格式，仅输出为 kHALF 时使用）
//   hostInputPinned      - 锁页内存输入指针
//   hostOutputFloatPinned - 锁页内存输出指针（float）
//   hostOutputHalfPinned  - 锁页内存输出指针（half）
//   resizedScratch/paddedScratch - 预处理阶段的临时 Mat
//   candidates/removed    - 输出解码/NMS 阶段的临时缓冲区
//   classNames            - 类别名称列表
//   ready                 - 模型是否已加载并可推理
//   supportsDynamicBatch  - 引擎是否支持动态 batch 维度（当前工程强制关闭）
// ============================================================================
struct TrtYoloDetector::Impl {
    TrtLogger logger;
    std::unique_ptr<nvinfer1::IRuntime,          TrtDeleter<nvinfer1::IRuntime>>        runtime;
    std::unique_ptr<nvinfer1::ICudaEngine,       TrtDeleter<nvinfer1::ICudaEngine>>     engine;
    std::unique_ptr<nvinfer1::IExecutionContext, TrtDeleter<nvinfer1::IExecutionContext>> context;

#if NV_TENSORRT_MAJOR >= 10
    std::string inputTensorName;
    std::string outputTensorName;
#else
    int inputIndex  = -1;
    int outputIndex = -1;
#endif

    nvinfer1::Dims     inputDims{}, outputDims{};
    nvinfer1::DataType inputType = nvinfer1::DataType::kFLOAT;
    nvinfer1::DataType outputType = nvinfer1::DataType::kFLOAT;

    int inputW = 640, inputH = 640;
    std::size_t inputElements = 0, outputElements = 0;
    std::size_t inputCapacityElements = 0, outputCapacityElements = 0;

    void*       inputDevice  = nullptr;
    void*       outputDevice = nullptr;
    cudaStream_t stream      = nullptr;

    std::vector<float>  hostInputFloat;
    std::vector<float>  hostOutputFloat;
    std::vector<__half> hostOutputHalf;   // 仅在输出类型为 kHALF 时使用

    // 锁页内存用于加速 CPU↔GPU 的数据拷贝，避免隐式的 pageable→pinned 转换开销。
    float* hostInputPinned = nullptr;
    float* hostOutputFloatPinned = nullptr;
    __half* hostOutputHalfPinned = nullptr;

    cv::Mat resizedScratch, paddedScratch;

    // 输出解码阶段使用的临时缓冲区，避免每帧重新分配内存。
    std::vector<Candidate> candidates;
    std::vector<uint8_t>   removed;

    std::vector<std::string> classNames;

    bool ready               = false;
    bool supportsDynamicBatch = false;

    // 析构函数：释放 GPU 显存和 CUDA 流资源。
    // 副作用：释放 inputDevice/outputDevice 显存，销毁 CUDA 流。
    ~Impl() {
        if (inputDevice)  { cudaFree(inputDevice);  inputDevice  = nullptr; }
        if (outputDevice) { cudaFree(outputDevice); outputDevice = nullptr; }
        if (stream)       { cudaStreamDestroy(stream); stream = nullptr; }
    }
};

// 公共 API：构造和析构都只管理 Impl 指针（PIMPL 惯用法）。
TrtYoloDetector::TrtYoloDetector() : impl_(new Impl()) {}
TrtYoloDetector::~TrtYoloDetector() = default;

// ============================================================================
// TrtYoloDetector::Load —— 加载 TensorRT 引擎文件并初始化推理所需的所有资源。
//
// 处理流程：
//   1. 读取 .engine 文件二进制内容；
//   2. 创建 IRuntime 并反序列化为 ICudaEngine；
//   3. 创建 IExecutionContext；
//   4. 解析输入/输出张量的名称（10.x）或 binding 索引（8.x）；
//   5. 获取张量的维度、数据类型；
//   6. 处理动态形状（固定 batch=1, channel=3, H, W）；
//   7. 在 GPU 上分配输入/输出显存缓冲区（动态 batch 时按 8 倍扩容）；
//   8. 创建 CUDA 流；
//   9. 在 CPU 侧分配主机缓冲区；
//   10. 保存类别名称列表。
//
// 参数：
//   enginePath  - .engine 文件的文件系统路径
//   classNames  - 类别名称列表（如 {"R1","R2","B1","B2",...}）
//   inputWidth  - 模型输入宽度（如 640）
//   inputHeight - 模型输入高度（如 640）
//   error       - [输出] 失败时写入错误描述字符串，可为 nullptr
//
// 返回：成功返回 true；任何步骤失败返回 false 并设置 error。
// 副作用：
//   - 释放旧的 runtime/engine/context 对象；
//   - 在 GPU 上分配显存（cudaMalloc）；
//   - 创建 CUDA 流（cudaStreamCreate）；
//   - 设置 impl_->ready = true。
//   - 强制关闭动态 batch（impl_->supportsDynamicBatch = false）。
// ============================================================================
bool TrtYoloDetector::Load(const std::string& enginePath,
                           const std::vector<std::string>& classNames,
                           int inputWidth, int inputHeight, std::string* error) {
    impl_->ready = false;
    impl_->supportsDynamicBatch = false;

    // 步骤 1：打开 engine 文件并读取全部二进制内容到内存
    std::ifstream fin(enginePath, std::ios::binary);
    if (!fin) {
        if (error) *error = "failed to open TensorRT engine: " + enginePath;
        return false;
    }
    fin.seekg(0, std::ios::end);
    const std::streamsize size = fin.tellg();
    fin.seekg(0, std::ios::beg);
    if (size <= 0) {
        if (error) *error = "empty TensorRT engine file: " + enginePath;
        return false;
    }

    std::vector<char> blob(static_cast<std::size_t>(size));
    if (!fin.read(blob.data(), size)) {
        if (error) *error = "failed to read engine bytes: " + enginePath;
        return false;
    }

    // 步骤 2：创建运行时并反序列化引擎
    impl_->runtime.reset(nvinfer1::createInferRuntime(impl_->logger));
    if (!impl_->runtime) {
        if (error) *error = "createInferRuntime failed";
        return false;
    }

    impl_->engine.reset(impl_->runtime->deserializeCudaEngine(blob.data(), blob.size()));
    if (!impl_->engine) {
        if (error) *error = "deserializeCudaEngine failed for: " + enginePath;
        return false;
    }

    // 步骤 3：创建执行上下文
    impl_->context.reset(impl_->engine->createExecutionContext());
    if (!impl_->context) {
        if (error) *error = "createExecutionContext failed";
        return false;
    }

    impl_->inputW = inputWidth;
    impl_->inputH = inputHeight;

    // 步骤 4-5：解析输入输出张量（10.x 用 tensor 名称，8.x 用 binding 索引）
#if NV_TENSORRT_MAJOR >= 10
    // TensorRT 10.x：通过 getIOTensorName / getTensorIOMode 获取张量名
    for (int32_t i = 0; i < impl_->engine->getNbIOTensors(); ++i) {
        const char* name = impl_->engine->getIOTensorName(i);
        if (!name) continue;
        auto mode = impl_->engine->getTensorIOMode(name);
        if (mode == nvinfer1::TensorIOMode::kINPUT && impl_->inputTensorName.empty())
            impl_->inputTensorName = name;
        else if (mode == nvinfer1::TensorIOMode::kOUTPUT && impl_->outputTensorName.empty())
            impl_->outputTensorName = name;
    }
    if (impl_->inputTensorName.empty() || impl_->outputTensorName.empty()) {
        if (error) *error = "failed to resolve input/output tensor names";
        return false;
    }

    nvinfer1::Dims inDims = impl_->engine->getTensorShape(impl_->inputTensorName.c_str());
    impl_->supportsDynamicBatch = (inDims.nbDims >= 1 && inDims.d[0] < 0);
    // 步骤 6：如果是动态形状，固定为 {1, 3, H, W}
    if (HasDynamicShape(inDims)) {
        nvinfer1::Dims4 fixedDims{1, 3, inputHeight, inputWidth};
        if (!impl_->context->setInputShape(impl_->inputTensorName.c_str(), fixedDims)) {
            if (error) *error = "setInputShape failed";
            return false;
        }
    }
    impl_->inputDims  = impl_->context->getTensorShape(impl_->inputTensorName.c_str());
    impl_->outputDims = impl_->context->getTensorShape(impl_->outputTensorName.c_str());
    impl_->inputType  = impl_->engine->getTensorDataType(impl_->inputTensorName.c_str());
    impl_->outputType = impl_->engine->getTensorDataType(impl_->outputTensorName.c_str());
#else
    // TensorRT 8.x：通过 binding 索引区分输入/输出
    for (int i = 0; i < impl_->engine->getNbBindings(); ++i) {
        if (impl_->engine->bindingIsInput(i)) impl_->inputIndex = i;
        else impl_->outputIndex = i;
    }
    if (impl_->inputIndex < 0 || impl_->outputIndex < 0) {
        if (error) *error = "failed to resolve input/output binding indices";
        return false;
    }

    nvinfer1::Dims inDims = impl_->engine->getBindingDimensions(impl_->inputIndex);
    impl_->supportsDynamicBatch = (inDims.nbDims >= 1 && inDims.d[0] < 0);
    // 步骤 6：如果是动态形状，固定为 {1, 3, H, W}
    if (HasDynamicShape(inDims)) {
        nvinfer1::Dims4 fixedDims{1, 3, inputHeight, inputWidth};
        if (!impl_->context->setBindingDimensions(impl_->inputIndex, fixedDims)) {
            if (error) *error = "setBindingDimensions failed";
            return false;
        }
    }
    impl_->inputDims  = impl_->context->getBindingDimensions(impl_->inputIndex);
    impl_->outputDims = impl_->context->getBindingDimensions(impl_->outputIndex);
    impl_->inputType  = impl_->engine->getBindingDataType(impl_->inputIndex);
    impl_->outputType = impl_->engine->getBindingDataType(impl_->outputIndex);
#endif

    // 计算张量元素总数，校验非空
    impl_->inputElements  = Volume(impl_->inputDims);
    impl_->outputElements = Volume(impl_->outputDims);
    if (impl_->inputElements == 0 || impl_->outputElements == 0) {
        if (error) *error = "invalid binding dims, cannot compute tensor volume";
        return false;
    }

    // 步骤 7：分配 GPU 显存缓冲区。动态 batch 时按 8 倍预留以支持更大 batch。
    if (impl_->supportsDynamicBatch) {
        impl_->inputCapacityElements  = ExpandElementsForReserve(impl_->inputElements,  kDynamicInitialReserveFactor);
        impl_->outputCapacityElements = ExpandElementsForReserve(impl_->outputElements, kDynamicInitialReserveFactor);
    } else {
        impl_->inputCapacityElements  = impl_->inputElements;
        impl_->outputCapacityElements = impl_->outputElements;
    }

    const std::size_t inputBytes  = impl_->inputCapacityElements  * ElementSize(impl_->inputType);
    const std::size_t outputBytes = impl_->outputCapacityElements * ElementSize(impl_->outputType);

    if (cudaMalloc(&impl_->inputDevice, inputBytes) != cudaSuccess ||
        cudaMalloc(&impl_->outputDevice, outputBytes) != cudaSuccess) {
        if (error) *error = "cudaMalloc failed for TensorRT buffers";
        return false;
    }

    // 步骤 8：创建 CUDA 流，用于异步传输和推理
    if (cudaStreamCreate(&impl_->stream) != cudaSuccess) {
        if (error) *error = "cudaStreamCreate failed";
        return false;
    }

    // 步骤 9：CPU 侧主机缓冲区分配。输入统一为 float，输出按实际类型分配。
    impl_->hostInputFloat.resize(impl_->inputCapacityElements);
    if (impl_->outputType == nvinfer1::DataType::kFLOAT) {
        impl_->hostOutputFloat.resize(impl_->outputCapacityElements);
    } else if (impl_->outputType == nvinfer1::DataType::kHALF) {
        // half 输出：同时分配 half 缓冲和 float 缓冲，
        // half 用于接收 GPU→CPU 的原始数据，float 用于后续解码（DecodeOutputRows 内转换）
        impl_->hostOutputHalf.resize(impl_->outputCapacityElements);
        impl_->hostOutputFloat.resize(impl_->outputCapacityElements);
    } else {
        if (error) *error = "unsupported output data type";
        return false;
    }

    // 步骤 10：保存类别名称
    impl_->classNames = classNames;

    // 为避免当前工程运行路径变化，这里退回到逐图像推理模式，不使用动态 batch。
    impl_->supportsDynamicBatch = false;
    impl_->ready = true;
    return true;
}

// ============================================================================
// TrtYoloDetector::IsReady —— 查询检测器是否已完成初始化并可用于推理。
// 返回：模型已加载完毕返回 true，否则 false。
// ============================================================================
bool TrtYoloDetector::IsReady() const { return impl_->ready; }

// ============================================================================
// TrtYoloDetector::SupportsDynamicBatch —— 查询引擎是否支持动态 batch。
// 注意：当前工程实现中强制返回 false（见 Load() 末尾），因此此方法始终返回 false。
// 返回：理论上支持返回 true；当前一律返回 false。
// ============================================================================
bool TrtYoloDetector::SupportsDynamicBatch() const {
    return impl_->ready && impl_->supportsDynamicBatch;
}

// ============================================================================
// TrtYoloDetector::Infer —— 对单张 BGR 图像执行 YOLO 目标检测推理。
//
// 完整推理流水线：
//   1. 校验初始化状态；
//   2. 设置输入形状和 GPU 内存地址（10.x）/ binding 维度（8.x）；
//   3. 预处理：letterbox 缩放填充 + 通道拆分，写入 CHW float 缓冲区；
//   4. 上传输入数据到 GPU 显存（float→float 或 float→half 转换后上传）；
//   5. 执行 TensorRT 推理（enqueueV2/enqueueV3）；
//   6. 下载输出数据到 CPU（根据输出类型选择 float 或 half）；
//   7. 同步 CUDA 流，确保 GPU 操作完成；
//   8. 解码输出 + NMS 后处理，生成最终 Detection 列表。
//
// 参数：
//   bgr        - 输入 OpenCV BGR 图像（CV_8UC3）
//   confThresh - 置信度阈值（0.0~1.0），低于此值的检测结果被丢弃
//   iouThresh  - NMS IoU 阈值（0.0~1.0），高于此值的重叠框被抑制
//   maxDet     - 最大检测输出数（0 表示不限制）
//   detections - [输出] 检测结果列表，调用前被清空
//   error      - [输出] 失败时写入错误描述，可为 nullptr
//
// 返回：成功返回 true，失败返回 false 并设置 error。
// 副作用：
//   - 清空并重新填充 detections 指针指向的 vector；
//   - 修改 impl_ 内部的 CPU 输入/输出缓冲区内容；
//   - 修改 impl_ 内部的 GPU 显存内容（通过异步拷贝和推理）；
//   - 修改 impl_->resizedScratch、paddedScratch 临时 Mat；
//   - 修改 impl_ 内部的 candidates 和 removed 临时缓冲区。
// ============================================================================
bool TrtYoloDetector::Infer(const cv::Mat& bgr, float confThresh, float iouThresh,
                            int maxDet, std::vector<Detection>* detections, std::string* error) {
    detections->clear();
    if (!impl_->ready) {
        if (error) *error = "detector is not initialized";
        return false;
    }

    // 步骤 2：设置输入形状和内存地址（10.x）或 binding 维度（8.x）
    // 每次推理前重新设置，防止被上一次 batch 推理修改。
#if NV_TENSORRT_MAJOR >= 10
    impl_->context->setInputShape(impl_->inputTensorName.c_str(), impl_->inputDims);
    if (!impl_->context->setTensorAddress(impl_->inputTensorName.c_str(), impl_->inputDevice)) {
        if (error) *error = "setTensorAddress(input) failed";
        return false;
    }
    if (!impl_->context->setTensorAddress(impl_->outputTensorName.c_str(), impl_->outputDevice)) {
        if (error) *error = "setTensorAddress(output) failed";
        return false;
    }
#else
    impl_->context->setBindingDimensions(impl_->inputIndex, impl_->inputDims);
#endif

    // 步骤 3：图像预处理 —— letterbox 缩放 + 通道拆分
    LetterboxMeta meta;
    if (!PreprocessToFloatPlanes(bgr, impl_->inputW, impl_->inputH,
                                 impl_->resizedScratch, impl_->paddedScratch,
                                 impl_->hostInputFloat.data(), meta, error))
        return false;

    // 步骤 4：上传输入数据到 GPU（Upload）
    std::size_t inputBytes = impl_->inputElements * ElementSize(impl_->inputType);
    if (impl_->inputType == nvinfer1::DataType::kFLOAT) {
        // float 输入：直接从 hostInputFloat 拷贝到 GPU
        if (cudaMemcpyAsync(impl_->inputDevice, impl_->hostInputFloat.data(),
                            inputBytes, cudaMemcpyHostToDevice, impl_->stream) != cudaSuccess) {
            if (error) *error = "cudaMemcpyAsync input failed";
            return false;
        }
    } else { // 输入类型为 kHALF（FP16）
        // 将 float 数组转为 half 后上传。使用临时缓冲区避免覆盖 hostOutputHalf。
        // Converter::convert：利用 OpenCV parallel_for_ 多线程加速 float→half 转换。
        struct Converter {
            static void convert(const float* src, __half* dst, std::size_t n) {
                cv::parallel_for_(cv::Range(0, static_cast<int>(n)),
                    [&](const cv::Range& r){
                        for (int i = r.start; i < r.end; ++i)
                            dst[i] = __float2half(src[i]);
                    });
            }
        };
        std::vector<__half> tmpHalf;
        try {
            tmpHalf.resize(impl_->inputElements);
        } catch (...) {
            if (error) *error = "failed to allocate temporary half buffer";
            return false;
        }
        Converter::convert(impl_->hostInputFloat.data(), tmpHalf.data(), impl_->inputElements);

        if (cudaMemcpyAsync(impl_->inputDevice, tmpHalf.data(),
                            impl_->inputElements * sizeof(__half),
                            cudaMemcpyHostToDevice, impl_->stream) != cudaSuccess) {
            if (error) *error = "cudaMemcpyAsync input (half) failed";
            return false;
        }
    }

    // 步骤 5：执行推理（Run）
#if NV_TENSORRT_MAJOR >= 10
    // TensorRT 10.x：使用 enqueueV3，张量地址已通过 setTensorAddress 绑定
    if (!impl_->context->enqueueV3(impl_->stream)) {
        if (error) *error = "TensorRT enqueueV3 failed";
        return false;
    }
#else
    // TensorRT 8.x：使用 enqueueV2，通过 bindings 数组传递输入/输出 GPU 指针
    std::vector<void*> bindings(impl_->engine->getNbBindings(), nullptr);
    bindings[impl_->inputIndex]  = impl_->inputDevice;
    bindings[impl_->outputIndex] = impl_->outputDevice;
    if (!impl_->context->enqueueV2(bindings.data(), impl_->stream, nullptr)) {
        if (error) *error = "TensorRT enqueueV2 failed";
        return false;
    }
#endif

    // 步骤 6：下载输出数据到 CPU（Download）
    std::size_t outputBytes = impl_->outputElements * ElementSize(impl_->outputType);
    if (impl_->outputType == nvinfer1::DataType::kFLOAT) {
        if (cudaMemcpyAsync(impl_->hostOutputFloat.data(), impl_->outputDevice,
                            outputBytes, cudaMemcpyDeviceToHost, impl_->stream) != cudaSuccess) {
            if (error) *error = "cudaMemcpyAsync output failed";
            return false;
        }
    } else { // 输出类型为 kHALF
        // 先以 half 格式下载，解码时在 DecodeOutputRows 内通过 __half2float 转换
        if (cudaMemcpyAsync(impl_->hostOutputHalf.data(), impl_->outputDevice,
                            outputBytes, cudaMemcpyDeviceToHost, impl_->stream) != cudaSuccess) {
            if (error) *error = "cudaMemcpyAsync output (half) failed";
            return false;
        }
    }

    // 步骤 7：同步 CUDA 流，确保所有异步 GPU 操作（上传+推理+下载）已完成
    cudaStreamSynchronize(impl_->stream);

    // 步骤 8：解码输出 + NMS
    // 最后一个维度即为每行的属性数（输出格式为 [rows, attrs]）
    int attrs = impl_->outputDims.d[impl_->outputDims.nbDims - 1];
    if (attrs <= 0) { if (error) *error = "invalid output attrs"; return false; }
    const std::size_t rows = impl_->outputElements / attrs;

    if (impl_->outputType == nvinfer1::DataType::kFLOAT) {
        DecodeOutputRows(impl_->hostOutputFloat.data(), 0, rows, attrs, confThresh, iouThresh,
                         maxDet, meta, bgr.cols, bgr.rows,
                         impl_->classNames, impl_->removed, impl_->candidates, *detections);
    } else {
        // half 输出：DecodeOutputRows 内部通过 __half2float 完成转换
        DecodeOutputRows(impl_->hostOutputHalf.data(), 0, rows, attrs, confThresh, iouThresh,
                         maxDet, meta, bgr.cols, bgr.rows,
                         impl_->classNames, impl_->removed, impl_->candidates, *detections);
    }
    return true;
}

// ============================================================================
// TrtYoloDetector::InferBatch —— 批量推理接口。
//
// 当前实现为退避模式：不支持真正的动态 batch 推理，而是逐帧循环调用单图像 Infer()。
// 这样做保证了代码的简单性和稳定性，代价是批处理时没有 GPU 并行加速。
//
// 参数：
//   bgrBatch       - 输入图像列表，每张为 CV_8UC3 BGR 格式
//   confThresh     - 置信度阈值
//   iouThresh      - NMS IoU 阈值
//   maxDet         - 最大检测输出数
//   detectionsBatch - [输出] 每张图像对应的检测结果列表的列表，调用前被清空
//   error          - [输出] 失败时写入错误描述，可为 nullptr
//
// 返回：全部图像推理成功返回 true；任一图像失败返回 false 并设置 error。
// 副作用：逐帧修改 GPU 显存和内部缓冲区（与 Infer() 相同）。
// ============================================================================
bool TrtYoloDetector::InferBatch(const std::vector<cv::Mat>& bgrBatch,
                                 float confThresh, float iouThresh, int maxDet,
                                 std::vector<std::vector<Detection>>* detectionsBatch,
                                 std::string* error) {
    // 回退实现：不使用动态 batch，将每帧依次调用单图像 Infer()
    detectionsBatch->clear();
    if (!impl_->ready) { if (error) *error = "not initialized"; return false; }
    const std::size_t n = bgrBatch.size();
    if (n == 0) return true;    // 空列表视为成功
    detectionsBatch->resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        // 逐帧推理，任一帧失败则整个批次失败
        if (!Infer(bgrBatch[i], confThresh, iouThresh, maxDet, &(*detectionsBatch)[i], error))
            return false;
    }
    return true;
}

}  // namespace radar26
