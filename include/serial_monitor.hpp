#pragma once

#include "radar_types.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <string>
#include <vector>

namespace radar26 {

// 串口监视窗口，把收发状态整理成可视化文本界面。
class SerialMonitor {
public:
    SerialMonitor() = default;
    ~SerialMonitor();

    SerialMonitor(const SerialMonitor&) = delete;
    SerialMonitor& operator=(const SerialMonitor&) = delete;

    // 创建监视窗口。
    void Open();
    // 关闭监视窗口。
    void Close();
    bool IsOpen() const { return open_; }

    // 把各个收发通道的状态刷新到窗口里。
    void Update(const SerialStatus& tx0305,
                const SerialStatus& tx0121,
                const SerialStatus& tx0301,
                const SerialStatus& rx020E,
                const SerialStatus& rx0301,
                const SerialStatus& rx0001,
                const SerialStatus& rx0003,
                const SerialStatus& tcp01,
                const SerialStatus& tcpRx8001,
                const SerialStatus& tcpRx8002,
                const SerialStatus& tcpRx8003,
                const SerialStatus& tcpRx8004);

private:
    static std::string GetText(const SerialStatus& s);
    std::vector<std::string> WrapLines(const std::string& text, int maxWidth);

    static constexpr int kWidth = 1280;
    static constexpr int kMinHeight = 720;
    static constexpr char kWindowName[] = "Serial Monitor";

    bool open_ = false;
};

}  // namespace radar26
