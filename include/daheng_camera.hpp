#pragma once

#include <opencv2/core.hpp>

#include <memory>
#include <string>

namespace radar26 {

struct DahengCameraOptions {
    int deviceIndex = 1;
    int width = 1920;
    int height = 1080;
    double exposureTimeUs = 5000.0;
    double gain = 4.0;
    bool autoWhiteBalance = true;
    bool flipVertical = false;
};

class DahengCamera {
public:
    DahengCamera();
    ~DahengCamera();

    DahengCamera(const DahengCamera&) = delete;
    DahengCamera& operator=(const DahengCamera&) = delete;

    bool Open(const DahengCameraOptions& options, std::string* error);
    bool Read(cv::Mat* bgrFrame, std::string* error);
    void Close();
    bool IsOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace radar26