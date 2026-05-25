#pragma once

#include <opencv2/core.hpp>

#include <memory>
#include <string>

namespace radar26 {

// 大恒相机的运行参数，供 Open 时设置设备索引、分辨率和图像方向。
struct DahengCameraOptions {
    int deviceIndex = 1;
    int width = 1920;
    int height = 1080;
    double exposureTimeUs = 5000.0;
    double gain = 4.0;
    bool autoWhiteBalance = true;
    bool flipVertical = false;
};

// 对大恒相机 SDK 的轻量封装，屏蔽底层句柄和初始化细节。
class DahengCamera {
public:
    DahengCamera();
    ~DahengCamera();

    DahengCamera(const DahengCamera&) = delete;
    DahengCamera& operator=(const DahengCamera&) = delete;

    // 打开相机并应用配置参数。
    bool Open(const DahengCameraOptions& options, std::string* error);
    // 读取一帧并输出为 BGR 图像。
    bool Read(cv::Mat* bgrFrame, std::string* error);
    // 关闭相机并释放底层资源。
    void Close();
    // 判断当前相机是否已经打开。
    bool IsOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace radar26