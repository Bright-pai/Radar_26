#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace radar26 {

// 这里定义工程内部共用的基础类型和配置结构体，其他模块都通过这些类型传递数据。
enum class Team {
    Red,
    Blue,
};

// 单个检测结果，包含类别、置信度以及目标在图像中的矩形框。
struct Detection {
    int classId = -1;
    std::string className;
    float confidence = 0.0F;
    cv::Rect2f box;
};

// 相机输入相关配置，既支持大恒相机，也支持视频文件和测试图像。
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

// 串口通信配置，控制串口是否启用、端口号、波特率和发送周期。
struct SerialConfig {
    bool enable = true;
    std::string port = "/dev/ttyUSB0";
    int baudrate = 115200;
    int sendPeriodMs = 200;
};

// TCP 通信配置，用于向外部系统发送触发信号（0x00 握手，0x01 触发）。
struct TcpConfig {
    bool enable = false;
    std::string ip = "127.0.0.1";
    int port = 8080;
};

// TensorRT 模型配置，分别描述车辆模型和装甲板模型。
struct ModelConfig {
    std::string carEnginePath;
    std::string armorEnginePath;
    int inputWidth = 640;
    int inputHeight = 640;
    std::vector<std::string> carClassNames;
    std::vector<std::string> armorClassNames;
};

// 检测阈值和后处理参数，主要用于控制置信度、NMS 和滤波超时。
struct DetectionConfig {
    float carConf = 0.1F;
    float carIou = 0.5F;
    int carMaxDet = 14;

    float armorConf = 0.4F;
    float armorIou = 0.2F;
    int armorMaxDet = 14;

    double filterTimeoutSec = 2.0;
};

// 整个工程运行所需的总配置，主程序会把 YAML 内容读取到这个结构体里。
struct AppConfig {
    Team team = Team::Red;
    CameraConfig camera;
    SerialConfig serial;
    TcpConfig tcp;
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
};

// 标定阶段读取到的单应矩阵、地图图像和掩码图像都保存在这里。
struct CalibrationData {
    cv::Mat MGround;
    cv::Mat MHeightR;
    cv::Mat MHeightG;

    cv::Mat mapImage;
    cv::Mat maskImage;

    int maskWidth = 0;
    int maskHeight = 0;
};

// 串口状态统计，用于界面监控当前收发次数、序号和最近一条文本内容。
struct SerialStatus {
    std::atomic<std::uint64_t> count{0};
    std::atomic<int> lastSeq{-1};
    std::shared_ptr<const std::string> lastText{std::make_shared<const std::string>("N/A")};
};

// 0x0001：比赛状态包，表示当前比赛阶段以及剩余时间。
struct GameStatus {
    uint8_t game_type = 0;
    uint8_t game_progress = 0;
    uint16_t stage_remain_time = 0;
};

// 0x0003：前哨站血量包，只保留核心血量字段。
struct OutpostHealth {
    uint16_t health = 0;
};

// 雷达信息位状态，记录机会、敌方触发、加密等级和密钥是否可改。
struct RadarInfoState {
    int doubleVulnerabilityChance = -1;
    int opponentDoubleTriggered = -1;
    int encryptLevel = 1;
    int keyModifiable = 0;
};

// 0x0A01：TCP 接收的对方 6 个机器人坐标，12 个 int16_t（小端，单位 cm）。
// 顺序：英雄→工程→步兵3→步兵4→空中→哨兵
struct TcpEnemyPositions {
    int16_t hero_x = 0, hero_y = 0;
    int16_t engineer_x = 0, engineer_y = 0;
    int16_t infantry3_x = 0, infantry3_y = 0;
    int16_t infantry4_x = 0, infantry4_y = 0;
    int16_t aerial_x = 0, aerial_y = 0;
    int16_t sentry_x = 0, sentry_y = 0;
};

// 机器人坐标列表，按协议顺序存放 12 个机器人、每个机器人两个坐标值。
using RobotCoordinates = std::vector<uint16_t>;

constexpr int kRobotCount = 12;
constexpr int kCoordValueCount = 24;

}  // namespace radar26
