#include "serial_monitor.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace radar26 {

SerialMonitor::~SerialMonitor() {
    Close();
}

void SerialMonitor::Open() {
    if (open_) return;
    cv::namedWindow(kWindowName, cv::WINDOW_NORMAL);
    cv::resizeWindow(kWindowName, kWidth, kMinHeight);
    open_ = true;
}

void SerialMonitor::Close() {
    if (!open_) return;
    cv::destroyWindow(kWindowName);
    open_ = false;
}

void SerialMonitor::Update(const SerialStatus& tx0305,
                           const SerialStatus& tx0121,
                           const SerialStatus& tx0301,
                           const SerialStatus& rx020E,
                           const SerialStatus& rx0301) {
    if (!open_) return;

    const int font = cv::FONT_HERSHEY_SIMPLEX;
    const double scale = 0.45;
    const int thickness = 1;
    const cv::Scalar bgColor(30, 30, 30);
    const int lineHeight = 20;
    const int margin = 10;
    const int maxTextWidth = kWidth - margin * 2;

    // estimate required canvas height
    int totalLines = 0;
    for (const auto* status : {&tx0305, &tx0121, &tx0301, &rx020E, &rx0301}) {
        totalLines += 2; // header + content, each at least 1 line
        std::string text = GetText(*status);
        if (!text.empty()) {
            auto wrapped = WrapLines(text, maxTextWidth);
            totalLines += static_cast<int>(wrapped.size());
        }
    }
    totalLines += 5; // spacing between channels

    const int canvasHeight = std::max(kMinHeight, margin * 2 + totalLines * lineHeight);
    cv::Mat canvas(canvasHeight, kWidth, CV_8UC3, bgColor);

    int y = margin;

    auto drawLine = [&](const std::string& text, const cv::Scalar& color) {
        cv::putText(canvas, text, cv::Point(margin, y), font, scale, color, thickness);
        y += lineHeight;
    };

    auto drawHeader = [&](const std::string& label, const cv::Scalar& color,
                          const SerialStatus& status) {
        std::ostringstream oss;
        oss << "=== " << label << " ===  count=" << status.count.load(std::memory_order_relaxed)
            << "  lastSeq=" << status.lastSeq.load(std::memory_order_relaxed) << " ===";
        cv::putText(canvas, oss.str(), cv::Point(margin, y), font, scale + 0.05, color, thickness + 1);
        y += lineHeight + 2;
    };

    auto drawChannel = [&](const std::string& label, const cv::Scalar& color,
                           const SerialStatus& status) {
        drawHeader(label, color, status);
        std::string text = GetText(status);
        if (text.empty()) text = "N/A";
        auto lines = WrapLines(text, maxTextWidth);
        for (const auto& line : lines) {
            drawLine(line, cv::Scalar(200, 200, 200));
        }
        y += 4; // spacing
    };

    drawChannel("TX 0x0305", cv::Scalar(0, 255, 255), tx0305);
    drawChannel("TX 0x0121", cv::Scalar(255, 200, 0), tx0121);
    drawChannel("TX 0x0301", cv::Scalar(0, 255, 0), tx0301);
    drawChannel("RX 0x020E", cv::Scalar(255, 150, 150), rx020E);
    drawChannel("RX 0x0301", cv::Scalar(150, 255, 150), rx0301);

    cv::imshow(kWindowName, canvas);
}

std::string SerialMonitor::GetText(const SerialStatus& s) {
    auto sp = std::atomic_load_explicit(&s.lastText, std::memory_order_acquire);
    return sp ? *sp : std::string();
}

std::vector<std::string> SerialMonitor::WrapLines(const std::string& text, int maxWidth) {
    std::vector<std::string> lines;
    if (text.empty()) return lines;

    const int font = cv::FONT_HERSHEY_SIMPLEX;
    const double scale = 0.45;
    const int thickness = 1;

    // approximate chars per line: OpenCV FONT_HERSHEY_SIMPLEX at scale 0.45
    // each char ~11px wide, so ~maxWidth/11 chars per line
    const int charsPerLine = std::max(40, maxWidth / 11);
    if (static_cast<int>(text.size()) <= charsPerLine) {
        lines.push_back(text);
        return lines;
    }

    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t len = std::min(static_cast<std::size_t>(charsPerLine), text.size() - pos);
        // try to break on a space or pipe delimiter
        if (pos + len < text.size()) {
            std::size_t brk = text.rfind(' ', pos + len);
            if (brk == std::string::npos || brk <= pos) {
                brk = text.rfind('|', pos + len);
            }
            if (brk != std::string::npos && brk > pos) {
                len = brk - pos;
            }
        }
        lines.push_back(text.substr(pos, len));
        pos += len;
        // skip leading space/pipe on next line
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '|')) ++pos;
    }
    return lines;
}

}  // namespace radar26
