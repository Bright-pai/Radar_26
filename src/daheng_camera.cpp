#include "daheng_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <vector>

#if !defined(RADAR26_WITH_DAHENG)
#define RADAR26_WITH_DAHENG 0
#endif

#if RADAR26_WITH_DAHENG
#include "DxImageProc.h"
#include "GxIAPI.h"
#include "GxPixelFormat.h"
#endif

namespace radar26 {
namespace {

std::string GxErrorText(int code) {
    std::ostringstream oss;
    oss << "GX_STATUS=" << code;
    return oss.str();
}

}  // namespace

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

DahengCamera::DahengCamera() : impl_(std::make_unique<Impl>()) {}

DahengCamera::~DahengCamera() {
    Close();
}

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

    GX_STATUS status = GXInitLib();
    if (status != GX_STATUS_SUCCESS) {
        if (error != nullptr) {
            *error = "GXInitLib failed: " + GxErrorText(status);
        }
        return false;
    }

    uint32_t deviceNum = 0;
    status = GXUpdateAllDeviceList(&deviceNum, 1000);
    if (status != GX_STATUS_SUCCESS) {
        if (error != nullptr) {
            *error = "GXUpdateAllDeviceList failed: " + GxErrorText(status);
        }
        GXCloseLib();
        return false;
    }

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

    auto failAndClose = [&](const std::string& msg) {
        if (error != nullptr) {
            *error = msg;
        }
        Close();
        return false;
    };

    status = GXSetEnumValueByString(impl_->device, "AcquisitionMode", "Continuous");
    if (status != GX_STATUS_SUCCESS) {
        return failAndClose("set AcquisitionMode failed: " + GxErrorText(status));
    }

    status = GXSetEnumValueByString(impl_->device, "TriggerMode", "Off");
    if (status != GX_STATUS_SUCCESS) {
        return failAndClose("set TriggerMode failed: " + GxErrorText(status));
    }

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

    status = GXSetFloatValue(impl_->device, "ExposureTime", options.exposureTimeUs);
    if (status != GX_STATUS_SUCCESS && error != nullptr) {
        *error = "warning: set ExposureTime failed: " + GxErrorText(status);
    }

    status = GXSetFloatValue(impl_->device, "Gain", options.gain);
    if (status != GX_STATUS_SUCCESS && error != nullptr) {
        *error = "warning: set Gain failed: " + GxErrorText(status);
    }

    status = GXSetEnumValueByString(impl_->device, "BalanceWhiteAuto",
                                    options.autoWhiteBalance ? "Continuous" : "Off");
    if (status != GX_STATUS_SUCCESS && error != nullptr) {
        *error = "warning: set BalanceWhiteAuto failed: " + GxErrorText(status);
    }

    GX_ENUM_VALUE enumValue{};
    status = GXGetEnumValue(impl_->device, "PixelColorFilter", &enumValue);
    if (status != GX_STATUS_SUCCESS) {
        return failAndClose("GXGetEnumValue(PixelColorFilter) failed: " + GxErrorText(status));
    }
    impl_->colorFilter = enumValue.stCurValue.nCurValue;

    status = GXGetDataStreamHandleFromDev(impl_->device, 1, &impl_->dataStream);
    if (status != GX_STATUS_SUCCESS) {
        return failAndClose("GXGetDataStreamHandleFromDev failed: " + GxErrorText(status));
    }

    uint32_t payloadSize = 0;
    status = GXGetPayLoadSize(impl_->dataStream, &payloadSize);
    if (status != GX_STATUS_SUCCESS || payloadSize == 0) {
        return failAndClose("GXGetPayLoadSize failed: " + GxErrorText(status));
    }
    impl_->rgbBuffer.assign(static_cast<std::size_t>(payloadSize) * 3U, 0U);

    status = GXStreamOn(impl_->device);
    if (status != GX_STATUS_SUCCESS) {
        return failAndClose("GXStreamOn failed: " + GxErrorText(status));
    }

    impl_->opened = true;
    return true;
#endif
}

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

    PGX_FRAME_BUFFER frame = nullptr;
    GX_STATUS status = GXDQBuf(impl_->device, &frame, 1000);
    if (status != GX_STATUS_SUCCESS) {
        if (error != nullptr) {
            *error = "GXDQBuf failed: " + GxErrorText(status);
        }
        return false;
    }

    auto queueBack = [&]() {
        if (frame != nullptr) {
            (void)GXQBuf(impl_->device, frame);
        }
    };

    if (frame == nullptr || frame->nStatus != GX_FRAME_STATUS_SUCCESS) {
        queueBack();
        if (error != nullptr) {
            *error = "invalid Daheng frame status";
        }
        return false;
    }

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

    // 避免 clone：直接使用内部 rgbBuffer 构造 Mat，然后 cvtColor 到目标 Mat
    cv::Mat rgb(frame->nHeight, frame->nWidth, CV_8UC3, impl_->rgbBuffer.data());
    if (impl_->options.flipVertical) {
        cv::flip(rgb, *bgrFrame, 0);                // 直接输出到 bgrFrame（会分配内存）
        cv::cvtColor(*bgrFrame, *bgrFrame, cv::COLOR_RGB2BGR);
    } else {
        cv::cvtColor(rgb, *bgrFrame, cv::COLOR_RGB2BGR);
    }

    queueBack();
    return true;
#endif
}

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

bool DahengCamera::IsOpen() const {
    return impl_->opened;
}

}  // namespace radar26