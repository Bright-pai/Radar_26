#pragma once

#include "radar_types.hpp"

#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace radar26 {

class TrtYoloDetector {
public:
    TrtYoloDetector();
    ~TrtYoloDetector();

    TrtYoloDetector(const TrtYoloDetector&) = delete;
    TrtYoloDetector& operator=(const TrtYoloDetector&) = delete;

    bool Load(const std::string& enginePath, const std::vector<std::string>& classNames, int inputWidth,
              int inputHeight, std::string* error);

    bool IsReady() const;

    bool Infer(const cv::Mat& bgr, float confThresh, float iouThresh, int maxDet, std::vector<Detection>* detections,
               std::string* error);

    bool InferBatch(const std::vector<cv::Mat>& bgrBatch, float confThresh, float iouThresh, int maxDet,
                    std::vector<std::vector<Detection>>* detectionsBatch, std::string* error);

    bool SupportsDynamicBatch() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace radar26
