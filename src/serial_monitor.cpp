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
                           const SerialStatus& tcp01,
                           const SerialStatus& tcpRx8001,
                           const SerialStatus& tcpRx8002,
                           const SerialStatus& tcpRx8003,
                           const SerialStatus& tcpRx8004) {
    if (!open_) return;

    // ---- 绘制参数 ----
    const int font = cv::FONT_HERSHEY_SIMPLEX;   // OpenCV 内置字体
    const double scale = 0.45;                    // 字体缩放比例，控制文字大小
    const int thickness = 1;                      // 文字线条粗细
    const cv::Scalar bgColor(30, 30, 30);         // 深灰色背景，保护眼睛
    const int lineHeight = 20;                    // 每行文字的高度(像素)
    const int margin = 10;                        // 画布四周边距
    const int maxTextWidth = kWidth - margin * 2; // 文字区域最大宽度

    // ---- 估算所需画布高度 ----
    // 遍历所有9个通道，计算每个通道需要的行数(标题2行 + 内容行)
    int totalLines = 0;
    for (const auto* status : {&tx0305, &tx0121, &tx0301, &rx020E, &rx0301, &rx0001, &rx0003, &tcp01,
                               &tcpRx8001, &tcpRx8002, &tcpRx8003, &tcpRx8004}) {
        totalLines += 2; // 每个通道的标题栏占2行
        std::string text = GetText(*status);
        if (!text.empty()) {
            auto wrapped = WrapLines(text, maxTextWidth);
            totalLines += static_cast<int>(wrapped.size());
        }
    }
    totalLines += 5; // 通道之间的额外间距

    // 画布高度取计算值与最小高度中的较大者
    const int canvasHeight = std::max(kMinHeight, margin * 2 + totalLines * lineHeight);
    // 创建 3 通道 8 位无符号彩色图像作为画布，初始化为深灰背景色
    cv::Mat canvas(canvasHeight, kWidth, CV_8UC3, bgColor);

    int y = margin; // 当前绘制 Y 坐标，从上到下递增

    // ---- 内部绘制辅助 Lambda ----

    // drawLine: 在当前位置绘制单行文字，然后向下移动一行
    auto drawLine = [&](const std::string& text, const cv::Scalar& color) {
        cv::putText(canvas, text, cv::Point(margin, y), font, scale, color, thickness);
        y += lineHeight;
    };

    // drawHeader: 绘制通道标题栏，格式为 "=== 通道名 ===  count=收发次数  lastSeq=最后序号 ==="
    // 标题栏使用稍大的字体和粗线，以便和内容区分
    auto drawHeader = [&](const std::string& label, const cv::Scalar& color,
                          const SerialStatus& status) {
        std::ostringstream oss;
        oss << "=== " << label << " ===  count=" << status.count.load(std::memory_order_relaxed)
            << "  lastSeq=" << status.lastSeq.load(std::memory_order_relaxed) << " ===";
        cv::putText(canvas, oss.str(), cv::Point(margin, y), font, scale + 0.05, color, thickness + 1);
        y += lineHeight + 2;
    };

    // drawChannel: 绘制一个完整的通道块(标题 + 内容文本)
    auto drawChannel = [&](const std::string& label, const cv::Scalar& color,
                           const SerialStatus& status) {
        drawHeader(label, color, status);
        std::string text = GetText(status);
        if (text.empty()) text = "N/A";
        auto lines = WrapLines(text, maxTextWidth);
        for (const auto& line : lines) {
            drawLine(line, cv::Scalar(200, 200, 200)); // 浅灰色内容文字
        }
        y += 4; // 通道之间的间距
    };

    // ---- 按顺序绘制 9 个通道 ----
    // 每个通道使用不同颜色以便快速区分

    // TX 发送通道 (3个)
    drawChannel("TX 0x0305", cv::Scalar(0, 255, 255), tx0305);    // 黄色 - 位置坐标发送
    drawChannel("TX 0x0121", cv::Scalar(255, 200, 0), tx0121);    // 橙色 - 雷达命令发送
    drawChannel("TX 0x0301", cv::Scalar(0, 255, 0), tx0301);      // 绿色 - 自定义数据发送

    // RX 接收通道 (4个)
    drawChannel("RX 0x020E", cv::Scalar(255, 150, 150), rx020E);  // 浅红 - 交互数据接收
    drawChannel("RX 0x0301", cv::Scalar(150, 255, 150), rx0301);  // 浅绿 - 机器人位置接收
    drawChannel("RX 0x0001 (GameStatus)", cv::Scalar(100, 200, 255), rx0001);  // 浅蓝 - 比赛状态
    drawChannel("RX 0x0003 (Outpost)", cv::Scalar(255, 200, 100), rx0003);     // 浅橙 - 前哨站血量

    // TCP 通道 (2个)
    drawChannel("TCP 0x01 (Trigger)", cv::Scalar(255, 255, 100), tcp01);       // 淡黄 - TCP触发
    drawChannel("TCP Rx 8001", cv::Scalar(255, 150, 255), tcpRx8001);
    drawChannel("TCP Rx 8002", cv::Scalar(200, 150, 200), tcpRx8002);
    drawChannel("TCP Rx 8003", cv::Scalar(200, 150, 200), tcpRx8003);
    drawChannel("TCP Rx 8004", cv::Scalar(200, 150, 200), tcpRx8004);         // 粉色 - TCP接收

    // 将绘制好的画布显示到窗口
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
