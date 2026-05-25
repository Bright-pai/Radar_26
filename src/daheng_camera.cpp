/**
 * @file    daheng_camera.cpp
 * @brief   大恒（Daheng）相机驱动封装实现。
 *
 * 本文件通过 Pimpl 模式封装大恒 Galaxy SDK（GxIAPI），将 Raw Bayer 图像
 * 转换为 OpenCV BGR 格式，供上层雷达检测应用消费。
 *
 * 编译选项：
 *   -DRADAR26_WITH_DAHENG=ON  链接大恒 SDK 与 DxImageProc 库
 *   -DRADAR26_WITH_DAHENG=OFF 生成空实现，避免依赖大恒头文件
 */

#include "daheng_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <vector>

// 默认禁用大恒支持，由 CMake 通过 target_compile_definitions 覆写
#if !defined(RADAR26_WITH_DAHENG)
#define RADAR26_WITH_DAHENG 0
#endif

#if RADAR26_WITH_DAHENG
#include "DxImageProc.h"      // DxRaw8toRGB24：Bayer -> RGB 转换
#include "GxIAPI.h"           // 大恒 Galaxy SDK C 接口
#include "GxPixelFormat.h"    // 像素格式枚举（BAYER_GR8 等）
#endif

namespace radar26 {
namespace {

/**
 * @brief 将大恒 SDK 的 GX_STATUS 错误码转换为便于日志排查的文本。
 *
 * @param code  GX_STATUS 类型的 SDK 返回码
 * @return      形如 "GX_STATUS=-8" 的字符串
 *
 * 副作用：无。
 */
std::string GxErrorText(int code) {
    std::ostringstream oss;
    oss << "GX_STATUS=" << code;
    return oss.str();
}

}  // namespace

/**
 * @brief DahengCamera 的内部实现结构体（Pimpl 模式）。
 *
 * 将大恒 SDK 的所有句柄与状态封装在此处，避免在公开头文件
 * daheng_camera.hpp 中引入大恒 SDK 头文件，降低编译依赖。
 *
 * 成员：
 *   - opened:        相机是否已成功打开并推流
 *   - options:       当前生效的相机配置快照
 *   - device:        大恒设备句柄（GX_DEV_HANDLE），nullptr 表示未打开
 *   - dataStream:    数据流句柄（GX_DS_HANDLE），用于查询 Payload 大小
 *   - colorFilter:   传感器 Bayer 颜色滤镜类型（GX_COLOR_FILTER_BAYER_RG 等）
 *   - rgbBuffer:     预分配的 RGB 中间缓冲区，避免每帧重新分配内存
 */
struct DahengCamera::Impl {
    bool opened = false;
    DahengCameraOptions options;

#if RADAR26_WITH_DAHENG
    GX_DEV_HANDLE device = nullptr;
    GX_DS_HANDLE dataStream = nullptr;
    int64_t colorFilter = GX_COLOR_FILTER_NONE;
    std::vector<unsigned char> rgbBuffer;
#endif
};

/**
 * @brief 构造函数：分配 Impl 对象，相机处于未打开状态。
 *
 * 参数：无。
 * 返回：无。
 * 副作用：在堆上分配 Impl 内存（unique_ptr）。
 */
DahengCamera::DahengCamera() : impl_(std::make_unique<Impl>()) {}

/**
 * @brief 析构函数：自动调用 Close() 释放大恒资源。
 *
 * 参数：无。
 * 返回：无。
 * 副作用：若相机仍处于打开状态，依次停止采集流、关闭设备、关闭 SDK 库。
 */
DahengCamera::~DahengCamera() {
    Close();
}

/**
 * @brief 打开大恒相机并按配置设置曝光、增益、分辨率、白平衡等参数。
 *
 * 执行流程：
 *   1. 先确保旧连接已关闭（重复 Open 安全）。
 *   2. 初始化 GX SDK 库（GXInitLib）。
 *   3. 枚举所有在线设备（GXUpdateAllDeviceList）。
 *   4. 按 options.deviceIndex（从 1 开始）打开指定相机。
 *   5. 依次设置采集模式（Continuous）、触发模式（Off）、分辨率、
 *      曝光时间、增益、白平衡。
 *   6. 读取并缓存 PixelColorFilter，用于后续 Bayer 解码。
 *   7. 获取数据流句柄及 Payload 大小，预分配 RGB 中间缓冲。
 *   8. 开启采集流（GXStreamOn），标记 opened = true。
 *
 * 若上述任一步失败，通过 RAII 式 lambda failAndClose 回滚所有已分配句柄。
 *
 * @param options  相机配置（设备索引、分辨率、曝光、增益等）。
 * @param error    可选输出参数，失败时写入人类可读错误信息。
 * @return         true 表示相机已打开并正常推流；false 表示失败。
 *
 * 副作用：
 *   - 初始化并持有大恒 SDK 库句柄（需在退出前调用 Close）。
 *   - 分配 rgbBuffer 大小 = payloadSize * 3（宽度 x 高度 x 3）。
 *   - 若编译时未启用大恒支持，直接返回 false。
 */
bool DahengCamera::Open(const DahengCameraOptions& options, std::string* error) {
    Close();

#if !RADAR26_WITH_DAHENG
    if (error != nullptr) {
        *error = "Radar_26 built without Daheng SDK support. Reconfigure with -DRADAR26_WITH_DAHENG=ON";
    }
    (void)options;
    return false;
#else
    impl_->options = options;

    // ——— 第 1 步：初始化 SDK 库 ———
    GX_STATUS status = GXInitLib();
    if (status != GX_STATUS_SUCCESS) {
        if (error != nullptr) {
            *error = "GXInitLib failed: " + GxErrorText(status);
        }
        return false;
    }

    // ——— 第 2 步：枚举设备列表（超时 1000 ms） ———
    uint32_t deviceNum = 0;
    status = GXUpdateAllDeviceList(&deviceNum, 1000);
    if (status != GX_STATUS_SUCCESS) {
        if (error != nullptr) {
            *error = "GXUpdateAllDeviceList failed: " + GxErrorText(status);
        }
        GXCloseLib();
        return false;
    }

    // ——— 第 3 步：按用户指定索引打开设备 ———
    const int openIndex = std::max(1, options.deviceIndex);
    if (deviceNum < static_cast<uint32_t>(openIndex)) {
        if (error != nullptr) {
            std::ostringstream oss;
            oss << "requested Daheng index " << openIndex << " but only " << deviceNum << " camera(s) found";
            *error = oss.str();
        }
        GXCloseLib();
        return false;
    }

    status = GXOpenDeviceByIndex(static_cast<uint32_t>(openIndex), &impl_->device);
    if (status != GX_STATUS_SUCCESS) {
        if (error != nullptr) {
            *error = "GXOpenDeviceByIndex failed: " + GxErrorText(status);
        }
        GXCloseLib();
        return false;
    }

    /**
     * failAndClose lambda：任意一步配置失败时统一关闭相机并清理句柄，
     * 避免设备/库句柄泄漏。传入错误信息字符串，可选输出到 error。
     */
    auto failAndClose = [&](const std::string& msg) {
        if (error != nullptr) {
            *error = msg;
        }
        Close();
        return false;
    };

    // ——— 第 4 步：配置采集模式为连续采集 ———
    status = GXSetEnumValueByString(impl_->device, "AcquisitionMode", "Continuous");
    if (status != GX_STATUS_SUCCESS) {
        return failAndClose("set AcquisitionMode failed: " + GxErrorText(status));
    }

    // ——— 第 5 步：关闭触发模式（自由运行） ———
    status = GXSetEnumValueByString(impl_->device, "TriggerMode", "Off");
    if (status != GX_STATUS_SUCCESS) {
        return failAndClose("set TriggerMode failed: " + GxErrorText(status));
    }

    // ——— 第 6 步：设置分辨率（非必须，失败仅告警） ———
    if (options.width > 0) {
        status = GXSetIntValue(impl_->device, "Width", options.width);
        if (status != GX_STATUS_SUCCESS && error != nullptr) {
            *error = "warning: set Width failed: " + GxErrorText(status);
        }
    }
    if (options.height > 0) {
        status = GXSetIntValue(impl_->device, "Height", options.height);
        if (status != GX_STATUS_SUCCESS && error != nullptr) {
            *error = "warning: set Height failed: " + GxErrorText(status);
        }
    }

    // ——— 第 7 步：设置曝光时间（微秒） ———
    status = GXSetFloatValue(impl_->device, "ExposureTime", options.exposureTimeUs);
    if (status != GX_STATUS_SUCCESS && error != nullptr) {
        *error = "warning: set ExposureTime failed: " + GxErrorText(status);
    }

    // ——— 第 8 步：设置模拟增益 ———
    status = GXSetFloatValue(impl_->device, "Gain", options.gain);
    if (status != GX_STATUS_SUCCESS && error != nullptr) {
        *error = "warning: set Gain failed: " + GxErrorText(status);
    }

    // ——— 第 9 步：设置自动白平衡 ———
    status = GXSetEnumValueByString(impl_->device, "BalanceWhiteAuto",
                                    options.autoWhiteBalance ? "Continuous" : "Off");
    if (status != GX_STATUS_SUCCESS && error != nullptr) {
        *error = "warning: set BalanceWhiteAuto failed: " + GxErrorText(status);
    }

    // ——— 第 10 步：读取并缓存 Bayer 颜色滤镜类型 ———
    GX_ENUM_VALUE enumValue{};
    status = GXGetEnumValue(impl_->device, "PixelColorFilter", &enumValue);
    if (status != GX_STATUS_SUCCESS) {
        return failAndClose("GXGetEnumValue(PixelColorFilter) failed: " + GxErrorText(status));
    }
    impl_->colorFilter = enumValue.stCurValue.nCurValue;

    // ——— 第 11 步：获取数据流句柄（stride = 1） ———
    status = GXGetDataStreamHandleFromDev(impl_->device, 1, &impl_->dataStream);
    if (status != GX_STATUS_SUCCESS) {
        return failAndClose("GXGetDataStreamHandleFromDev failed: " + GxErrorText(status));
    }

    // ——— 第 12 步：获取 Payload 大小并预分配 RGB 缓冲区 ———
    uint32_t payloadSize = 0;
    status = GXGetPayLoadSize(impl_->dataStream, &payloadSize);
    if (status != GX_STATUS_SUCCESS || payloadSize == 0) {
        return failAndClose("GXGetPayLoadSize failed: " + GxErrorText(status));
    }
    impl_->rgbBuffer.assign(static_cast<std::size_t>(payloadSize) * 3U, 0U);

    // ——— 第 13 步：开启采集流 ———
    status = GXStreamOn(impl_->device);
    if (status != GX_STATUS_SUCCESS) {
        return failAndClose("GXStreamOn failed: " + GxErrorText(status));
    }

    impl_->opened = true;
    return true;
#endif
}

/**
 * @brief 从相机读取一帧并转换为 BGR 格式的 cv::Mat。
 *
 * 内部流程：
 *   1. 调用 GXDQBuf 从相机驱动队列取出一帧原始 Bayer 图像。
 *   2. 用 DxRaw8toRGB24 将 Bayer 数据插值为 24 位 RGB。
 *   3. cv::cvtColor RGB -> BGR 写入输出参数。
 *   4. 若配置 flipVertical，对结果做垂直翻转。
 *   5. 调用 GXQBuf 将帧缓冲区归还驱动队列。
 *
 * @param bgrFrame  输出参数，指向一个 cv::Mat 对象，成功时包含 BGR 图像。
 *                  若传入 nullptr 则直接返回 false。
 * @param error     可选输出参数，失败时写入错误描述。
 * @return          true 表示成功获取并转换一帧；false 表示失败。
 *
 * 副作用：
 *   - 修改 impl_->rgbBuffer 的内容（每帧覆写）。
 *   - bgrFrame 的内存可能被重新分配（cv::flip / cv::cvtColor 的结果）。
 *   - 必须调用 GXQBuf 归还帧缓冲，否则相机队列耗尽后会丢帧/超时。
 */
bool DahengCamera::Read(cv::Mat* bgrFrame, std::string* error) {
    if (bgrFrame == nullptr) {
        if (error != nullptr) {
            *error = "output frame pointer is null";
        }
        return false;
    }

#if !RADAR26_WITH_DAHENG
    if (error != nullptr) {
        *error = "Daheng support disabled at compile time";
    }
    return false;
#else
    if (!impl_->opened || impl_->device == nullptr) {
        if (error != nullptr) {
            *error = "Daheng camera not opened";
        }
        return false;
    }

    // ——— 从驱动队列取出一帧（超时 1000 ms） ———
    PGX_FRAME_BUFFER frame = nullptr;
    GX_STATUS status = GXDQBuf(impl_->device, &frame, 1000);
    if (status != GX_STATUS_SUCCESS) {
        if (error != nullptr) {
            *error = "GXDQBuf failed: " + GxErrorText(status);
        }
        return false;
    }

    /**
     * queueBack lambda：确保无论后续处理成功与否，帧缓冲区都被归还驱动队列。
     * 不归还会导致驱动侧 buffer 耗尽，后续 GXDQBuf 永久超时。
     */
    auto queueBack = [&]() {
        if (frame != nullptr) {
            (void)GXQBuf(impl_->device, frame);
        }
    };

    // ——— 校验帧状态 ———
    if (frame == nullptr || frame->nStatus != GX_FRAME_STATUS_SUCCESS) {
        queueBack();
        if (error != nullptr) {
            *error = "invalid Daheng frame status";
        }
        return false;
    }

    // ——— 仅支持标准 Bayer 8-bit 格式 ———
    const int pixelFormat = frame->nPixelFormat;
    if (pixelFormat != GX_PIXEL_FORMAT_BAYER_GR8 && pixelFormat != GX_PIXEL_FORMAT_BAYER_RG8 &&
        pixelFormat != GX_PIXEL_FORMAT_BAYER_GB8 && pixelFormat != GX_PIXEL_FORMAT_BAYER_BG8) {
        queueBack();
        if (error != nullptr) {
            std::ostringstream oss;
            oss << "unsupported Daheng pixel format: " << pixelFormat;
            *error = oss.str();
        }
        return false;
    }

    // ——— DxRaw8toRGB24：Bayer 8-bit -> RGB 24-bit（采用邻域插值） ———
    VxInt32 dxStatus = DxRaw8toRGB24(frame->pImgBuf, impl_->rgbBuffer.data(), static_cast<VxUint32>(frame->nWidth),
                                     static_cast<VxUint32>(frame->nHeight), RAW2RGB_NEIGHBOUR,
                                     static_cast<DX_PIXEL_COLOR_FILTER>(impl_->colorFilter), false);
    if (dxStatus != DX_OK) {
        queueBack();
        if (error != nullptr) {
            std::ostringstream oss;
            oss << "DxRaw8toRGB24 failed: " << dxStatus;
            *error = oss.str();
        }
        return false;
    }

    // ——— RGB -> BGR，可选垂直翻转 ———
    // 先将 RGB 数据包装为 cv::Mat（不拷贝数据，直接引用 rgbBuffer），
    // 再转换为 BGR 输出给 OpenCV 后续处理管线。
    cv::Mat rgb(frame->nHeight, frame->nWidth, CV_8UC3, impl_->rgbBuffer.data());
    if (impl_->options.flipVertical) {
        cv::flip(rgb, *bgrFrame, 0);                // 垂直翻转后输出到 bgrFrame（会分配新内存）
        cv::cvtColor(*bgrFrame, *bgrFrame, cv::COLOR_RGB2BGR);
    } else {
        cv::cvtColor(rgb, *bgrFrame, cv::COLOR_RGB2BGR);
    }

    // ——— 归还帧缓冲区 ———
    queueBack();
    return true;
#endif
}

/**
 * @brief 关闭大恒相机，释放所有 SDK 资源。
 *
 * 按顺序执行：
 *   1. 若已打开，调用 GXStreamOff 停止采集流。
 *   2. 调用 GXCloseDevice 关闭设备句柄。
 *   3. 调用 GXCloseLib 关闭 SDK 库。
 *   4. 清空中间缓冲区与状态标志。
 *
 * 该函数设计为幂等（idempotent），重复调用安全。
 *
 * 参数：无。
 * 返回：无。
 * 副作用：
 *   - 释放大恒设备/流/SDK 句柄。
 *   - rgbBuffer 被清空。
 *   - opened 置为 false。
 */
void DahengCamera::Close() {
#if RADAR26_WITH_DAHENG
    if (impl_->device != nullptr) {
        if (impl_->opened) {
            (void)GXStreamOff(impl_->device);
        }
        (void)GXCloseDevice(impl_->device);
        impl_->device = nullptr;
    }
    if (impl_->opened) {
        (void)GXCloseLib();
    }
    impl_->dataStream = nullptr;
    impl_->rgbBuffer.clear();
#endif
    impl_->opened = false;
}

/**
 * @brief 查询相机是否处于打开状态。
 *
 * 参数：无。
 * @return true 表示相机已打开且采集流正在运行；false 表示未打开或已关闭。
 * 副作用：无（const 方法，不修改任何状态）。
 */
bool DahengCamera::IsOpen() const {
    return impl_->opened;
}

}  // namespace radar26
