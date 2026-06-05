#include "serial_monitor.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace radar26 {

//==============================================================================
// SerialMonitor::~SerialMonitor()
// 功能：析构函数，确保监视窗口被正确销毁。
// 参数：无。
// 返回：无。
// 副作用：如果窗口处于打开状态，则调用 Close() 销毁 OpenCV 窗口并释放资源。
//==============================================================================
SerialMonitor::~SerialMonitor() {
    Close();
}

//==============================================================================
// SerialMonitor::Open()
// 功能：创建一个固定尺寸(1280x720)的 OpenCV 监视窗口，窗口名为 "Serial Monitor"。
//        窗口使用 WINDOW_NORMAL 模式，允许用户自由缩放。
// 参数：无。
// 返回：无。
// 副作用：创建一个新的 OpenCV highgui 窗口；设置 open_=true。
//         幂等操作：如果窗口已打开则直接返回，不会重复创建。
//==============================================================================
void SerialMonitor::Open() {
    if (open_) return;
    // WINDOW_NORMAL 允许用户拖动调整窗口大小
    cv::namedWindow(kWindowName, cv::WINDOW_NORMAL);
    cv::resizeWindow(kWindowName, kWidth, kMinHeight);
    open_ = true;
}

//==============================================================================
// SerialMonitor::Close()
// 功能：销毁监视窗口并释放相关资源。
// 参数：无。
// 返回：无。
// 副作用：销毁 OpenCV 窗口，释放 GUI 资源；设置 open_=false。
//         幂等操作：窗口已关闭时直接返回。
//==============================================================================
void SerialMonitor::Close() {
    if (!open_) return;
    cv::destroyWindow(kWindowName);
    open_ = false;
}

//==============================================================================
// SerialMonitor::Update()
// 功能：核心刷新方法——将 9 个收发通道(3个TX发送通道 + 4个RX接收通道 + 2个TCP通道)
//        的状态统计信息绘制到一个深色背景的画布上，然后通过 cv::imshow 显示。
//        每个通道显示标题(含通道名称、收发计数、最后序号)和最近一次的文本内容。
//        长文本会自动按宽度折行，避免超出窗口边界。
// 参数：
//   tx0305 - 发送通道: 0x0305 位置坐标包的收发统计
//   tx0121 - 发送通道: 0x0121 雷达子命令包的收发统计
//   tx0301 - 发送通道: 0x0301 自定义数据包的收发统计
//   rx020E - 接收通道: 0x020E 交互数据的收发统计
//   rx0301 - 接收通道: 0x0301 机器人位置数据的收发统计
//   rx0001 - 接收通道: 0x0001 比赛状态数据(GameStatus)的收发统计
//   rx0003 - 接收通道: 0x0003 前哨站血量数据(Outpost)的收发统计
//   tcp01  - TCP发送通道: 0x01 触发信号(Trigger)的收发统计
//   tcpRx  - TCP接收通道: 8001~8004 端口敌方坐标数据的收发统计
// 返回：无。
// 副作用：更新 OpenCV 窗口显示内容；消耗 CPU 进行文本折行和图像绘制。
//         如果窗口处于关闭状态，调用此函数无任何效果。
//==============================================================================
void SerialMonitor::Update(const SerialStatus& tx0305,
                           const SerialStatus& tx0121,
                           const SerialStatus& tx0301,
                           const SerialStatus& rx020E,
                           const SerialStatus& rx0301,
                           const SerialStatus& rx0001,
                           const SerialStatus& rx0003,
                           const SerialStatus& rx0101,
                           const SerialStatus& tcp01,
                           const SerialStatus& tcpRx8001,
                           const SerialStatus& tcpRx8002,
                           const SerialStatus& tcpRx8003) {
    if (!open_) return;

    const int font = cv::FONT_HERSHEY_SIMPLEX;
    const double scale = 0.40;
    const double hdrScale = 0.50;
    const int thickness = 1;
    const cv::Scalar bgColor(25, 25, 30);
    const int lineH = 18;
    const int margin = 8;
    const int maxTextWidth = kWidth - margin * 2;

    int totalLines = 5; // section headers
    for (const auto* status : {&tx0305, &tx0121, &tx0301, &rx020E, &rx0301, &rx0001, &rx0003, &rx0101, &tcp01,
                               &tcpRx8001, &tcpRx8002, &tcpRx8003}) {
        totalLines += 1;
        std::string t = GetText(*status);
        if (!t.empty()) totalLines += static_cast<int>(WrapLines(t, maxTextWidth).size());
    }

    const int canvasH = std::max(800, margin * 2 + totalLines * lineH);
    cv::Mat canvas(canvasH, kWidth, CV_8UC3, bgColor);
    int y = margin;

    auto drawLine = [&](const std::string& text, const cv::Scalar& color) {
        cv::putText(canvas, text, cv::Point(margin, y), font, scale, color, thickness);
        y += lineH;
    };
    auto drawHdr = [&](const std::string& text, const cv::Scalar& color) {
        cv::putText(canvas, text, cv::Point(margin, y), font, hdrScale, color, thickness + 1);
        y += lineH + 4;
    };
    auto drawSec = [&](const std::string& text) {
        y += 6;
        cv::line(canvas, cv::Point(margin, y), cv::Point(kWidth - margin, y), cv::Scalar(100, 100, 100), 1);
        y += 4;
        cv::putText(canvas, text, cv::Point(margin, y), font, hdrScale, cv::Scalar(200, 200, 255), thickness + 1);
        y += lineH + 4;
    };
    auto drawCh = [&](const std::string& id, const std::string& desc, const cv::Scalar& color,
                      const SerialStatus& st) {
        std::ostringstream oss;
        oss << id << " " << desc << " | cnt=" << st.count.load(std::memory_order_relaxed);
        cv::putText(canvas, oss.str(), cv::Point(margin, y), font, scale, color, thickness + 1);
        y += lineH;
        std::string txt = GetText(st);
        if (txt.empty()) txt = "---";
        for (const auto& line : WrapLines(txt, maxTextWidth))
            drawLine(line, cv::Scalar(180, 180, 180));
        y += 2;
    };

    // ===== SERIAL TX =====
    drawSec("=== SERIAL TX ===");
    drawCh("0x0305", "Position Coords",   cv::Scalar(  0,255,255), tx0305);
    drawCh("0x0121", "Radar CMD (0121)",  cv::Scalar(255,200,  0), tx0121);
    drawCh("0x0301", "Custom Data (0206)",cv::Scalar(  0,255,  0), tx0301);

    // ===== SERIAL RX =====
    drawSec("=== SERIAL RX ===");
    drawCh("0x020E", "Radar Info",        cv::Scalar(255,150,150), rx020E);
    drawCh("0x0301", "Robot Pos (0200)",  cv::Scalar(150,255,150), rx0301);
    drawCh("0x0001", "Game Status",       cv::Scalar(100,200,255), rx0001);
    drawCh("0x0003", "Outpost HP",        cv::Scalar(255,200,100), rx0003);
    drawCh("0x0101", "Energy State",      cv::Scalar(255,200,255), rx0101);

    // ===== TCP TX =====
    drawSec("=== TCP TX ===");
    drawCh("Trigger", "0x01 Signal (port 5000)", cv::Scalar(255,255,100), tcp01);

    // ===== TCP RX (Client -> 192.168.12.99) =====
    drawSec("=== TCP RX (from 192.168.12.99) ===");
    drawCh("8001", "0A01 Pos / 0A02-05 Data", cv::Scalar(255,150,255), tcpRx8001);
    drawCh("8002", "0A06 Cracked Key L1",      cv::Scalar(200,150,200), tcpRx8002);
    drawCh("8003", "0A06 Cracked Key L2",      cv::Scalar(200,150,200), tcpRx8003);

    cv::imshow(kWindowName, canvas);
}

//==============================================================================
// SerialMonitor::GetText() [static]
// 功能：从 SerialStatus 中原子地读取最近一次记录的文本快照。
//        使用 atomic_load_explicit 配合 memory_order_acquire 保证：
//        读取到的 shared_ptr 指向的字符串内容对当前线程可见。
// 参数：
//   s - 串口状态快照结构体，其中的 lastText 是 atomic<shared_ptr<const string>>
// 返回：最近一次记录的文本内容；如果从未记录过则返回空字符串。
// 副作用：无（只读操作，不修改 SerialStatus）。
//==============================================================================
std::string SerialMonitor::GetText(const SerialStatus& s) {
    // memory_order_acquire 保证在此读取之后的所有操作不会重排到此读取之前
    auto sp = std::atomic_load_explicit(&s.lastText, std::memory_order_acquire);
    return sp ? *sp : std::string();
}

//==============================================================================
// SerialMonitor::WrapLines()
// 功能：将长文本按指定像素宽度折行为多行，确保显示时不超出窗口边界。
//        折行策略：每行按经验估算的字符数截断，优先在空格(' ')或竖线('|')处分行，
//        以保持 hex dump 和管道分隔的状态串的可读性。行首的空格和竖线会被跳过。
// 参数：
//   text     - 需要折行的原始文本字符串
//   maxWidth - 可用的最大像素宽度
// 返回：折行后的字符串向量，每个元素为一行；空输入返回空向量。
// 副作用：无（纯计算，不涉及 I/O 或状态修改）。
//==============================================================================
std::vector<std::string> SerialMonitor::WrapLines(const std::string& text, int maxWidth) {
    std::vector<std::string> lines;
    if (text.empty()) return lines;

    const int font = cv::FONT_HERSHEY_SIMPLEX;
    const double scale = 0.45;
    const int thickness = 1;

    // 根据像素宽度估算每行可容纳的字符数(每字符约占用 11 像素宽度)
    // 至少保证 40 字符/行，避免极端情况下行太短
    const int charsPerLine = std::max(40, maxWidth / 11);

    // 文本本身不超过一行，直接返回
    if (static_cast<int>(text.size()) <= charsPerLine) {
        lines.push_back(text);
        return lines;
    }

    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t len = std::min(static_cast<std::size_t>(charsPerLine), text.size() - pos);
        // 如果不是最后一段，尝试在空格或竖线处断行
        // 这样 hex dump "A5 0C 00 ..." 和状态串 "ON|OFF|..." 更易读
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
        // 跳过下一行开头的空格和竖线分隔符，避免行首出现无关字符
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '|')) ++pos;
    }
    return lines;
}

}  // namespace radar26
