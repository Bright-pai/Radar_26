#pragma once

#include "radar_types.hpp"
#include "serial_protocol.hpp"
#include "serial_port.hpp"
#include "serial_monitor.hpp"
#include "tcp_client.hpp"
#include "tracker_filter.hpp"
#include "trt_detector.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>

namespace radar26 {

// RadarApp 负责把相机、推理、目标滤波和串口通信串成完整闭环。
class RadarApp {
public:
    explicit RadarApp(AppConfig config);
    ~RadarApp();

    // 读取标定数据、加载模型并初始化串口和内部状态。
    bool Initialize(std::string* error);
    // 启动主循环，持续采集图像、推理、更新结果并发送串口数据。
    void Run();

private:
    // 打开串口并配置通信参数。
    bool OpenSerial(std::string* error);
    // 启动发送、接收和解析线程。
    void StartSerialThreads();
    // 停止所有串口相关线程。
    void StopSerialThreads();
    // 周期性发送雷达控制和位置信息。
    void SerialSendLoop();
    // 从串口接收原始字节并送入解析队列。
    void SerialReceiveLoop();
    // 解析接收队列中的数据包，并更新内部状态。
    void ParserWorker();

    // 异步帧读取线程
    void FrameGrabLoop();

    // 把地图坐标映射为串口协议所需坐标。
    std::pair<int, int> MapToSerCoords(const std::string& name, float mapX, float mapY) const;
    // 使用单应矩阵将图像点投影到地图坐标。
    bool ProjectPoint(const cv::Mat& transform, const cv::Point2f& cameraPoint, int* mapX, int* mapY) const;

    // 更新发送/接收状态文本，供监视窗口展示。
    void UpdateSerialStatus(SerialStatus* status, uint8_t seq, const std::string& text);
    // 根据解析后的包更新雷达信息位。
    bool UpdateRadarInfoFromParsedPacket(const ParsedPacket& parsed);

    // 建立 TCP 连接并发送 0x00 握手。
    bool OpenTcp(std::string* error);
    // TCP 发送线程：周期性检查触发条件并发送 0x01。
    void TcpSendLoop();
    // 启动 / 停止 TCP Client 接收线程（连接 192.168.12.99:8001~8004）。
    void StartTcpReceivers();
    void StopTcpReceivers();
    // 单端口 TCP Client 读取线程：connect → recv 解帧 → 回调 → 断线重连。
    void TcpReaderLoop(int port);
    // 接收帧回调：端口 8001（0x0A01~0x0A05/0x0201等）。
    void OnTcp8001Frame(const std::vector<uint8_t>& frame);
    // 接收帧回调：端口 8002/8003/8004（0x0A06 密钥）。
    void OnTcpKeyFrame(int level, const std::vector<uint8_t>& frame);

    AppConfig config_;
    CalibrationData calibration_;

    TrtYoloDetector carDetector_;
    TrtYoloDetector armorDetector_;
    std::unique_ptr<TargetFilter> filter_;

    std::unique_ptr<SerialPort> serial_;
    std::unique_ptr<TcpClient> tcp_;           // 触发信号发送 (192.168.10.2:5000)
    SerialMonitor serialMonitor_;
    std::thread sendThread_;
    std::thread recvThread_;
    std::thread tcpThread_;
    std::thread tcpRxThread8001_;              // TCP Client 接收 8001
    std::thread tcpRxThread8002_;              // TCP Client 接收 8002
    std::thread tcpRxThread8003_;              // TCP Client 接收 8003
    std::thread tcpRxThread8004_;              // TCP Client 接收 8004
    std::thread grabThread_;
    std::atomic<bool> running_{false};

    // parse worker queue
    std::deque<std::vector<uint8_t>> parseQueue_;
    std::mutex parseQueueMu_;
    std::condition_variable parseQueueCv_;
    std::vector<std::thread> parserThreads_;
    std::atomic<bool> parserRunning_{false};

    std::map<std::string, std::optional<std::pair<int, int>>> sendLastSerCoords_;

    std::array<std::atomic<int>, 12> latestSerCoordX_{};
    std::array<std::atomic<int>, 12> latestSerCoordY_{};
    std::array<std::atomic<bool>, 12> latestSerCoordValid_{};
    std::array<std::atomic<bool>, 12> latestSerCoordFromDetection_{};  // true=检测到, false=盲区猜测
    std::array<std::string, 12> latestDetectedType_{};

    // 各个收发通道的统计信息，监控窗口直接显示这些内容。
    SerialStatus tx0305_{};   // 发送：位置坐标
    SerialStatus tx0121_{};   // 发送：雷达命令
    SerialStatus tx0301_{};   // 发送：自定义数据
    SerialStatus rx020E_{};   // 接收：雷达信息
    SerialStatus rx0301_{};   // 接收：机器人交互位置
    SerialStatus rx0001_{};   // 接收：比赛状态（阶段+剩余时间）
    SerialStatus rx0003_{};   // 接收：前哨站血量
    SerialStatus tcp01_{};     // 发送：TCP 0x01 触发信号
    SerialStatus tcpRx8001_{}; // 接收：TCP 8001（0x0A01/0201/0202等）
    SerialStatus tcpRx8002_{}; // 接收：TCP 8002（0x0A06 L1密钥）
    SerialStatus tcpRx8003_{}; // 接收：TCP 8003（0x0A06 L2密钥）
    SerialStatus tcpRx8004_{}; // 接收：TCP 8004（0x0A06 L3密钥）

    std::atomic<int> doubleVulnerabilityChance_{-1};
    std::atomic<int> opponentDoubleTriggered_{-1};
    std::atomic<int> encryptLevel_{1};
    std::atomic<int> keyModifiable_{0};
    std::atomic<bool> hasRadarInfo020E_{false};
    std::atomic<bool> canTriggerDoubleNow_{false};
    std::atomic<uint32_t> doubleTriggerEpoch_{0};
    uint32_t lastHandled020EEpoch_ = 0;  // 用于检测新的 0x020E 到达

    // 0x0121 发送状态 — 三种触发独立，各有各的计数器
    bool initialKeySent_ = false;
    std::chrono::steady_clock::time_point lastCrackedSendTime_;
    std::chrono::steady_clock::time_point lastTcpFwdTime_;   // TCP 数据转发 1Hz 限速
    uint8_t doubleTriggerCount_ = 0;   // 双倍易伤累计次数，pwdCmd=0 用
    uint8_t keyModCount_ = 0;          // 修改密钥累计次数，pwdCmd=1 用
    uint8_t crackedKeyCount_ = 0;      // 破解密钥累计次数，pwdCmd=2 用

    std::array<uint8_t, 6> currentKey_{};

    // TCP 触发所需的状态数据（由 ParserWorker 写入，TcpSendLoop 读取）
    std::atomic<uint8_t> latestGameType_{0};
    std::atomic<uint8_t> latestGameProgress_{0};
    std::atomic<uint16_t> latestStageRemainTime_{0};
    std::atomic<uint16_t> latestOutpostHealth_{1500};
    std::atomic<bool> outpostHealthDecreased_{false};  // 血量较上一时刻下降过
    // 一次性触发标记：保证每个触发条件只生效一次
    std::atomic<bool> tcpTrigger1Fired_{false};  // game_type==4 && time<=450 && 前哨站掉血
    std::atomic<bool> tcpTrigger2Fired_{false};  // 工程机器人到位（仅检测时）
    std::atomic<bool> tcpTrigger3Fired_{false};  // 剩余时间<300s

    // ---- TCP 接收数据（TcpServer 回调写入，SerialSendLoop 读取转发）----
    // 0x0A01：对方 6 个机器人坐标，12 个 int16_t 原子（单位 cm）
    std::array<std::atomic<int16_t>, 12> tcpEnemyCoords_{};
    std::atomic<bool> tcpEnemyCoordsValid_{false};
    // 0x0A02~0x0A05：拼成 66 字节转发给 0x0301->0x0206
    std::array<uint8_t, 66> tcp0206Data_{};
    std::atomic<bool> tcp0206SegValid_[4];   // 0=0A02 1=0A03 2=0A04 3=0A05
    std::atomic<bool> tcp0206AllValid_{false};
    // 0x0A06：三级破解密钥（8002→L1, 8003→L2, 8004→L3）
    std::array<uint8_t, 6> tcpCrackedKey_[3];
    std::atomic<bool> tcpCrackedKeyValid_[3];

    // 帧缓冲区（异步读取）
    cv::Mat latestFrame_;
    std::mutex frameMutex_;
    std::atomic<bool> frameReady_{false};
};

}  // namespace radar26