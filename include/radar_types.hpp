#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace radar26 {

enum class Team {
    Red,
    Blue,
};

struct Detection {
    int classId = -1;
    std::string className;
    float confidence = 0.0F;
    cv::Rect2f box;
};

struct CameraConfig {
    std::string mode = "daheng";  // daheng | video_file | test
    std::string videoPath;
    std::string snapshotPath;
    int deviceId = 0;
    int dahengDeviceIndex = 1;
    int width = 1920;
    int height = 1080;
    double exposureTime = 18000.0;
    double gain = 14.0;
    bool dahengAutoWhiteBalance = true;
    bool dahengFlipVertical = false;
};

struct SerialConfig {
    bool enable = true;
    std::string port = "/dev/ttyUSB0";
    int baudrate = 115200;
    int sendPeriodMs = 200;
};

struct ModelConfig {
    std::string carEnginePath;
    std::string armorEnginePath;
    int inputWidth = 640;
    int inputHeight = 640;
    std::vector<std::string> carClassNames;
    std::vector<std::string> armorClassNames;
};

struct DetectionConfig {
    float carConf = 0.1F;
    float carIou = 0.5F;
    int carMaxDet = 14;

    float armorConf = 0.4F;
    float armorIou = 0.2F;
    int armorMaxDet = 14;

    double filterTimeoutSec = 2.0;
};

struct AppConfig {
    Team team = Team::Red;
    CameraConfig camera;
    SerialConfig serial;
    ModelConfig model;
    DetectionConfig detection;

    std::string calibrationRedPath;
    std::string calibrationBluePath;
    std::string calibrationMapImagePath;
    std::string calibrationRedMaskPath;
    std::string calibrationBlueMaskPath;
    std::string refereeLogPath;

    bool debug = true;
    bool debugRequestKeyUpdate = false;
    bool debugRequestEnemyKey = false;
    bool showUi = true;
    int opencvThreads = 0;
    double guessSwitchIntervalSec = 3.0;
};

struct CalibrationData {
    cv::Mat MGround;
    cv::Mat MHeightR;
    cv::Mat MHeightG;

    cv::Mat mapImage;
    cv::Mat maskImage;

    int maskWidth = 0;
    int maskHeight = 0;
};

struct SerialStatus {
    std::atomic<std::uint64_t> count{0};
    std::atomic<int> lastSeq{-1};
    std::shared_ptr<const std::string> lastText{std::make_shared<const std::string>("N/A")};
};

struct RadarInfoState {
    int doubleVulnerabilityChance = -1;
    int opponentDoubleTriggered = -1;
    int encryptLevel = 1;
    int keyModifiable = 0;
};

using RobotCoordinates = std::vector<uint16_t>;

constexpr int kRobotCount = 12;
constexpr int kCoordValueCount = 24;

}  // namespace radar26
