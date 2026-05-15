#pragma once

#include "radar_types.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <string>
#include <vector>

namespace radar26 {

class SerialMonitor {
public:
    SerialMonitor() = default;
    ~SerialMonitor();

    SerialMonitor(const SerialMonitor&) = delete;
    SerialMonitor& operator=(const SerialMonitor&) = delete;

    void Open();
    void Close();
    bool IsOpen() const { return open_; }

    void Update(const SerialStatus& tx0305,
                const SerialStatus& tx0121,
                const SerialStatus& tx0301,
                const SerialStatus& rx020E,
                const SerialStatus& rx0301);

private:
    static std::string GetText(const SerialStatus& s);
    std::vector<std::string> WrapLines(const std::string& text, int maxWidth);

    static constexpr int kWidth = 1280;
    static constexpr int kMinHeight = 720;
    static constexpr char kWindowName[] = "Serial Monitor";

    bool open_ = false;
};

}  // namespace radar26
