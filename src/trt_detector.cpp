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

// ---------- Logger ----------
class TrtLogger final : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity == Severity::kERROR || severity == Severity::kINTERNAL_ERROR)
            std::fprintf(stderr, "[TensorRT] %s\n", msg);
    }
};

// ---------- RAII deleter for TensorRT objects ----------
template <typename T>
struct TrtDeleter {
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

// ---------- Tensor helpers ----------
std::size_t Volume(const nvinfer1::Dims& dims) {
    std::size_t v = 1;
    for (int i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] <= 0) return 0;
        v *= static_cast<std::size_t>(dims.d[i]);
    }
    return v;
}

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

bool HasDynamicShape(const nvinfer1::Dims& dims) {
    for (int i = 0; i < dims.nbDims; ++i)
        if (dims.d[i] < 0) return true;
    return false;
}

// ---------- Memory growth helpers ----------
constexpr std::size_t kDynamicInitialReserveFactor = 8U;
constexpr std::size_t kDynamicGrowFactor          = 2U;

std::size_t ExpandElementsForReserve(std::size_t required, std::size_t factor) {
    if (required == 0U || factor <= 1U) return required;
    if (required > std::numeric_limits<std::size_t>::max() / factor) return required;
    return required * factor;
}

// ---------- Letterbox metadata ----------
struct LetterboxMeta {
    float ratio   = 1.0F;
    float padLeft = 0.0F;
    float padTop  = 0.0F;
};

// ---------- Per‑candidate for CPU NMS ----------
struct Candidate {
    int         classId = -1;
    float       score   = 0.0F;
    cv::Rect2f  box;
};

// ---------- Preprocessing ----------
// Write (R,G,B) planes to chw buffer; output is always float first, then optionally converted to half.
bool PreprocessToFloatPlanes(const cv::Mat& bgr, int dstW, int dstH,
                             cv::Mat& resizedScratch, cv::Mat& paddedScratch,
                             float* chw, LetterboxMeta& meta, std::string* error) {
    if (bgr.empty()) {
        if (error) *error = "input image is empty";
        return false;
    }

    const float r = std::min(static_cast<float>(dstW) / bgr.cols,
                             static_cast<float>(dstH) / bgr.rows);
    const int unpadW = static_cast<int>(std::round(bgr.cols * r));
    const int unpadH = static_cast<int>(std::round(bgr.rows * r));

    cv::resize(bgr, resizedScratch, cv::Size(unpadW, unpadH));

    const float dw = static_cast<float>(dstW - unpadW) * 0.5F;
    const float dh = static_cast<float>(dstH - unpadH) * 0.5F;
    const int left   = static_cast<int>(std::round(dw - 0.1F));
    const int top    = static_cast<int>(std::round(dh - 0.1F));

    if (left < 0 || top < 0 || left + unpadW > dstW || top + unpadH > dstH) {
        if (error) *error = "invalid letterbox padding values";
        return false;
    }

    paddedScratch.create(dstH, dstW, CV_8UC3);
    paddedScratch.setTo(cv::Scalar(114, 114, 114));
    resizedScratch.copyTo(paddedScratch(cv::Rect(left, top, unpadW, unpadH)));

    // Convert to float [0,1] and split channels → R, G, B planes
    cv::Mat floatImg;
    paddedScratch.convertTo(floatImg, CV_32FC3, 1.0 / 255.0);

    std::array<cv::Mat, 3> channels;
    cv::split(floatImg, channels.data());  // B, G, R

    const std::size_t planeSize = static_cast<std::size_t>(dstW) * dstH;
    // Copy to interleaved planes: R, G, B
    std::memcpy(chw,                 channels[2].data, planeSize * sizeof(float)); // R
    std::memcpy(chw + planeSize,     channels[1].data, planeSize * sizeof(float)); // G
    std::memcpy(chw + planeSize * 2, channels[0].data, planeSize * sizeof(float)); // B

    meta.ratio   = r;
    meta.padLeft = static_cast<float>(left);
    meta.padTop  = static_cast<float>(top);
    return true;
}

// ---------- Detection decode helpers ----------
float IoU(const cv::Rect2f& a, const cv::Rect2f& b) {
    const float xx1 = std::max(a.x, b.x);
    const float yy1 = std::max(a.y, b.y);
    const float xx2 = std::min(a.x + a.width,  b.x + b.width);
    const float yy2 = std::min(a.y + a.height, b.y + b.height);

    const float w = std::max(0.0F, xx2 - xx1);
    const float h = std::max(0.0F, yy2 - yy1);
    const float inter = w * h;
    const float uni   = a.area() + b.area() - inter;
    return (uni > 0.0F) ? inter / uni : 0.0F;
}

cv::Rect2f UndoLetterbox(float x1, float y1, float x2, float y2,
                         const LetterboxMeta& meta, int srcW, int srcH) {
    float rx1 = (x1 - meta.padLeft) / meta.ratio;
    float ry1 = (y1 - meta.padTop)  / meta.ratio;
    float rx2 = (x2 - meta.padLeft) / meta.ratio;
    float ry2 = (y2 - meta.padTop)  / meta.ratio;

    rx1 = std::clamp(rx1, 0.0F, static_cast<float>(srcW - 1));
    ry1 = std::clamp(ry1, 0.0F, static_cast<float>(srcH - 1));
    rx2 = std::clamp(rx2, 0.0F, static_cast<float>(srcW - 1));
    ry2 = std::clamp(ry2, 0.0F, static_cast<float>(srcH - 1));

    float w = std::max(0.0F, rx2 - rx1);
    float h = std::max(0.0F, ry2 - ry1);
    return {rx1, ry1, w, h};
}

// Unified decode for float/half output rows
template <typename T>
void DecodeOutputRows(const T* output, std::size_t rowStart, std::size_t rowCount,
                      int attrs, float confThresh, float iouThresh, int maxDet,
                      const LetterboxMeta& meta, int srcW, int srcH,
                      const std::vector<std::string>& classNames,
                      std::vector<uint8_t>& removed, std::vector<Candidate>& cands,
                      std::vector<Detection>& detections) {
    detections.clear();

    auto toFloat = [](T v) -> float {
        if constexpr (std::is_same_v<T, float>) return v;
        else return __half2float(v);
    };

    if (attrs == 6) {  // Built‑in NMS: rows are [x1,y1,x2,y2,score,class]
        std::size_t limit = (maxDet > 0) ? std::min(rowCount, static_cast<std::size_t>(maxDet)) : rowCount;
        detections.reserve(limit);
        for (std::size_t i = 0; i < rowCount && detections.size() < limit; ++i) {
            const T* row = output + (rowStart + i) * attrs;
            float score = toFloat(row[4]);
            if (score < confThresh) continue;
            int cls = static_cast<int>(std::round(toFloat(row[5])));
            if (cls < 0) continue;

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

    // Standard YOLO output: cx, cy, w, h, obj, class_probs...
    cands.clear();
    cands.reserve(rowCount);
    for (std::size_t i = 0; i < rowCount; ++i) {
        const T* row = output + (rowStart + i) * attrs;
        float obj = toFloat(row[4]);
        if (obj <= 0.0F) continue;

        int bestCls = -1;
        float bestScore = 0.0F;
        const int numClasses = attrs - 5;
        for (int c = 0; c < numClasses; ++c) {
            float s = toFloat(row[5 + c]);
            if (s > bestScore) { bestScore = s; bestCls = c; }
        }
        if (bestCls < 0) continue;

        float score = obj * bestScore;
        if (score < confThresh) continue;

        float cx = toFloat(row[0]), cy = toFloat(row[1]);
        float w2 = toFloat(row[2]) * 0.5F, h2 = toFloat(row[3]) * 0.5F;
        cv::Rect2f box = UndoLetterbox(cx - w2, cy - h2, cx + w2, cy + h2, meta, srcW, srcH);
        if (box.width < 1.0F || box.height < 1.0F) continue;

        cands.push_back({bestCls, score, box});
    }

    // NMS
    std::sort(cands.begin(), cands.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    removed.assign(cands.size(), 0);
    detections.clear();
    detections.reserve(cands.size());

    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (removed[i]) continue;
        const Candidate& c = cands[i];
        Detection d;
        d.classId    = c.classId;
        d.confidence = c.score;
        d.box        = c.box;
        d.className  = (c.classId >= 0 && c.classId < static_cast<int>(classNames.size()))
                           ? classNames[c.classId] : std::to_string(c.classId);
        detections.push_back(std::move(d));

        if (maxDet > 0 && static_cast<int>(detections.size()) >= maxDet) break;

        for (std::size_t j = i + 1; j < cands.size(); ++j) {
            if (removed[j]) continue;
            if (IoU(c.box, cands[j].box) > iouThresh) removed[j] = 1;
        }
    }
}

}  // anonymous namespace

// ─────────── Impl definition ───────────
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
    std::vector<__half> hostOutputHalf;   // only used when output is kHALF

    // Pinned host buffers to accelerate H2D/D2H copies and avoid reallocations
    float* hostInputPinned = nullptr;
    float* hostOutputFloatPinned = nullptr;
    __half* hostOutputHalfPinned = nullptr;

    cv::Mat resizedScratch, paddedScratch;

    // scratch for decode (CPU NMS branch)
    std::vector<Candidate> candidates;
    std::vector<uint8_t>   removed;

    std::vector<std::string> classNames;

    bool ready               = false;
    bool supportsDynamicBatch = false;

    ~Impl() {
        if (inputDevice)  { cudaFree(inputDevice);  inputDevice  = nullptr; }
        if (outputDevice) { cudaFree(outputDevice); outputDevice = nullptr; }
        if (stream)       { cudaStreamDestroy(stream); stream = nullptr; }
    }
};

// ─────────── Public API ───────────
TrtYoloDetector::TrtYoloDetector() : impl_(new Impl()) {}
TrtYoloDetector::~TrtYoloDetector() = default;

bool TrtYoloDetector::Load(const std::string& enginePath,
                           const std::vector<std::string>& classNames,
                           int inputWidth, int inputHeight, std::string* error) {
    impl_->ready = false;
    impl_->supportsDynamicBatch = false;

    // Read engine file
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

    impl_->context.reset(impl_->engine->createExecutionContext());
    if (!impl_->context) {
        if (error) *error = "createExecutionContext failed";
        return false;
    }

    impl_->inputW = inputWidth;
    impl_->inputH = inputHeight;

    // Resolve IO names / indices
#if NV_TENSORRT_MAJOR >= 10
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

    impl_->inputElements  = Volume(impl_->inputDims);
    impl_->outputElements = Volume(impl_->outputDims);
    if (impl_->inputElements == 0 || impl_->outputElements == 0) {
        if (error) *error = "invalid binding dims, cannot compute tensor volume";
        return false;
    }

    // Allocate device buffers (with growth reserve only when dynamic)
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

    if (cudaStreamCreate(&impl_->stream) != cudaSuccess) {
        if (error) *error = "cudaStreamCreate failed";
        return false;
    }

    // Host buffers (input always stored as float, converted to half on copy if needed)
    impl_->hostInputFloat.resize(impl_->inputCapacityElements);
    if (impl_->outputType == nvinfer1::DataType::kFLOAT) {
        impl_->hostOutputFloat.resize(impl_->outputCapacityElements);
    } else if (impl_->outputType == nvinfer1::DataType::kHALF) {
        impl_->hostOutputHalf.resize(impl_->outputCapacityElements);
        impl_->hostOutputFloat.resize(impl_->outputCapacityElements); // for final decode
    } else {
        if (error) *error = "unsupported output data type";
        return false;
    }

    impl_->classNames = classNames;
    // 回退：禁用动态 batch 支持，恢复到逐图像推理（与动态 batch 变更之前一致）
    impl_->supportsDynamicBatch = false;
    impl_->ready = true;
    return true;
}

bool TrtYoloDetector::IsReady() const { return impl_->ready; }
bool TrtYoloDetector::SupportsDynamicBatch() const {
    return impl_->ready && impl_->supportsDynamicBatch;
}

// ---------- Core inference (single image) ----------
bool TrtYoloDetector::Infer(const cv::Mat& bgr, float confThresh, float iouThresh,
                            int maxDet, std::vector<Detection>* detections, std::string* error) {
    detections->clear();
    if (!impl_->ready) {
        if (error) *error = "detector is not initialized";
        return false;
    }

    // Ensure static input shape is set (may have been changed by a previous batch call)
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

    // Preprocess to float planes
    LetterboxMeta meta;
    if (!PreprocessToFloatPlanes(bgr, impl_->inputW, impl_->inputH,
                                 impl_->resizedScratch, impl_->paddedScratch,
                                 impl_->hostInputFloat.data(), meta, error))
        return false;

    // Upload
    std::size_t inputBytes = impl_->inputElements * ElementSize(impl_->inputType);
    if (impl_->inputType == nvinfer1::DataType::kFLOAT) {
        if (cudaMemcpyAsync(impl_->inputDevice, impl_->hostInputFloat.data(),
                            inputBytes, cudaMemcpyHostToDevice, impl_->stream) != cudaSuccess) {
            if (error) *error = "cudaMemcpyAsync input failed";
            return false;
        }
    } else { // kHALF
        // Convert float planes to half on the fly into a temporary buffer to avoid
        // overwriting impl_->hostOutputHalf which may be used for output storage.
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

    // Run
#if NV_TENSORRT_MAJOR >= 10
    if (!impl_->context->enqueueV3(impl_->stream)) {
        if (error) *error = "TensorRT enqueueV3 failed";
        return false;
    }
#else
    std::vector<void*> bindings(impl_->engine->getNbBindings(), nullptr);
    bindings[impl_->inputIndex]  = impl_->inputDevice;
    bindings[impl_->outputIndex] = impl_->outputDevice;
    if (!impl_->context->enqueueV2(bindings.data(), impl_->stream, nullptr)) {
        if (error) *error = "TensorRT enqueueV2 failed";
        return false;
    }
#endif

    // Download
    std::size_t outputBytes = impl_->outputElements * ElementSize(impl_->outputType);
    if (impl_->outputType == nvinfer1::DataType::kFLOAT) {
        if (cudaMemcpyAsync(impl_->hostOutputFloat.data(), impl_->outputDevice,
                            outputBytes, cudaMemcpyDeviceToHost, impl_->stream) != cudaSuccess) {
            if (error) *error = "cudaMemcpyAsync output failed";
            return false;
        }
    } else { // kHALF
        if (cudaMemcpyAsync(impl_->hostOutputHalf.data(), impl_->outputDevice,
                            outputBytes, cudaMemcpyDeviceToHost, impl_->stream) != cudaSuccess) {
            if (error) *error = "cudaMemcpyAsync output (half) failed";
            return false;
        }
    }
    cudaStreamSynchronize(impl_->stream);

    // Decode
    int attrs = impl_->outputDims.d[impl_->outputDims.nbDims - 1];
    if (attrs <= 0) { if (error) *error = "invalid output attrs"; return false; }
    const std::size_t rows = impl_->outputElements / attrs;

    if (impl_->outputType == nvinfer1::DataType::kFLOAT) {
        DecodeOutputRows(impl_->hostOutputFloat.data(), 0, rows, attrs, confThresh, iouThresh,
                         maxDet, meta, bgr.cols, bgr.rows,
                         impl_->classNames, impl_->removed, impl_->candidates, *detections);
    } else {
        // half → float conversion integrated in DecodeOutputRows via __half2float
        DecodeOutputRows(impl_->hostOutputHalf.data(), 0, rows, attrs, confThresh, iouThresh,
                         maxDet, meta, bgr.cols, bgr.rows,
                         impl_->classNames, impl_->removed, impl_->candidates, *detections);
    }
    return true;
}

// ---------- Batch inference ----------
bool TrtYoloDetector::InferBatch(const std::vector<cv::Mat>& bgrBatch,
                                 float confThresh, float iouThresh, int maxDet,
                                 std::vector<std::vector<Detection>>* detectionsBatch,
                                 std::string* error) {
    // 回退实现：不使用动态 batch，将每帧依次调用单图像 Infer()
    detectionsBatch->clear();
    if (!impl_->ready) { if (error) *error = "not initialized"; return false; }
    const std::size_t n = bgrBatch.size();
    if (n == 0) return true;
    detectionsBatch->resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!Infer(bgrBatch[i], confThresh, iouThresh, maxDet, &(*detectionsBatch)[i], error))
            return false;
    }
    return true;
}

}  // namespace radar26