#pragma once

#include "radar_types.hpp"
#include "serial_protocol.hpp"
#include "serial_port.hpp"
#include "serial_monitor.hpp"
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

class RadarApp {
public:
    explicit RadarApp(AppConfig config);
    ~RadarApp();

    bool Initialize(std::string* error);
    void Run();

private:
    void InitializeGuessTable();
    bool OpenSerial(std::string* error);
    void StartSerialThreads();
    void StopSerialThreads();
    void SerialSendLoop();
    void SerialReceiveLoop();
    void ParserWorker();

    // 异步帧读取线程
    void FrameGrabLoop();

    std::pair<int, int> MapToSerCoords(const std::string& name, float mapX, float mapY) const;
    bool ProjectPoint(const cv::Mat& transform, const cv::Point2f& cameraPoint, int* mapX, int* mapY) const;

    void UpdateSerialStatus(SerialStatus* status, uint8_t seq, const std::string& text);
    bool UpdateRadarInfoFromParsedPacket(const ParsedPacket& parsed);

    AppConfig config_;
    CalibrationData calibration_;

    TrtYoloDetector carDetector_;
    TrtYoloDetector armorDetector_;
    std::unique_ptr<TargetFilter> filter_;

    std::unique_ptr<SerialPort> serial_;
    SerialMonitor serialMonitor_;
    std::thread sendThread_;
    std::thread recvThread_;
    std::thread grabThread_;
    std::atomic<bool> running_{false};

    // parse worker queue
    std::deque<std::vector<uint8_t>> parseQueue_;
    std::mutex parseQueueMu_;
    std::condition_variable parseQueueCv_;
    std::vector<std::thread> parserThreads_;
    std::atomic<bool> parserRunning_{false};

    std::map<std::string, std::optional<std::pair<int, int>>> sendLastSerCoords_;
    std::map<std::string, std::array<cv::Point2f, 2>> guessTable_;
    std::map<std::string, int> guessIndex_;
    std::map<std::string, std::chrono::steady_clock::time_point> guessLastSwitch_;

    std::array<std::atomic<int>, 12> latestSerCoordX_{};
    std::array<std::atomic<int>, 12> latestSerCoordY_{};
    std::array<std::atomic<bool>, 12> latestSerCoordValid_{};
    std::array<std::string, 12> latestDetectedType_{};

    SerialStatus tx0305_{};
    SerialStatus tx0121_{};
    SerialStatus tx0301_{};
    SerialStatus rx020E_{};
    SerialStatus rx0301_{};

    std::atomic<int> doubleVulnerabilityChance_{-1};
    std::atomic<int> opponentDoubleTriggered_{-1};
    std::atomic<int> encryptLevel_{1};
    std::atomic<int> keyModifiable_{0};
    std::atomic<bool> hasRadarInfo020E_{false};
    std::atomic<bool> canTriggerDoubleNow_{false};
    std::atomic<uint32_t> doubleTriggerEpoch_{0};

    std::array<uint8_t, 6> currentKey_{};
    uint8_t chanceCounter_ = 1;

    // 帧缓冲区（异步读取）
    cv::Mat latestFrame_;
    std::mutex frameMutex_;
    std::atomic<bool> frameReady_{false};
};

}  // namespace radar26