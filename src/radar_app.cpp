#include "radar_app.hpp"

#include "config_loader.hpp"
#include "daheng_camera.hpp"
#include "serial_protocol.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <filesystem>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
namespace radar26 {
namespace {

// ============================================================================
// 匿名命名空间：文件内部使用的常量、工具函数和辅助类
// ============================================================================

// 12 个机器人统一命名表，索引与 latestSerCoordX_/Y_ 数组对齐：
//   0=R1  1=R2  2=R3  3=R4  4=R6  5=R7  6=B1  7=B2  8=B3  9=B4 10=B6 11=B7
constexpr std::array<const char*, 12> kAllRobotNames = {
    "R1", "R2", "R3", "R4", "R6", "R7", "B1", "B2", "B3", "B4", "B6", "B7",
};

// 固定串口发送间隔（ms），用于早期原型；当前发送周期由 config 中的 sendPeriodMs 控制
constexpr int kFixedSerialSendPeriodMs = 200;
// 串口协议允许的最大 payload 长度（超过则认为帧损坏）
constexpr std::size_t kMaxSerialPayloadLen = 512;
// 串口接收环形缓冲区总容量（字节），超过则覆盖最旧数据
constexpr std::size_t kMaxSerialBufferBytes = 32768;
// 接收白名单：只保留这四种 cmd_id 的帧，其余丢弃
constexpr uint16_t kIncomingCmd020E = 0x020E;  // 雷达信息（双倍易伤/加密等级等）
constexpr uint16_t kIncomingCmd0301 = 0x0301;  // 机器人交互相应位置
constexpr uint16_t kIncomingCmd0001 = 0x0001;  // 比赛状态（game_status_t）
constexpr uint16_t kIncomingCmd0003 = 0x0003;  // 前哨站血量
constexpr uint16_t kIncomingCmd0101 = 0x0101;  // 能量机关状态

// 接收帧早期过滤：只保留白名单内的 cmd_id，减少后续 CRC 校验和解析开销
bool IsIncomingSerialCmd(uint16_t cmdId) {
    return cmdId == kIncomingCmd020E || cmdId == kIncomingCmd0301 ||
           cmdId == kIncomingCmd0001 || cmdId == kIncomingCmd0003 ||
           cmdId == kIncomingCmd0101;
}

// 环形字节缓冲区：用于从串口/TCP 字节流中提取 0xA5 协议帧。
// head_ 指向队首（最早写入的字节），size_ 表示当前有效字节数。
// 当缓冲区满时，新数据会覆盖最旧的字节（自动滑动窗口）。
class SerialByteRingBuffer {
public:
    // capacity: 缓冲区最大容量（字节）
    explicit SerialByteRingBuffer(std::size_t capacity) : data_(capacity) {}

    // 返回当前缓冲区中有效字节数
    std::size_t size() const { return size_; }

    // 按逻辑索引访问（0 = 队首，size_-1 = 队尾），内部自动处理环形折返
    uint8_t operator[](std::size_t index) const {
        return data_[(head_ + index) % data_.size()];
    }

    // 向队尾追加 count 个字节；如果 count 超过总容量，只保留最后 capacity 字节
    void push(const uint8_t* bytes, std::size_t count) {
        if (count == 0U || data_.empty()) return;

        // 如果新数据量超过总容量，直接用末尾片段初始化整个缓冲区
        if (count >= data_.size()) {
            std::copy(bytes + (count - data_.size()), bytes + count, data_.begin());
            head_ = 0U;
            size_ = data_.size();
            return;
        }

        // 如果剩余空间不够，先丢弃队首旧数据腾出空间
        if (count > data_.size() - size_) {
            erase_prefix(count - (data_.size() - size_));
        }

        // 写入：可能需要分两段（队尾→末尾 + 开头→剩余）
        const std::size_t tail = (head_ + size_) % data_.size();
        const std::size_t first = std::min(count, data_.size() - tail);
        std::copy(bytes, bytes + first, data_.begin() + static_cast<std::ptrdiff_t>(tail));
        if (count > first) {
            std::copy(bytes + first, bytes + count, data_.begin());
        }
        size_ += count;
    }

    // 从队首丢弃 count 个字节；count >= size_ 时清空缓冲区
    void erase_prefix(std::size_t count) {
        if (count == 0U) return;
        if (count >= size_) { head_ = 0U; size_ = 0U; return; }
        head_ = (head_ + count) % data_.size();
        size_ -= count;
    }

private:
    std::vector<uint8_t> data_;   // 底层存储
    std::size_t head_ = 0U;       // 队首位置（最早字节索引）
    std::size_t size_ = 0U;       // 有效字节数
};

// 6字节密钥 → 可打印字符串（监视窗口显示用）
// 输入: key (6字节ASCII数组)
// 输出: 对应字符串，如 "K9xR2p"
std::string KeyToString(const std::array<uint8_t, 6>& key) {
    std::string out;
    out.reserve(key.size());
    for (const uint8_t b : key) out.push_back(static_cast<char>(b));
    return out;
}

// 格式化坐标列表为监视窗口文本
// 输入: robotOrder (机器人名字顺序), coords (24个uint16, 12组x,y对)
// 输出: 每行4个机器人，格式 "R1(100,200)  R2(300,400)"
std::string BuildCoordsStatus(const std::vector<std::string>& robotOrder, const std::vector<uint16_t>& coords) {
    std::ostringstream oss;
    const std::size_t count = std::min(robotOrder.size(), coords.size() / 2U);
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0U && (i % 4U) == 0U) oss << "\n";   // 每4个换行
        else if (i != 0U) oss << "  ";                  // 同行内双空格分隔
        oss << robotOrder[i] << "(" << coords[i * 2U] << "," << coords[i * 2U + 1U] << ")";
    }
    return oss.str();
}

// 格式化 0x0121 包内容为监视窗口文本
// 输入: radarCmd(双倍易伤计数), passwordCmd(0/1/2), key(6字节)
std::string BuildRadarCmdStatus(uint8_t radarCmd, uint8_t passwordCmd, const std::array<uint8_t, 6>& key) {
    std::ostringstream oss;
    oss << "radar_cmd=" << static_cast<int>(radarCmd)
        << " password_cmd=" << static_cast<int>(passwordCmd)
        << " key=" << KeyToString(key);
    return oss.str();
}

// 格式化 0x020E 解析结果为监视窗口文本
// 输入: radarInfo (原始字节), bits (解析后的位域)
std::string BuildRadarInfoStatus(uint8_t radarInfo, const RadarInfoBits& bits) {
    std::ostringstream oss;
    oss << "radar_info=" << static_cast<int>(radarInfo)
        << " chance=" << static_cast<int>(bits.hasOpportunity)
        << " enemy_triggered=" << static_cast<int>(bits.enemyTriggered)
        << " encrypt=" << static_cast<int>(bits.encryptLevel)
        << " key_mod=" << static_cast<int>(bits.keyModifiable);
    return oss.str();
}

// 字节数组 → 十六进制字符串（大写，空格分隔），如 "A5 01 00 0E 02"
// 用于监视窗口显示原始帧内容
std::string HexDump(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    bool first = true;
    for (uint8_t b : data) {
        if (!first) oss << ' ';
        first = false;
        oss.width(2);
        oss.fill('0');
        oss << static_cast<int>(b);
    }
    oss << std::dec;
    return oss.str();
}

// 机器人名字 → kAllRobotNames 数组索引
// "R1"→0 "R2"→1 ... "B7"→11, 未知返回 -1
int RobotNameToIndex(const std::string& name) {
    for (std::size_t i = 0; i < kAllRobotNames.size(); ++i)
        if (name == kAllRobotNames[i]) return static_cast<int>(i);
    return -1;
}

// 标号5统一映射为6（裁判系统中编号5和6视为同类，取6）
// "R5"→"R6" "B5"→"B6" 其余不变
std::string NormalizeRobotName(const std::string& input) {
    if (input == "R5") return "R6";
    if (input == "B5") return "B6";
    return input;
}

// 判断是否为12个合法机器人名之一
bool IsKnownRobot(const std::string& name) {
    return std::find(kAllRobotNames.begin(), kAllRobotNames.end(), name) != kAllRobotNames.end();
}

// 构建 0x0305 发送顺序（根据当前阵营，对方6个在前，己方6个在后）
// 红方: [B1..B7(敌人), R1..R7(己方)]
// 蓝方: [R1..R7(敌人), B1..B7(己方)]
std::vector<std::string> BuildRobotOrder(Team team) {
    if (team == Team::Red)
        return {"B1", "B2", "B3", "B4", "B6", "B7", "R1", "R2", "R3", "R4", "R6", "R7"};
    return {"R1", "R2", "R3", "R4", "R6", "R7", "B1", "B2", "B3", "B4", "B6", "B7"};
}

// 浮点矩形 → 整数矩形（裁切到图像范围内）
// 输入: r(浮点坐标框), width/height(图像尺寸)
// 返回: 有效矩形；框完全出界或过小则返回空矩形
cv::Rect ClampRect(const cv::Rect2f& r, int width, int height) {
    int x = std::max(0, static_cast<int>(std::floor(r.x)));
    int y = std::max(0, static_cast<int>(std::floor(r.y)));
    int w = std::max(0, static_cast<int>(std::ceil(r.width)));
    int h = std::max(0, static_cast<int>(std::ceil(r.height)));
    if (x >= width || y >= height) return cv::Rect();
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 1 || h <= 1) return cv::Rect();  // 太小视为无效
    return cv::Rect(x, y, w, h);
}

// int → uint16_t（钳位到 [0, 65535]）
uint16_t ClipToU16(int v) {
    if (v < 0) return 0;
    if (v > 65535) return 65535;
    return static_cast<uint16_t>(v);
}

// 6字符字符串 → 6字节数组（生成新密钥时用）
// 输入: key (恰好6个ASCII字符的字符串)
// 输出: 对应 uint8_t[6]
std::array<uint8_t, 6> KeyToArray(const std::string& key) {
    std::array<uint8_t, 6> out{};
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint8_t>(key[i]);
    return out;
}

// 红方标红色(BGR=0,0,255)，蓝方标蓝色(BGR=255,0,0)
cv::Scalar TeamColor(const std::string& name) {
    return name[0] == 'R' ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 0, 0);
}

// 等比例缩放图像到 maxWidth×maxHeight 以内，超界时才缩放，否则返回原图
// 输入: src(原图), maxWidth/maxHeight(显示上限)
// 输出: 缩放后的图像（可能和src相同）
cv::Mat ResizeForDisplay(const cv::Mat& src, int maxWidth, int maxHeight) {
    if (src.empty() || maxWidth <= 0 || maxHeight <= 0) return src;
    if (src.cols <= maxWidth && src.rows <= maxHeight) return src;
    const double sx = static_cast<double>(maxWidth) / src.cols;
    const double sy = static_cast<double>(maxHeight) / src.rows;
    const double scale = std::max(1e-6, std::min(sx, sy));  // 取较小比例保证不超界
    cv::Mat dst;
    cv::resize(src, dst, cv::Size(), scale, scale, cv::INTER_AREA);
    return dst;
}

}  // namespace

// ============================================================================
// RadarApp 构造 / 析构 / 初始化
// ============================================================================

// 构造：保存配置副本（move 避免拷贝），其余成员在 Initialize() 中初始化
RadarApp::RadarApp(AppConfig config, std::string configPath)
    : config_(std::move(config)), configPath_(std::move(configPath)) {}

// 析构：逆序关闭所有线程和连接
RadarApp::~RadarApp() {
    StopSerialThreads();          // 先停所有子线程，再关硬件
    if (serial_) serial_->Close();
    if (tcp_) tcp_->Close();
}

// 初始化：加载标定 → 建滤波器 → 初始化坐标数组 → 生成随机密钥 → 加载模型 → 开串口/TCP
// 输出: error(失败时写入原因), 返回 true 表示成功
bool RadarApp::Initialize(std::string* error) {
    // 1. 加载标定数据（单应矩阵、地图、掩码）
    if (!ConfigLoader::LoadCalibration(config_, &calibration_, error)) return false;

    // 2. 创建目标位置滤波器（窗口大小=1，超时来自配置）
    filter_ = std::make_unique<TargetFilter>(1, config_.detection.filterTimeoutSec);

    // 3. 初始化 12 个机器人的原子坐标数组（全零、无效）
    for (const auto* name : kAllRobotNames) sendLastSerCoords_[name] = std::nullopt;
    for (std::size_t i = 0; i < kAllRobotNames.size(); ++i) {
        latestSerCoordX_[i].store(0, std::memory_order_relaxed);
        latestSerCoordY_[i].store(0, std::memory_order_relaxed);
        latestSerCoordValid_[i].store(false, std::memory_order_relaxed);
        latestSerCoordFromDetection_[i].store(false, std::memory_order_relaxed);
    }

    // 4. 初始己方密钥（DBD31D）
    currentKey_ = KeyToArray("DBD31D");

    // 4.5 初始化相机实时参数（曝光 + 增益）
    targetExposure_.store(config_.camera.exposureTime, std::memory_order_relaxed);
    targetGain_.store(config_.camera.gain, std::memory_order_relaxed);
    lastSavedExposure_ = config_.camera.exposureTime;
    lastSavedGain_ = config_.camera.gain;

    // 5. 加载两个 TensorRT 检测模型（车辆 + 装甲板）
    if (!carDetector_.Load(config_.model.carEnginePath, config_.model.carClassNames,
                           config_.model.inputWidth, config_.model.inputHeight, error)) return false;
    if (!armorDetector_.Load(config_.model.armorEnginePath, config_.model.armorClassNames,
                             config_.model.inputWidth, config_.model.inputHeight, error)) return false;

    // 6. 串口初始化（如果启用）
    if (config_.serial.enable) {
        if (!OpenSerial(error)) return false;
        StartSerialThreads();  // 启动 send/recv/parser/tcp 线程
    }
    // 7. TCP 初始化（如果启用）：客户端连目标服务器 + 启动接收服务器 8001~8004
    if (config_.tcp.enable) {
        if (!OpenTcp(error)) return false;   // 连接失败不阻塞
        StartTcpReceivers();                  // TCP Client 连 8001~8004
    }
    return true;
}

// 打开串口设备
// 输出: error(失败原因), 返回 true=成功
bool RadarApp::OpenSerial(std::string* error) {
    serial_ = std::make_unique<SerialPort>();
    if (!serial_->Open(config_.serial.port, config_.serial.baudrate, error)) return false;
    std::cout << "Serial opened: " << config_.serial.port << " @" << config_.serial.baudrate << std::endl;
    return true;
}

// 建立 TCP 客户端连接到目标服务器（用于发送 0x00/0x01 触发信号）
// 输出: error(失败原因，但连接失败不阻止启动), 返回始终为 true
bool RadarApp::OpenTcp(std::string* error) {
    tcp_ = std::make_unique<TcpClient>();
    if (!tcp_->Connect(config_.tcp.ip, config_.tcp.port, error)) {
        // 连接失败不阻塞启动，TcpSendLoop 每 2s 自动重连
        std::cerr << "[TCP] connect failed: " << (error ? *error : "unknown") << std::endl;
        std::cerr << "[TCP] trigger logic will run, send will be skipped until connected" << std::endl;
        if (error) error->clear();
        return true;
    }
    std::cout << "TCP connected: " << config_.tcp.ip << ":" << config_.tcp.port << std::endl;

    // 建立连接后发送 0x00 握手测试通信
    std::string sendError;
    if (!tcp_->Send({0x00}, &sendError)) {
        std::cerr << "[TCP] handshake 0x00 failed: " << sendError << std::endl;
    } else {
        std::cout << "TCP handshake 0x00 sent OK" << std::endl;
    }
    return true;
}

// ============================================================================
// TcpSendLoop — TCP 触发信号发送线程（每 100ms 一周期）
//
// 三个一次性触发器：
//   T1: game_progress==4 && remainTime<390s && outpostHealth 下降过
//   T2: 我方工程机器人被检测到且坐标满足条件
//   T3: remainTime<300s
// 任一满足 → 向 tcp 目标发送 0x01
// ============================================================================
void RadarApp::TcpSendLoop() {
    const auto checkPeriod = std::chrono::milliseconds(100);
    auto nextTick = std::chrono::steady_clock::now();

    // 重连计时：每 2 秒尝试一次，避免高频 connect 刷屏
    auto lastReconnectAttempt = std::chrono::steady_clock::now();
    const auto reconnectInterval = std::chrono::seconds(2);

    while (running_.load(std::memory_order_relaxed)) {
        nextTick += checkPeriod;

        // ---- 断线重连 ----
        bool tcpOk = (tcp_ != nullptr && tcp_->IsConnected());
        if (!tcpOk && tcp_ != nullptr) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastReconnectAttempt >= reconnectInterval) {
                lastReconnectAttempt = now;
                std::string err;
                if (tcp_->Connect(config_.tcp.ip, config_.tcp.port, &err)) {
                    // 连接成功，发 0x00 握手
                    tcp_->Send({0x00}, &err);
                    tcpOk = true;
                }
            }
        }

        // ---- 读取触发输入 ----
        const uint8_t  gameType     = latestGameType_.load(std::memory_order_relaxed);
        const uint8_t  gameProgress = latestGameProgress_.load(std::memory_order_relaxed);
        const uint16_t remainTime   = latestStageRemainTime_.load(std::memory_order_relaxed);
        const uint16_t outpostHealth = latestOutpostHealth_.load(std::memory_order_relaxed);

        // 工程机器人信息：蓝方B2(index=7), 红方R2(index=1)
        const int engSlot = (config_.team == Team::Blue) ? 7 : 1;
        const bool engValid = latestSerCoordValid_[static_cast<std::size_t>(engSlot)].load(std::memory_order_acquire);
        const bool engDetected = engValid && latestSerCoordFromDetection_[static_cast<std::size_t>(engSlot)].load(std::memory_order_relaxed);
        const int engCoordY = engValid ? latestSerCoordX_[static_cast<std::size_t>(engSlot)].load(std::memory_order_relaxed) : -1;

        // ---- 触发判断（无论是否连接都执行）----
        bool shouldSend01 = false;
        std::string triggerReason;

        // 触发1（一次性）：比赛阶段game_progress==4 且 剩余时间<390s 且 前哨站掉血
        bool t1Fired = tcpTrigger1Fired_.load(std::memory_order_relaxed);
        if (!t1Fired && gameProgress == 4 && remainTime > 0 && remainTime < 390
            && outpostHealthDecreased_.load(std::memory_order_relaxed)) {
            tcpTrigger1Fired_.store(true, std::memory_order_relaxed);
            t1Fired = true;
            shouldSend01 = true;
            triggerReason = "T1: progress==4 & time<390s & hp_decreased";
        }

        // 触发2（一次性）：我方工程机器人到达指定位置（仅当真实检测到，盲区猜测不算）
        bool t2Fired = tcpTrigger2Fired_.load(std::memory_order_relaxed);
        if (!t2Fired && gameProgress == 4 && engDetected && remainTime > 0) {
            const bool positionReached = (config_.team == Team::Blue)
                ? (engCoordY > 400) : (engCoordY < 2200);
            if (positionReached) {
                tcpTrigger2Fired_.store(true, std::memory_order_relaxed);
                t2Fired = true;
                shouldSend01 = true;
                triggerReason = std::string("T2: team=")
                    + (config_.team == Team::Blue ? "Blue" : "Red")
                    + " slot=" + std::to_string(engSlot)
                    + " coordY=" + std::to_string(engCoordY);
            }
        }

        // 触发3（一次性）：剩余时间<180s 后只发一次
        bool t3Fired = tcpTrigger3Fired_.load(std::memory_order_relaxed);
        if (!t3Fired && remainTime > 0 && gameProgress == 4 && remainTime < 180) {
            tcpTrigger3Fired_.store(true, std::memory_order_relaxed);
            t3Fired = true;
            shouldSend01 = true;
            if (triggerReason.empty()) triggerReason = "T3: time<180s once";
        }
        const bool t3Active = t3Fired;

        // ---- 实际发送（仅在连接正常时）----
        if (shouldSend01 && tcpOk) {
            std::string error;
            tcp_->Send({0x01}, &error);
        }

        // ---- 构造状态文本（始终显示完整诊断）----
        {
            std::ostringstream oss;
            oss << (tcpOk ? "" : "[NO-TCP] ")
                << (shouldSend01 ? "SEND" : "IDLE")
                << " | progress=" << static_cast<int>(gameProgress)
                << " remain=" << remainTime << "s"
                << " hp=" << outpostHealth;
            if (engValid) {
                oss << " engY=" << engCoordY;
                oss << (config_.team == Team::Blue ? " (need>400)" : " (need<2200)");
                if (engDetected) oss << "[det]";
            } else {
                oss << " engY=N/A";
            }
            oss << " hpDn=" << (outpostHealthDecreased_.load() ? "Y" : "N")
                << " | T1=" << (t1Fired ? "DONE" : "wait")
                << " T2=" << (t2Fired ? "DONE" : "wait")
                << " T3=" << (t3Active ? "ON" : "off")
                << " | energy S=" << smallEnergyActivated_.load()
                << " B=" << bigEnergyActivated_.load()
                << " | dbl330=" << doubleTriggered330_ << " dbl180=" << doubleTriggered180_;
            UpdateSerialStatus(&tcp01_, 0, oss.str());
        }

        const auto nowAfterLoop = std::chrono::steady_clock::now();
        if (nextTick <= nowAfterLoop) nextTick = nowAfterLoop;
        std::this_thread::sleep_until(nextTick);
    }
}

// ---- TCP Client 接收线程：主动连接 192.168.12.99:8001~8004 ----
//有线模式192.168.12.99
static const char* kTcpRxIp = "192.168.12.99";

void RadarApp::StartTcpReceivers() {
    running_.store(true, std::memory_order_release);  // 确保接收线程看到 running_=true
    fprintf(stderr, "[TCP-RX] starting receiver threads for ports 8001, 8002, 8003 ...\n");
    fflush(stderr);
    tcpRxThread8001_ = std::thread(&RadarApp::TcpReaderLoop, this, 8001);
    fprintf(stderr, "[TCP-RX] 8001 thread created\n");
    fflush(stderr);
    tcpRxThread8002_ = std::thread(&RadarApp::TcpReaderLoop, this, 8002);
    fprintf(stderr, "[TCP-RX] 8002 thread created\n");
    fflush(stderr);
    tcpRxThread8003_ = std::thread(&RadarApp::TcpReaderLoop, this, 8003);
    fprintf(stderr, "[TCP-RX] 8003 thread created, all launched\n");
    fflush(stderr);
}

void RadarApp::StopTcpReceivers() {
    if (tcpRxThread8001_.joinable()) tcpRxThread8001_.join();
    if (tcpRxThread8002_.joinable()) tcpRxThread8002_.join();
    if (tcpRxThread8003_.joinable()) tcpRxThread8003_.join();
}

// TCP Client 读取循环：connect → recv 流式数据 → 环形缓冲区解 0xA5 帧 → 回调 → 断线重连
void RadarApp::TcpReaderLoop(int port) {
    fprintf(stderr, "[TCP-RX:%d] thread started, running=%d\n", port,
            static_cast<int>(running_.load(std::memory_order_relaxed)));
    fflush(stderr);
    const auto reconnectDelay = std::chrono::seconds(2);
    while (running_.load(std::memory_order_acquire)) {
        TcpClient client;
        std::string err;
        fprintf(stderr, "[TCP-RX:%d] connecting to %s:%d ...\n", port, kTcpRxIp, port);
        fflush(stderr);
        if (!client.Connect(kTcpRxIp, port, &err)) {
            fprintf(stderr, "[TCP-RX:%d] connect failed: %s\n", port, err.c_str());
            fflush(stderr);
            std::this_thread::sleep_for(reconnectDelay);
            continue;
        }
        fprintf(stderr, "[TCP-RX:%d] connected\n", port);
        fflush(stderr);

        std::vector<uint8_t> ring(65536);
        std::size_t head = 0, sz = 0;
        uint8_t buf[4096];
        const std::size_t kMaxPayload = 512;
        auto lastNoneTime = std::chrono::steady_clock::now();

        int64_t bytesReceived = 0;
        while (running_.load(std::memory_order_acquire)) {
            ssize_t n = client.Read(buf, sizeof(buf), &err);
            if (n < 0) {
                fprintf(stderr, "[TCP-RX:%d] %s (total rx=%ld bytes)\n", port, err.c_str(), bytesReceived);
                break;
            }
            if (n == 0) {
                auto now = std::chrono::steady_clock::now();
                if (now - lastNoneTime >= std::chrono::seconds(5)) {
                    fprintf(stderr, "[TCP-RX:%d] no data (5s)\n", port);
                    lastNoneTime = now;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            bytesReceived += n;
            for (ssize_t i = 0; i < n; ++i) {
                if (sz >= ring.size()) { head = (head + 1) % ring.size(); --sz; }
                ring[(head + sz) % ring.size()] = buf[i]; ++sz;
            }

            while (sz >= 9) {
                while (sz > 0 && ring[head] != 0xA5) { head = (head + 1) % ring.size(); --sz; }
                if (sz < 9) break;
                uint16_t dataLen = static_cast<uint16_t>(ring[(head + 1) % ring.size()])
                                 | (static_cast<uint16_t>(ring[(head + 2) % ring.size()]) << 8);
                if (dataLen > kMaxPayload) { head = (head + 1) % ring.size(); --sz; continue; }
                std::size_t frameLen = 7 + dataLen + 2;
                if (sz < frameLen) break;
                std::vector<uint8_t> frame;
                frame.reserve(frameLen);
                for (std::size_t i = 0; i < frameLen; ++i)
                    frame.push_back(ring[(head + i) % ring.size()]);
                static int parsedOk = 0, parseFail = 0;
                static auto lastStatTime = std::chrono::steady_clock::now();
                auto parsed = ParsePacket(frame);
                if (parsed.has_value()) {
                    if (port == 8001) OnTcp8001Frame(frame);
                    else if (port == 8002) OnTcpKeyFrame(0, frame);
                    else if (port == 8003) OnTcpKeyFrame(1, frame);
                    parsedOk++;
                } else {
                    parseFail++;
                }
                auto now = std::chrono::steady_clock::now();
                if (now - lastStatTime >= std::chrono::seconds(5)) {
                    fprintf(stderr, "[TCP-RX:%d] 5s stats: %d parsed OK, %d CRC fail\n",
                            port, parsedOk, parseFail);
                    fflush(stderr);
                    parsedOk = 0; parseFail = 0;
                    lastStatTime = now;
                }
                head = (head + frameLen) % ring.size();
                sz -= frameLen;
            }
        }
        fprintf(stderr, "[TCP-RX:%d] disconnected, reconnecting...\n", port);
    }
}

// 8001 端口帧回调：解析 0x0A01(对方坐标)→tcpEnemyCoords_, 0x0A02~0A05→tcp0206Data_[66]
// 四段到齐后标记 tcp0206AllValid_=true，SerialSendLoop 会用这些数据覆盖默认值
void RadarApp::OnTcp8001Frame(const std::vector<uint8_t>& frame) {
    auto parsed = ParsePacket(frame);
    if (!parsed.has_value()) return;

    switch (parsed->cmdId) {
    case 0x0201:  // 13字节，暂不解析，仅打印原始hex
    case 0x0202:  // 14字节，暂不解析，仅打印原始hex
        std::cerr << "[TCP-RX:8001] 0x" << std::hex << parsed->cmdId << std::dec
                  << " raw: " << HexDump(parsed->payload) << std::endl;
        UpdateSerialStatus(&tcpRx8001_, parsed->seq, HexDump(parsed->payload));
        break;
    case 0x0A01: {
        if (parsed->payload.size() != 24) return;

        // 解析 12 个 int16 坐标
        int16_t tcpCoords[12];
        for (int i = 0; i < 12; ++i)
            tcpCoords[i] = static_cast<int16_t>(parsed->payload[i * 2])
                         | (static_cast<int16_t>(parsed->payload[i * 2 + 1]) << 8);

        // 直接覆盖 TCP 数据到 tcpEnemyCoords_
        const std::vector<std::string> enemyNames =
            (config_.team == Team::Red)
                ? std::vector<std::string>{"B1","B2","B3","B4","B6","B7"}
                : std::vector<std::string>{"R1","R2","R3","R4","R6","R7"};

        int accepted = 0;
        std::ostringstream oss;
        for (int i = 0; i < 6; ++i) {
            int16_t tx = tcpCoords[i * 2];
            int16_t ty = tcpCoords[i * 2 + 1];
            if (i % 3 == 0) oss << "\n ";
            oss << enemyNames[i] << "(" << tx << "," << ty << ")";
            tcpEnemyCoords_[i * 2].store(tx, std::memory_order_relaxed);
            tcpEnemyCoords_[i * 2 + 1].store(ty, std::memory_order_relaxed);
            sendLastSerCoords_[enemyNames[i]] = std::make_pair(static_cast<int>(tx), static_cast<int>(ty));
            accepted++;
        }
        oss << "\n  accept=" << accepted;
        tcp8001PosLine_ = oss.str();
        tcpEnemyCoordsValid_.store(true, std::memory_order_release);
        UpdateTcp8001Status(parsed->seq);
        break;
    }
    case 0x0A02: {
        if (parsed->payload.size() != 12) return;
        for (std::size_t i = 0; i < 12; ++i) tcp0206Data_[i] = parsed->payload[i];
        tcp0206SegValid_[0].store(true, std::memory_order_relaxed);
        break;
    }
    case 0x0A03: {
        if (parsed->payload.size() != 10) return;
        for (std::size_t i = 0; i < 10; ++i) tcp0206Data_[12 + i] = parsed->payload[i];
        tcp0206SegValid_[1].store(true, std::memory_order_relaxed);
        break;
    }
    case 0x0A04: {
        if (parsed->payload.size() != 8) return;
        for (std::size_t i = 0; i < 8; ++i) tcp0206Data_[22 + i] = parsed->payload[i];
        tcp0206SegValid_[2].store(true, std::memory_order_relaxed);
        break;
    }
    case 0x0A05: {
        if (parsed->payload.size() != 36) return;
        for (std::size_t i = 0; i < 36; ++i) tcp0206Data_[30 + i] = parsed->payload[i];
        tcp0206SegValid_[3].store(true, std::memory_order_relaxed);
        break;
    }
    default: return;
    }
    // 四段全部到齐后显示完整 66 字节
    if (tcp0206SegValid_[0].load(std::memory_order_relaxed) &&
        tcp0206SegValid_[1].load(std::memory_order_relaxed) &&
        tcp0206SegValid_[2].load(std::memory_order_relaxed) &&
        tcp0206SegValid_[3].load(std::memory_order_relaxed)) {
        bool wasValid = tcp0206AllValid_.load(std::memory_order_relaxed);
        tcp0206AllValid_.store(true, std::memory_order_release);
        if (!wasValid) {
            std::vector<uint8_t> all66(tcp0206Data_.data(), tcp0206Data_.data() + 66);
            tcp8001DataLine_ = "66B " + HexDump(all66);
            UpdateTcp8001Status(0);
        }
    }
}

void RadarApp::OnTcpKeyFrame(int level, const std::vector<uint8_t>& frame) {
    auto parsed = ParsePacket(frame);
    if (!parsed.has_value() || parsed->cmdId != 0x0A06 || parsed->payload.size() != 6) return;

    for (std::size_t i = 0; i < 6; ++i)
        tcpCrackedKey_[level][i] = parsed->payload[i];
    tcpCrackedKeyValid_[level].store(true, std::memory_order_release);

    const int port = 8002 + level;  // level 0→8002, 1→8003, 2→8004
    std::string keyStr = KeyToString(tcpCrackedKey_[level]);
    std::cerr << "[TCP-RX:" << port << "] 0x0A06 key L" << (level + 1)
              << " = " << keyStr << std::endl;

    std::ostringstream oss;
    oss << "0x0A06 key L" << (level + 1) << "=" << keyStr;
    SerialStatus* rxSt = (level == 0) ? &tcpRx8002_ : &tcpRx8003_;
    UpdateSerialStatus(rxSt, parsed->seq, oss.str());
}

void RadarApp::UpdateTcp8001Status(uint8_t seq) {
    std::string combined;
    if (tcp8001PosLine_.empty()) {
        combined = "waiting data...";
    } else {
        combined = tcp8001PosLine_;
    }
    if (!tcp8001DataLine_.empty()) {
        combined += "\n" + tcp8001DataLine_;
    }
    UpdateSerialStatus(&tcpRx8001_, seq, combined);
}

void RadarApp::StartSerialThreads() {
    running_.store(true);
    sendThread_ = std::thread(&RadarApp::SerialSendLoop, this);
    recvThread_ = std::thread(&RadarApp::SerialReceiveLoop, this);
    if (config_.tcp.enable) {
        tcpThread_ = std::thread(&RadarApp::TcpSendLoop, this);
    }

    // 启动解析线程，专门处理串口接收队列中的数据包。
    parserRunning_.store(true);
    const int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
    int parsers = 1;
    if (hwThreads > 2) parsers = std::max(1, hwThreads - 2);
    for (int i = 0; i < parsers; ++i) parserThreads_.emplace_back(&RadarApp::ParserWorker, this);
}

void RadarApp::StopSerialThreads() {
    running_.store(false);
    if (sendThread_.joinable()) sendThread_.join();
    if (recvThread_.joinable()) recvThread_.join();
    if (tcpThread_.joinable()) tcpThread_.join();

    // 停止解析线程并清空队列。
    parserRunning_.store(false);
    parseQueueCv_.notify_all();
    for (auto &t : parserThreads_) if (t.joinable()) t.join();
    parserThreads_.clear();

    if (config_.tcp.enable) StopTcpReceivers();
}

// ============================================================================
// SerialSendLoop — 串口发送主循环（每 sendPeriodMs 一周期）
//
// 每周期依次发送：
//   1. 0x0305 位置包：12 机器人坐标（filter/猜测 + TCP 0x0A01 覆盖对方6个）
//   2. 0x0301→0x0206 自定义数据包：66B（TCP 真实数据 或 默认填充）
//   3. 0x0301→0x0121 雷达命令：初始注册 + 0x020E 事件驱动（双倍/改密/破解）
// ============================================================================
void RadarApp::SerialSendLoop() {
    uint8_t seq = 0;            // 帧序号，每发一包 +1
    const int cfgPeriod = std::max(1, config_.serial.sendPeriodMs);
    const auto sendPeriod = std::chrono::milliseconds(cfgPeriod);
    auto nextTick = std::chrono::steady_clock::now();

    while (running_.load(std::memory_order_relaxed)) {
        nextTick += sendPeriod;
        if (serial_ == nullptr || !serial_->IsOpen()) {
            std::this_thread::sleep_until(nextTick);
            continue;
        }

        const auto now = std::chrono::steady_clock::now();

        const std::map<std::string, cv::Point2f> allData = filter_->GetAllData();
        const std::vector<std::string> robotOrder = BuildRobotOrder(config_.team);
        std::vector<uint16_t> coords;
        coords.reserve(24);

        const std::vector<std::string> enemyNames =
            (config_.team == Team::Red)
                ? std::vector<std::string>{"B1","B2","B3","B4","B6","B7"}
                : std::vector<std::string>{"R1","R2","R3","R4","R6","R7"};

        for (std::size_t ri = 0; ri < robotOrder.size(); ++ri) {
            const std::string& name = robotOrder[ri];
            int sx = 0, sy = 0;
            bool fromDetection = false;
            bool isEnemy = (ri < 6);
            auto dataIt = allData.find(name);

            if (isEnemy && tcpEnemyCoordsValid_.load(std::memory_order_acquire)) {
                const int tcpSx = static_cast<int>(tcpEnemyCoords_[ri * 2].load(std::memory_order_relaxed));
                const int tcpSy = static_cast<int>(tcpEnemyCoords_[ri * 2 + 1].load(std::memory_order_relaxed));
                if (config_.debug) {
                    sx = tcpSx; sy = tcpSy;
                } else if (dataIt != allData.end()) {
                    const auto visMapped = MapToSerCoords(name, dataIt->second.x, dataIt->second.y);
                    constexpr int kMaxDist = 300;
                    if (std::abs(tcpSx - visMapped.first) <= kMaxDist &&
                        std::abs(tcpSy - visMapped.second) <= kMaxDist) {
                        sx = tcpSx; sy = tcpSy;
                    } else {
                        sx = visMapped.first; sy = visMapped.second; fromDetection = true;
                    }
                } else {
                    sx = tcpSx; sy = tcpSy;
                }
                sendLastSerCoords_[name] = std::make_pair(sx, sy);
            } else if (dataIt != allData.end()) {
                // 摄像头检测到 → 用摄像头坐标
                const auto mapped = MapToSerCoords(name, dataIt->second.x, dataIt->second.y);
                sx = mapped.first; sy = mapped.second;
                sendLastSerCoords_[name] = mapped;
                fromDetection = true;
            } else {
                const auto lastIt = sendLastSerCoords_.find(name);
                if (lastIt != sendLastSerCoords_.end() && lastIt->second.has_value()) {
                    sx = lastIt->second->first; sy = lastIt->second->second;
                }
            }

            const uint16_t sxU16 = ClipToU16(sx);
            const uint16_t syU16 = ClipToU16(sy);
            coords.push_back(sxU16);
            coords.push_back(syU16);

            const int slot = RobotNameToIndex(name);
            if (slot >= 0) {
                latestSerCoordX_[slot].store(static_cast<int>(sxU16), std::memory_order_relaxed);
                latestSerCoordY_[slot].store(static_cast<int>(syU16), std::memory_order_relaxed);
                latestSerCoordFromDetection_[slot].store(fromDetection, std::memory_order_relaxed);
                latestSerCoordValid_[slot].store(true, std::memory_order_release);
            }
        }


        std::vector<uint8_t> posPacket;
        std::string error;
        if (BuildPositionPacket(seq, coords, &posPacket, &error)) {
            if (serial_->Write(posPacket, &error))
            {
                std::string sentText = HexDump(posPacket) + " | " + BuildCoordsStatus(robotOrder, coords);
                UpdateSerialStatus(&tx0305_, seq, sentText);
            }
        }

        {
            const uint16_t senderId = (config_.team == Team::Red) ? 9 : 109;
            // 0x0206 只发给己方：红方→1,3,4,6,7  蓝方→101,103,104,106,107
            const std::vector<uint16_t> receiverIds =
                (config_.team == Team::Red)
                    ? std::vector<uint16_t>{1, 3, 4, 6, 7}
                    : std::vector<uint16_t>{101, 103, 104, 106, 107};

            // 0x0206 66B 定频 1Hz 转发
            bool tcp66FwdNow = false;
            {
                auto tcpNow = std::chrono::steady_clock::now();
                if (tcpNow - lastTcpFwdTime_ >= std::chrono::milliseconds(1000)) {
                    tcp66FwdNow = true;
                    lastTcpFwdTime_ = tcpNow;
                }
            }
            auto pushU16 = [&](std::vector<uint8_t>& d, uint16_t v) {
                d.push_back(static_cast<uint8_t>(v & 0xFF));
                d.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            };
            if (tcp66FwdNow && tcp0206AllValid_.load(std::memory_order_acquire)) {
                for (uint16_t rid : receiverIds) {
                    std::vector<uint8_t> data;
                    data.reserve(72);
                    pushU16(data, 0x0206);
                    pushU16(data, senderId);
                    pushU16(data, rid);
                    for (int i = 0; i < 66; ++i) data.push_back(tcp0206Data_[i]);
                    std::vector<uint8_t> pkt = BuildGeneralPacket(seq, 0x0301, data);
                    std::string err;
                    if (serial_->Write(pkt, &err)) {
                        UpdateSerialStatus(&tx0301_, seq, "tcp0206 ->" + std::to_string(rid));
                    }
                    ++seq;
                }
            } else if (!tcp0206AllValid_.load(std::memory_order_acquire)) {
                for (uint16_t rid : receiverIds) {
                    std::vector<uint8_t> pkt;
                    std::string err;
                    if (BuildRadarCustomData0301Packet(seq, senderId, rid, &pkt, &err)) {
                        if (serial_->Write(pkt, &err)) {
                            UpdateSerialStatus(&tx0301_, seq, "default ->" + std::to_string(rid));
                        }
                    }
                    ++seq;
                }
            }
        }

        // ---- 0x0121 发送（三种触发独立，只受各自状态驱动）----
        const uint16_t senderId0121 = (config_.team == Team::Red) ? 9 : 109;

        auto send0121 = [&](uint8_t radarCmd, uint8_t pwdCmd,
                            const std::array<uint8_t, 6>& key, const std::string& label) {
            std::vector<uint8_t> pkt;
            std::string err;
            if (BuildRadarCmdPacket(seq, senderId0121, radarCmd, pwdCmd, key, &pkt, &err)) {
                if (serial_->Write(pkt, &err)) {
                    UpdateSerialStatus(&tx0121_, seq, label);
                }
                ++seq;
            }
        };

        // 初始注册（仅一次）
        if (!initialKeySent_) {
            send0121(keyModCount_++, 1, currentKey_, "INIT key=" + KeyToString(currentKey_));
            initialKeySent_ = true;
        }

        // 0x020E 事件驱动
        if (hasRadarInfo020E_.load(std::memory_order_acquire)) {
            const uint32_t curEpoch = doubleTriggerEpoch_.load(std::memory_order_relaxed);
            if (curEpoch != lastHandled020EEpoch_) {
                lastHandled020EEpoch_ = curEpoch;

                const int chance    = std::max(0, doubleVulnerabilityChance_.load(std::memory_order_relaxed));
                const int triggered = std::max(0, opponentDoubleTriggered_.load(std::memory_order_relaxed));
                const int keyMod    = std::max(0, keyModifiable_.load(std::memory_order_relaxed));
                const int encLvl    = std::max(0, encryptLevel_.load(std::memory_order_relaxed));


                // ---- 触发 B：修改密钥（上升沿：0→1）----
                static int prevKeyMod = 0;
                if (keyMod == 1 && prevKeyMod == 0) {
                    keyModCount_++;
                    if (keyModCount_ == 0) keyModCount_ = 1;
                    static const std::array<const char*, 3> kFixedKeys = {"DBD31D", "C74B91", "2E4512"};
                    currentKey_ = KeyToArray(std::string(kFixedKeys[(keyModCount_ - 1) % 3]));
                    send0121(keyModCount_, 1, currentKey_, "KEYMOD");
                }
                prevKeyMod = keyMod;

                // ---- 触发 C：破解密钥 ----
                // encryptLevel>=1 + 13s冷却 + 端口密钥valid
                if (encLvl >= 1 && encLvl <= 2) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now - lastCrackedSendTime_ >= std::chrono::seconds(13)) {
                        const int lvlIdx = encLvl - 1;
                        if (tcpCrackedKeyValid_[lvlIdx].load(std::memory_order_acquire)) {
                            lastCrackedSendTime_ = now;
                            crackedKeyCount_++;
                            if (crackedKeyCount_ == 0) crackedKeyCount_ = 1;
                            send0121(crackedKeyCount_, 2, tcpCrackedKey_[lvlIdx],
                                     "CRACKED L" + std::to_string(encLvl));
                        }
                    }
                }
            }
        }

        // ---- 双倍易伤触发：阶段4 + chance>0 + 两个时间窗口各触发一次 ----
        {
            const uint8_t  stage = latestGameProgress_.load(std::memory_order_relaxed);
            const uint16_t remain = latestStageRemainTime_.load(std::memory_order_relaxed);
            const int chance = std::max(0, doubleVulnerabilityChance_.load(std::memory_order_relaxed));
            if (stage == 4 && remain > 0 && chance > 0) {
                bool triggerNow = false;
                if (remain < 330 && !doubleTriggered330_) {
                    doubleTriggered330_ = true;
                    triggerNow = true;
                }
                if (remain < 180 && !doubleTriggered180_) {
                    doubleTriggered180_ = true;
                    triggerNow = true;
                }
                if (triggerNow) {
                    const uint16_t sid = (config_.team == Team::Red) ? 9 : 109;
                    const std::array<uint8_t, 6> zeroKey{};
                    std::string err;
                    for (uint8_t cmd : {uint8_t(1), uint8_t(2)}) {
                        std::vector<uint8_t> dpkt;
                        if (BuildRadarCmdPacket(seq, sid, cmd, 0, zeroKey, &dpkt, &err)) {
                            serial_->Write(dpkt, &err);
                            std::ostringstream oss;
                            oss << "DOUBLE cmd=" << static_cast<int>(cmd)
                                << " chance=" << chance
                                << " remain=" << remain << "s";
                            UpdateSerialStatus(&tx0121_, seq, oss.str());
                            ++seq;
                        }
                    }
                }
            }
        }

        ++seq;
        const auto nowAfterLoop = std::chrono::steady_clock::now();
        if (nextTick <= nowAfterLoop) nextTick = nowAfterLoop;
        std::this_thread::sleep_until(nextTick);
    }
}

// ============================================================================
// SerialReceiveLoop — 串口接收线程
// 循环读取原始字节 → 环形缓冲区 → 扫描 0xA5 帧头 → 白名单过滤 → 入队 parseQueue_
// ============================================================================
void RadarApp::SerialReceiveLoop() {
    SerialByteRingBuffer buffer(kMaxSerialBufferBytes);  // 32KB 环形缓冲区
    std::array<uint8_t, 2048> chunk{};

    while (running_.load(std::memory_order_relaxed)) {
        if (serial_ == nullptr || !serial_->IsOpen()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        std::string error;
        const ssize_t n = serial_->Read(chunk.data(), chunk.size(), &error);
        if (n < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        if (n == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }

        buffer.push(chunk.data(), static_cast<std::size_t>(n));

        while (buffer.size() >= 9U) {
            std::size_t cursor = 0U;
            while (cursor < buffer.size() && buffer[cursor] != 0xA5) {
                ++cursor;
            }
            if (cursor > 0U) {
                buffer.erase_prefix(cursor);
                continue;
            }
            if (buffer.size() < 9U) {
                break;
            }

            const uint16_t dataLen = static_cast<uint16_t>(buffer[1U]) | (static_cast<uint16_t>(buffer[2U]) << 8U);
            if (dataLen > kMaxSerialPayloadLen) {
                buffer.erase_prefix(1U);
                continue;
            }

            const std::size_t totalLen = 7U + static_cast<std::size_t>(dataLen) + 2U;
            if (buffer.size() < totalLen) {
                break;
            }

            // 早期过滤：检查 cmdId（位于 buffer[5] 和 buffer[6]），丢弃非目标帧。
            const uint16_t cmdId = static_cast<uint16_t>(buffer[5U]) | (static_cast<uint16_t>(buffer[6U]) << 8U);
            if (!IsIncomingSerialCmd(cmdId)) {
                buffer.erase_prefix(totalLen);
                continue;
            }

            std::vector<uint8_t> frame;
            frame.reserve(totalLen);
            for (std::size_t i = 0; i < totalLen; ++i) {
                frame.push_back(buffer[i]);
            }

            // 把完整帧放入解析队列，由后台 worker 继续处理。
            {
                std::lock_guard<std::mutex> lk(parseQueueMu_);
                parseQueue_.push_back(std::move(frame));
            }
            parseQueueCv_.notify_one();
            buffer.erase_prefix(totalLen);
        }
    }
}

// ============================================================================
// ParserWorker — 协议解析线程（线程池，通常 N≥1 个）
// 从 parseQueue_ 取帧 → ParsePacket(CRC校验) → 按 cmdId 分发处理
// 处理结果写入原子变量，供 SerialSendLoop / TcpSendLoop 读取
// ============================================================================
void RadarApp::ParserWorker() {
    while (parserRunning_.load(std::memory_order_relaxed)) {
        std::vector<uint8_t> frame;
        {
            std::unique_lock<std::mutex> lk(parseQueueMu_);
            parseQueueCv_.wait(lk, [&](){ return !parseQueue_.empty() || !parserRunning_.load(); });
            if (!parserRunning_.load() && parseQueue_.empty()) break;
            if (!parseQueue_.empty()) { frame = std::move(parseQueue_.front()); parseQueue_.pop_front(); }
        }
        if (frame.empty()) continue;
        
        const auto parsed = ParsePacket(frame);
        if (!parsed.has_value()) continue;

        // 只处理我们关心的接收命令。
        if (parsed->cmdId == 0x020E && parsed->payload.size() == 1U) {
            const RadarInfoBits bits = DecodeRadarInfoByte(parsed->payload[0]);
            doubleVulnerabilityChance_.store(bits.hasOpportunity, std::memory_order_relaxed);
            opponentDoubleTriggered_.store(bits.enemyTriggered, std::memory_order_relaxed);
            encryptLevel_.store(bits.encryptLevel, std::memory_order_relaxed);
            keyModifiable_.store(bits.keyModifiable, std::memory_order_relaxed);
            hasRadarInfo020E_.store(true, std::memory_order_release);

            doubleTriggerEpoch_.fetch_add(1, std::memory_order_relaxed);  // 每次 0x020E 都递增

            std::string parsedInfo = BuildRadarInfoStatus(parsed->payload[0], bits);
            std::string combined = std::string("cmd=0x020E seq=") + std::to_string(parsed->seq)
                                 + " payload=" + HexDump(parsed->payload) + " | " + parsedInfo;
            UpdateSerialStatus(&rx020E_, parsed->seq, combined);
        } else if (parsed->cmdId == 0x0301) {
            RobotInteractionPositions0301 message;
            std::string error;
            if (!DecodeAllyRobotPositions0301(*parsed, &message, &error)) {
                UpdateSerialStatus(&rx0301_, parsed->seq, std::string("decode failed: ") + error);
                continue;
            }
            std::ostringstream oss;
            oss << "cmd=0x0301 seq=" << static_cast<int>(parsed->seq)
                << " dataCmd=0x" << std::hex << std::uppercase << message.dataCmdId
                << " sender=" << std::dec << message.senderId
                << " receiver=" << message.receiverId
                << " hero=(" << message.positions.heroX << "," << message.positions.heroY << ")"
                << " eng=(" << message.positions.engineerX << "," << message.positions.engineerY << ")"
                << " inf3=(" << message.positions.infantry3X << "," << message.positions.infantry3Y << ")"
                << " inf4=(" << message.positions.infantry4X << "," << message.positions.infantry4Y << ")"
                << " rsv0=" << message.positions.reserved0
                << " rsv1=" << message.positions.reserved1;
            UpdateSerialStatus(&rx0301_, parsed->seq, oss.str());

            // 0x0200 子内容：己方机器人真实坐标，直接覆盖到 sendLastSerCoords_
            {
                const bool isRedAlly = (config_.team == Team::Red);
                using P = std::pair<std::string, std::pair<float, float>>;
                const std::vector<P> allyPos = {
                    {isRedAlly ? "R1" : "B1", {message.positions.heroX, message.positions.heroY}},
                    {isRedAlly ? "R2" : "B2", {message.positions.engineerX, message.positions.engineerY}},
                    {isRedAlly ? "R3" : "B3", {message.positions.infantry3X, message.positions.infantry3Y}},
                    {isRedAlly ? "R4" : "B4", {message.positions.infantry4X, message.positions.infantry4Y}},
                };
                for (const auto& [rname, coord] : allyPos) {
                    const auto mapped = MapToSerCoords(rname, coord.first, coord.second);
                    sendLastSerCoords_[rname] = mapped;
                    int slot = RobotNameToIndex(rname);
                    if (slot >= 0) {
                        latestSerCoordX_[slot].store(mapped.first, std::memory_order_relaxed);
                        latestSerCoordY_[slot].store(mapped.second, std::memory_order_relaxed);
                        latestSerCoordValid_[slot].store(true, std::memory_order_release);
                    }
                }
            }
        // 0x0001: 比赛状态，解析game_type(game_progress和stage_remain_time
        } else if (parsed->cmdId == 0x0001) {
            GameStatus gameStatus;
            std::string error;
            if (!DecodeGameStatus0001(*parsed, &gameStatus, &error)) {
                UpdateSerialStatus(&rx0001_, parsed->seq, std::string("decode failed: ") + error);
                continue;
            }
            latestGameType_.store(gameStatus.game_type, std::memory_order_relaxed);
            latestGameProgress_.store(gameStatus.game_progress, std::memory_order_relaxed);
            latestStageRemainTime_.store(gameStatus.stage_remain_time, std::memory_order_relaxed);
            std::ostringstream oss;
            oss << "cmd=0x0001 seq=" << static_cast<int>(parsed->seq)
                << " raw0=0x" << std::hex << std::uppercase << static_cast<int>(parsed->payload[0]) << std::dec
                << " game_type=" << static_cast<int>(gameStatus.game_type)
                << " game_progress=" << static_cast<int>(gameStatus.game_progress)
                << " stage_remain_time=" << gameStatus.stage_remain_time << "s";
            UpdateSerialStatus(&rx0001_, parsed->seq, oss.str());
        // 0x0003: 前哨站血量，解析payload[11-12]两字节
        } else if (parsed->cmdId == 0x0003) {
            OutpostHealth outpostHealth;
            std::string error;
            if (!DecodeOutpostHealth0003(*parsed, &outpostHealth, &error)) {
                UpdateSerialStatus(&rx0003_, parsed->seq, std::string("decode failed: ") + error);
                continue;
            }
            // 检测血量下降（较上一时刻）
            {
                const uint16_t prev = latestOutpostHealth_.load(std::memory_order_relaxed);
                if (outpostHealth.health < prev) {
                    outpostHealthDecreased_.store(true, std::memory_order_relaxed);
                }
            }
            latestOutpostHealth_.store(outpostHealth.health, std::memory_order_relaxed);
            std::ostringstream oss;
            oss << "cmd=0x0003 seq=" << static_cast<int>(parsed->seq)
                << " outpost_health=" << outpostHealth.health;
            UpdateSerialStatus(&rx0003_, parsed->seq, oss.str());
        // 0x0101：能量机关状态，4字节payload
        } else if (parsed->cmdId == 0x0101 && parsed->payload.size() == 4U) {
            const uint8_t b = parsed->payload[0];
            smallEnergyActivated_.store((b >> 3) & 0x03, std::memory_order_relaxed);
            bigEnergyActivated_.store((b >> 5) & 0x03, std::memory_order_relaxed);
            std::ostringstream oss;
            oss << "cmd=0x0101 small=" << ((b >> 3) & 0x03)
                << " big=" << ((b >> 5) & 0x03);
            UpdateSerialStatus(&rx0101_, parsed->seq, oss.str());
        }
    }
}

std::pair<int, int> RadarApp::MapToSerCoords(const std::string& name, float mapX, float mapY) const {
    if (!name.empty() && name[0] == 'R')
        return {static_cast<int>(std::lround(mapY)), static_cast<int>(std::lround(1500.0F - mapX))};
    return {static_cast<int>(std::lround(2800.0F - mapY)), static_cast<int>(std::lround(1500.0F - mapX))};
}

bool RadarApp::ProjectPoint(const cv::Mat& transform, const cv::Point2f& cameraPoint, int* mapX, int* mapY) const {
    std::vector<cv::Point2f> src{cameraPoint};
    std::vector<cv::Point2f> dst;
    cv::perspectiveTransform(src, dst, transform);
    if (dst.empty()) return false;
    *mapX = std::clamp(static_cast<int>(std::lround(dst[0].x)), 0, calibration_.maskWidth);
    *mapY = std::clamp(static_cast<int>(std::lround(dst[0].y)), 0, calibration_.maskHeight);
    return true;
}

void RadarApp::UpdateSerialStatus(SerialStatus* status, uint8_t seq, const std::string& text) {
    if (status == nullptr) return;
    status->count.fetch_add(1, std::memory_order_relaxed);
    status->lastSeq.store(static_cast<int>(seq), std::memory_order_relaxed);
    std::atomic_store_explicit(&status->lastText,
                               std::make_shared<const std::string>(text.empty() ? std::string("N/A") : text),
                               std::memory_order_release);
}

void RadarApp::Run() {
    running_.store(true);
    cv::setUseOptimized(true);
    const int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
    if (config_.opencvThreads > 0) cv::setNumThreads(config_.opencvThreads);
    else if (hwThreads > 0) cv::setNumThreads(hwThreads);

    // 打开相机源并启动异步帧读取
    std::cout << "camera config: mode=" << config_.camera.mode << std::endl;
    if (config_.camera.mode == "daheng") {
        std::cout << "  index=" << config_.camera.dahengDeviceIndex
                  << " size=" << config_.camera.width << "x" << config_.camera.height
                  << " exposure=" << config_.camera.exposureTime
                  << " gain=" << config_.camera.gain
                  << " awb=" << (config_.camera.dahengAutoWhiteBalance ? 1 : 0)
                  << " flip=" << (config_.camera.dahengFlipVertical ? 1 : 0)
                  << std::endl;
    } else if (config_.camera.mode == "video_file") {
        std::cout << "  path=" << config_.camera.videoPath
                  << " size=" << config_.camera.width << "x" << config_.camera.height
                  << std::endl;
    } else if (config_.camera.mode == "test") {
        std::cout << "  snapshot=" << config_.camera.snapshotPath << std::endl;
    }

    // 创建窗口
    cv::namedWindow("img", cv::WINDOW_NORMAL);
    cv::namedWindow("map", cv::WINDOW_NORMAL);
    if (config_.showUi) {
        serialMonitor_.Open();
    }

    // 相机参数调节滑轨（曝光 0~20000 us，增益 0~200 即 0.0~20.0）
    const int initExp = static_cast<int>(config_.camera.exposureTime);
    const int initGain = static_cast<int>(config_.camera.gain * 10.0);
    cv::createTrackbar("Exposure(us)", "img", nullptr, 20000);
    cv::createTrackbar("Gain(x0.1)",   "img", nullptr, 200);
    cv::setTrackbarPos("Exposure(us)", "img", std::clamp(initExp, 0, 20000));
    cv::setTrackbarPos("Gain(x0.1)",   "img", std::clamp(initGain, 0, 200));

    // 启动抓帧线程
    std::thread grabThread([&]() {
        if (config_.camera.mode == "test") {
            cv::Mat testImage = cv::imread(config_.camera.snapshotPath, cv::IMREAD_COLOR);
            if (testImage.empty()) {
                std::cerr << "failed to read test image: " << config_.camera.snapshotPath << std::endl;
                running_.store(false, std::memory_order_relaxed);
                return;
            }
            std::lock_guard<std::mutex> lk(frameMutex_);
            latestFrame_ = testImage;
            frameReady_.store(true, std::memory_order_release);

            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                std::lock_guard<std::mutex> lk2(frameMutex_);
                if (!frameReady_.load(std::memory_order_acquire)) {
                    latestFrame_ = testImage.clone();
                    frameReady_.store(true, std::memory_order_release);
                }
            }
            return;
        }

        if (config_.camera.mode == "video_file") {
            cv::VideoCapture cap;
            if (!cap.open(config_.camera.videoPath)) {
                std::cerr << "failed to open video file: " << config_.camera.videoPath << std::endl;
                running_.store(false, std::memory_order_relaxed);
                return;
            }

            while (running_) {
                cv::Mat frame;
                if (!cap.read(frame) || frame.empty()) {
                    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                    continue;
                }
                std::lock_guard<std::mutex> lk(frameMutex_);
                std::swap(latestFrame_, frame);
                frameReady_.store(true, std::memory_order_release);
            }
            cap.release();
            return;
        }

        // daheng mode
        radar26::DahengCamera dahengCamera;
        radar26::DahengCameraOptions options;
        options.deviceIndex = config_.camera.dahengDeviceIndex;
        options.width = config_.camera.width;
        options.height = config_.camera.height;
        options.exposureTimeUs = config_.camera.exposureTime;
        options.gain = config_.camera.gain;
        options.autoWhiteBalance = config_.camera.dahengAutoWhiteBalance;
        options.flipVertical = config_.camera.dahengFlipVertical;

        std::string error;
        if (!dahengCamera.Open(options, &error)) {
            std::cerr << "failed to open daheng camera: " << error << std::endl;
            running_.store(false, std::memory_order_relaxed);
            return;
        }

        double curExp = targetExposure_.load(std::memory_order_relaxed);
        double curGain = targetGain_.load(std::memory_order_relaxed);

        while (running_) {
            cv::Mat frame;
            if (!dahengCamera.Read(&frame, &error)) {
                continue;
            }

            // 轮询相机参数变更并应用到硬件
            const double newExp = targetExposure_.load(std::memory_order_relaxed);
            const double newGain = targetGain_.load(std::memory_order_relaxed);
            if (newExp != curExp) {
                std::string err;
                dahengCamera.SetExposureTime(newExp, &err);
                curExp = newExp;
            }
            if (newGain != curGain) {
                std::string err;
                dahengCamera.SetGain(newGain, &err);
                curGain = newGain;
            }

            std::lock_guard<std::mutex> lk(frameMutex_);
            std::swap(latestFrame_, frame);
            frameReady_.store(true, std::memory_order_release);
        }

        dahengCamera.Close();
    });

    // 地图预处理
    cv::Mat mapBaseShow;
    float mapScaleX = 1.0F, mapScaleY = 1.0F, mapScaleMin = 1.0F;
    mapBaseShow = ResizeForDisplay(calibration_.mapImage, 1400, 820);
    if (mapBaseShow.empty()) mapBaseShow = calibration_.mapImage.clone();
    if (!calibration_.mapImage.empty()) {
        mapScaleX = static_cast<float>(mapBaseShow.cols) / calibration_.mapImage.cols;
        mapScaleY = static_cast<float>(mapBaseShow.rows) / calibration_.mapImage.rows;
        mapScaleMin = std::max(0.1F, std::min(mapScaleX, mapScaleY));
    }

    while (running_) {
        cv::Mat frame;
        bool hasNew = false;
        {
            std::lock_guard<std::mutex> lk(frameMutex_);
            if (frameReady_.load(std::memory_order_acquire)) {
                // move latestFrame_ into local frame to avoid per-frame deep copy
                std::swap(frame, latestFrame_);
                frameReady_.store(false, std::memory_order_release);
                hasNew = true;
            }
        }
        if (!hasNew) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        
        cv::Mat imgShow;
        float imgScaleX = 1.0F, imgScaleY = 1.0F;
        imgShow = ResizeForDisplay(frame, 1400, 820);
        if (imgShow.empty()) imgShow = frame.clone();
        if (!imgShow.empty()) {
            imgScaleX = static_cast<float>(imgShow.cols) / frame.cols;
            imgScaleY = static_cast<float>(imgShow.rows) / frame.rows;
        }

        std::vector<Detection> carDetections;
        std::string err;
        const auto carInferStart = std::chrono::steady_clock::now();
        if (!carDetector_.Infer(frame, config_.detection.carConf, config_.detection.carIou,
                                config_.detection.carMaxDet, &carDetections, &err)) {
            if (config_.debug) std::cerr << "car detector infer failed: " << err << std::endl;
            continue;
        }
        const auto carInferEnd = std::chrono::steady_clock::now();
        const double carInferMs = std::chrono::duration<double, std::milli>(carInferEnd - carInferStart).count();

        std::vector<std::pair<int, cv::Rect>> candidateRois; candidateRois.reserve(carDetections.size());
        for (const auto& det : carDetections) {
            if (det.className != "car") continue;
            cv::Rect roi = ClampRect(det.box, frame.cols, frame.rows);
            if (roi.empty()) continue;
            const int area = roi.width * roi.height;
            candidateRois.emplace_back(area, roi);
        }
        const std::size_t maxCarsToProcess = 6;
        std::sort(candidateRois.begin(), candidateRois.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<cv::Rect> armorRois;
        std::vector<cv::Mat> armorInputs;
        for (std::size_t i = 0; i < candidateRois.size() && i < maxCarsToProcess; ++i) {
            const cv::Rect& roi = candidateRois[i].second;
            if (config_.showUi && !imgShow.empty()) {
                cv::Rect show = ClampRect(cv::Rect2f(roi.x * imgScaleX, roi.y * imgScaleY,
                                                     roi.width * imgScaleX, roi.height * imgScaleY),
                                          imgShow.cols, imgShow.rows);
                if (!show.empty()) {
                    cv::rectangle(imgShow, show, cv::Scalar(0, 255, 0), 1);
                    cv::putText(imgShow, "car", cv::Point(show.x, std::max(0, show.y - 4)),
                                cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 255, 0), 1);
                }
            }
            armorRois.push_back(roi);
            armorInputs.push_back(frame(roi));
        }

        std::vector<std::vector<Detection>> armorBatchDetections;
        double armorInferMs = 0.0;
        if (!armorInputs.empty()) {
            const auto armorInferStart = std::chrono::steady_clock::now();
            if (armorInputs.size() == 1 || !armorDetector_.SupportsDynamicBatch()) {
                armorBatchDetections.resize(armorInputs.size());
                for (std::size_t i = 0; i < armorInputs.size(); ++i)
                    armorDetector_.Infer(armorInputs[i], config_.detection.armorConf, config_.detection.armorIou,
                                         config_.detection.armorMaxDet, &armorBatchDetections[i], &err);
            } else {
                armorDetector_.InferBatch(armorInputs, config_.detection.armorConf, config_.detection.armorIou,
                                          config_.detection.armorMaxDet, &armorBatchDetections, &err);
            }
            const auto armorInferEnd = std::chrono::steady_clock::now();
            armorInferMs = std::chrono::duration<double, std::milli>(armorInferEnd - armorInferStart).count();
        }

        // 坐标投影
        for (std::size_t i = 0; i < armorRois.size() && i < armorBatchDetections.size(); ++i) {
            const cv::Rect& roi = armorRois[i];
            const auto& dets = armorBatchDetections[i];
            if (dets.empty()) continue;

            for (const auto& det : dets) {
                if (config_.showUi && !imgShow.empty()) {
                    cv::Rect absRoi = ClampRect(cv::Rect2f(roi.x + det.box.x, roi.y + det.box.y,
                                                           det.box.width, det.box.height),
                                                frame.cols, frame.rows);
                    if (!absRoi.empty()) {
                        cv::Rect showRoi = ClampRect(cv::Rect2f(absRoi.x * imgScaleX, absRoi.y * imgScaleY,
                                                                absRoi.width * imgScaleX, absRoi.height * imgScaleY),
                                                     imgShow.cols, imgShow.rows);
                        if (!showRoi.empty()) {
                            cv::rectangle(imgShow, showRoi, cv::Scalar(0, 255, 255), 1);
                            cv::putText(imgShow, det.className,
                                        cv::Point(showRoi.x, std::max(0, showRoi.y - 4)),
                                        cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 255, 255), 1);
                        }
                    }
                }

                std::string name = NormalizeRobotName(det.className);
                if (!IsKnownRobot(name)) continue;
                float cx = roi.x + det.box.x + det.box.width * 0.5F;
                float cy = roi.y + det.box.y + 1.5F * det.box.height;
                cv::Point2f cp(std::min(cx, static_cast<float>(frame.cols - 1)),
                               std::min(cy, static_cast<float>(frame.rows - 1)));

                int mx = 0, my = 0;
                bool added = false;
                if (ProjectPoint(calibration_.MGround, cp, &mx, &my)) {
                    cv::Vec3b c = calibration_.maskImage.at<cv::Vec3b>(my, mx);
                    if (c[0] == 0 && c[1] == 0 && c[2] == 0) {
                        filter_->AddData(name, static_cast<float>(mx), static_cast<float>(my));
                        added = true;
                    } else {
                        int tx = 0, ty = 0;
                        if (ProjectPoint(calibration_.MHeightR, cp, &tx, &ty)) {
                            cv::Vec3b c2 = calibration_.maskImage.at<cv::Vec3b>(ty, tx);
                            if (c2[1] > c2[0] && c2[1] > c2[2]) {
                                mx = tx; my = ty;
                                filter_->AddData(name, static_cast<float>(mx), static_cast<float>(my));
                                added = true;
                            }
                        }
                        if (!added && ProjectPoint(calibration_.MHeightG, cp, &tx, &ty)) {
                            cv::Vec3b c3 = calibration_.maskImage.at<cv::Vec3b>(ty, tx);
                            if (c3[0] > c3[1] && c3[0] > c3[2]) {
                                mx = tx; my = ty;
                                filter_->AddData(name, static_cast<float>(mx), static_cast<float>(my));
                                added = true;
                            }
                        }
                        if (!added) {
                            filter_->AddData(name, static_cast<float>(mx), static_cast<float>(my));
                            added = true;
                        }
                    }
                }

                // 视觉定位坐标写入 sendLastSerCoords_（供地图绘制 + 0x0305发送）
                if (added) {
                    auto ser = MapToSerCoords(name, static_cast<float>(mx), static_cast<float>(my));
                    sendLastSerCoords_[name] = ser;
                }
            }
        }

        // 地图绘制：显示所有12个校验过的机器人位置坐标（来自 latestSerCoord*）
        cv::Mat mapShow;
        if (config_.showUi) {
            mapShow = mapBaseShow.clone();
            const int r = std::max(4, static_cast<int>(std::lround(10.0F * mapScaleMin)));
            const double nameScale = std::max(0.55, 1.6 * static_cast<double>(mapScaleMin));
            const int nameThick = std::max(1, static_cast<int>(std::lround(2.2F * mapScaleMin)));
            for (std::size_t slot = 0; slot < kAllRobotNames.size(); ++slot) {
                if (!latestSerCoordValid_[slot].load(std::memory_order_acquire)) continue;
                std::string name = kAllRobotNames[slot];
                int serX = latestSerCoordX_[slot].load(std::memory_order_relaxed);
                int serY = latestSerCoordY_[slot].load(std::memory_order_relaxed);

                // serial坐标 → map坐标
                float mx, my;
                if (name[0] == 'R') { my = static_cast<float>(serX); mx = 1500.0F - static_cast<float>(serY); }
                else                { my = 2800.0F - static_cast<float>(serX); mx = 1500.0F - static_cast<float>(serY); }

                // map坐标 → 地图显示坐标
                float sx, sy;
                if (config_.team == Team::Red) { sx = 2800.0F - my; sy = mx; }
                else                           { sx = my; sy = 1500.0F - mx; }
                sx *= mapScaleX; sy *= mapScaleY;

                cv::Scalar col = TeamColor(name);
                cv::circle(mapShow, cv::Point(static_cast<int>(sx), static_cast<int>(sy)), r, col, -1);
                std::ostringstream coordText;
                coordText << name << " (" << static_cast<int>(mx) << "," << static_cast<int>(my) << ")";
                const double labelScale = std::max(0.35, 0.5 * mapScaleMin);
                const int labelThick = 1;
                cv::putText(mapShow, coordText.str(),
                            cv::Point(static_cast<int>(sx) + 6, static_cast<int>(sy) - 6),
                            cv::FONT_HERSHEY_SIMPLEX, labelScale, cv::Scalar(255, 255, 255), labelThick);
            }
        }

        // 串口监视窗口
        if (config_.showUi) {
            serialMonitor_.Update(tx0305_, tx0121_, tx0301_, rx020E_, rx0301_, rx0001_, rx0003_, rx0101_,
                                  tcp01_, tcpRx8001_, tcpRx8002_, tcpRx8003_);
        }

        if (config_.showUi) {
            cv::putText(imgShow, "Q: quit", cv::Point(imgShow.cols - 150, 28),
                        cv::FONT_HERSHEY_SIMPLEX, 0.60, cv::Scalar(0, 255, 0), 2);
            cv::imshow("img", imgShow);
            cv::imshow("map", mapShow);
        }

        int key = cv::waitKey(1) & 0xFF;
        if (key == 'q') { running_ = false; break; }

        // 读取相机参数滑轨，变更时更新原子变量并写回 app.yaml
        if (config_.showUi) {
            const int trackExp = cv::getTrackbarPos("Exposure(us)", "img");
            const int trackGain = cv::getTrackbarPos("Gain(x0.1)", "img");
            const double newExp = static_cast<double>(std::clamp(trackExp, 0, 20000));
            const double newGain = static_cast<double>(std::clamp(trackGain, 0, 200)) / 10.0;
            targetExposure_.store(newExp, std::memory_order_relaxed);
            targetGain_.store(newGain, std::memory_order_relaxed);
            if (newExp != lastSavedExposure_ || newGain != lastSavedGain_) {
                lastSavedExposure_ = newExp;
                lastSavedGain_ = newGain;
                config_.camera.exposureTime = newExp;
                config_.camera.gain = newGain;
                SaveCameraParams();
            }
        }

        auto tNow = std::chrono::steady_clock::now();
    }

    running_ = false;
    if (grabThread.joinable()) grabThread.join();
    cv::destroyAllWindows();
}

// 将当前 exposure_time / gain 写回 app.yaml（行级替换，保留格式与注释）
void RadarApp::SaveCameraParams() const {
    std::ifstream ifs(configPath_);
    if (!ifs.is_open()) return;
    std::vector<std::string> lines;
    std::string line;
    bool inCamera = false;
    while (std::getline(ifs, line)) {
        // 检测 camera: 节开始 / 下一节结束
        if (!inCamera && line.find("camera:") != std::string::npos && line.find_first_not_of(" \t") == 0) {
            inCamera = true;
        } else if (inCamera && !line.empty() && (line[0] != ' ' && line[0] != '\t')) {
            inCamera = false;  // 回到顶层键，退出 camera 节
        }
        if (inCamera) {
            // 匹配 "  exposure_time: <value>"
            auto expPos = line.find("exposure_time:");
            if (expPos != std::string::npos && expPos > 0 && line[expPos - 1] == ' ') {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << config_.camera.exposureTime;
                auto valStart = line.find_first_of("0123456789", expPos);
                auto valEnd   = line.find_first_not_of("0123456789.", valStart);
                if (valStart != std::string::npos) {
                    line.replace(valStart, (valEnd == std::string::npos ? line.size() : valEnd) - valStart, oss.str());
                }
            }
            // 匹配 "  gain: <value>"
            auto gainPos = line.find("gain:");
            if (gainPos != std::string::npos && gainPos > 0 && line[gainPos - 1] == ' ') {
                // 确保不是 "gain" 子串（如 "daheng_auto_white_balance" 中的）
                if (gainPos < 3 || line.substr(0, gainPos).find_first_not_of(" \t") == std::string::npos
                    || line[gainPos - 1] != '_') {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(1) << config_.camera.gain;
                    auto valStart = line.find_first_of("0123456789", gainPos);
                    auto valEnd   = line.find_first_not_of("0123456789.", valStart);
                    if (valStart != std::string::npos) {
                        line.replace(valStart, (valEnd == std::string::npos ? line.size() : valEnd) - valStart, oss.str());
                    }
                }
            }
        }
        lines.push_back(line);
    }
    ifs.close();

    std::ofstream ofs(configPath_, std::ios::trunc);
    if (!ofs.is_open()) return;
    for (const auto& l : lines) ofs << l << '\n';
}

}  // namespace radar26
