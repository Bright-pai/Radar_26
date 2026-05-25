#pragma once

#include "radar_types.hpp"

#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace radar26 {

// TensorRT YOLO 检测器封装，负责模型加载、推理和后处理。
class TrtYoloDetector {
public:
    TrtYoloDetector();
    ~TrtYoloDetector();

    TrtYoloDetector(const TrtYoloDetector&) = delete;
    TrtYoloDetector& operator=(const TrtYoloDetector&) = delete;

    // 加载 engine，并绑定类别名和输入尺寸。
    bool Load(const std::string& enginePath, const std::vector<std::string>& classNames, int inputWidth,
              int inputHeight, std::string* error);

    // 检查模型是否已经准备好可以推理。
    bool IsReady() const;

    // 对单张图像执行推理并输出检测框。
    bool Infer(const cv::Mat& bgr, float confThresh, float iouThresh, int maxDet, std::vector<Detection>* detections,
               std::string* error);

    // 对一组图像执行批量推理。
    bool InferBatch(const std::vector<cv::Mat>& bgrBatch, float confThresh, float iouThresh, int maxDet,
                    std::vector<std::vector<Detection>>* detectionsBatch, std::string* error);

    // 判断当前 engine 是否支持动态 batch。
    bool SupportsDynamicBatch() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace radar26
