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
#include <deque>

namespace radar26 {
namespace {

constexpr std::array<const char*, 12> kAllRobotNames = {
    "R1", "R2", "R3", "R4", "R6", "R7", "B1", "B2", "B3", "B4", "B6", "B7",
};

constexpr int kFixedSerialSendPeriodMs = 200;
constexpr std::size_t kMaxSerialPayloadLen = 512;
constexpr std::size_t kMaxSerialBufferBytes = 32768;
constexpr uint16_t kIncomingCmd020E = 0x020E;
constexpr uint16_t kIncomingCmd0301 = 0x0301;

bool IsIncomingSerialCmd(uint16_t cmdId) {
    return cmdId == kIncomingCmd020E || cmdId == kIncomingCmd0301;
}

class SerialByteRingBuffer {
public:
    explicit SerialByteRingBuffer(std::size_t capacity) : data_(capacity) {}

    std::size_t size() const { return size_; }

    uint8_t operator[](std::size_t index) const {
        return data_[(head_ + index) % data_.size()];
    }

    void push(const uint8_t* bytes, std::size_t count) {
        if (count == 0U || data_.empty()) {
            return;
        }

        if (count >= data_.size()) {
            std::copy(bytes + (count - data_.size()), bytes + count, data_.begin());
            head_ = 0U;
            size_ = data_.size();
            return;
        }

        if (count > data_.size() - size_) {
            erase_prefix(count - (data_.size() - size_));
        }

        const std::size_t tail = (head_ + size_) % data_.size();
        const std::size_t first = std::min(count, data_.size() - tail);
        std::copy(bytes, bytes + first, data_.begin() + static_cast<std::ptrdiff_t>(tail));
        if (count > first) {
            std::copy(bytes + first, bytes + count, data_.begin());
        }
        size_ += count;
    }

    void erase_prefix(std::size_t count) {
        if (count == 0U) {
            return;
        }
        if (count >= size_) {
            head_ = 0U;
            size_ = 0U;
            return;
        }
        head_ = (head_ + count) % data_.size();
        size_ -= count;
    }

private:
    std::vector<uint8_t> data_;
    std::size_t head_ = 0U;
    std::size_t size_ = 0U;
};

std::string KeyToString(const std::array<uint8_t, 6>& key) {
    std::string out;
    out.reserve(key.size());
    for (const uint8_t b : key) out.push_back(static_cast<char>(b));
    return out;
}

std::string BuildCoordsStatus(const std::vector<std::string>& robotOrder, const std::vector<uint16_t>& coords) {
    std::ostringstream oss;
    const std::size_t count = std::min(robotOrder.size(), coords.size() / 2U);
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0U && (i % 4U) == 0U) oss << "\n";
        else if (i != 0U) oss << "  ";
        oss << robotOrder[i] << "(" << coords[i * 2U] << "," << coords[i * 2U + 1U] << ")";
    }
    return oss.str();
}

std::string BuildRadarCmdStatus(uint8_t radarCmd, uint8_t passwordCmd, const std::array<uint8_t, 6>& key) {
    std::ostringstream oss;
    oss << "radar_cmd=" << static_cast<int>(radarCmd)
        << " password_cmd=" << static_cast<int>(passwordCmd)
        << " key=" << KeyToString(key);
    return oss.str();
}

std::string BuildRadarInfoStatus(uint8_t radarInfo, const RadarInfoBits& bits) {
    std::ostringstream oss;
    oss << "radar_info=" << static_cast<int>(radarInfo)
        << " chance=" << static_cast<int>(bits.hasOpportunity)
        << " enemy_triggered=" << static_cast<int>(bits.enemyTriggered)
        << " encrypt=" << static_cast<int>(bits.encryptLevel)
        << " key_mod=" << static_cast<int>(bits.keyModifiable);
    return oss.str();
}

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

int RobotNameToIndex(const std::string& name) {
    for (std::size_t i = 0; i < kAllRobotNames.size(); ++i)
        if (name == kAllRobotNames[i]) return static_cast<int>(i);
    return -1;
}

std::string NormalizeRobotName(const std::string& input) {
    if (input == "R5") return "R6";
    if (input == "B5") return "B6";
    return input;
}

bool IsKnownRobot(const std::string& name) {
    return std::find(kAllRobotNames.begin(), kAllRobotNames.end(), name) != kAllRobotNames.end();
}

std::vector<std::string> BuildRobotOrder(Team team) {
    if (team == Team::Red)
        return {"B1", "B2", "B3", "B4", "B6", "B7", "R1", "R2", "R3", "R4", "R6", "R7"};
    return {"R1", "R2", "R3", "R4", "R6", "R7", "B1", "B2", "B3", "B4", "B6", "B7"};
}

cv::Rect ClampRect(const cv::Rect2f& r, int width, int height) {
    int x = std::max(0, static_cast<int>(std::floor(r.x)));
    int y = std::max(0, static_cast<int>(std::floor(r.y)));
    int w = std::max(0, static_cast<int>(std::ceil(r.width)));
    int h = std::max(0, static_cast<int>(std::ceil(r.height)));
    if (x >= width || y >= height) return cv::Rect();
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 1 || h <= 1) return cv::Rect();
    return cv::Rect(x, y, w, h);
}

uint16_t ClipToU16(int v) {
    if (v < 0) return 0;
    if (v > 65535) return 65535;
    return static_cast<uint16_t>(v);
}

std::array<uint8_t, 6> KeyToArray(const std::string& key) {
    std::array<uint8_t, 6> out{};
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint8_t>(key[i]);
    return out;
}

cv::Scalar TeamColor(const std::string& name) {
    return name[0] == 'R' ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 0, 0);
}

cv::Mat ResizeForDisplay(const cv::Mat& src, int maxWidth, int maxHeight) {
    if (src.empty() || maxWidth <= 0 || maxHeight <= 0) return src;
    if (src.cols <= maxWidth && src.rows <= maxHeight) return src;
    const double sx = static_cast<double>(maxWidth) / src.cols;
    const double sy = static_cast<double>(maxHeight) / src.rows;
    const double scale = std::max(1e-6, std::min(sx, sy));
    cv::Mat dst;
    cv::resize(src, dst, cv::Size(), scale, scale, cv::INTER_AREA);
    return dst;
}

}  // namespace

RadarApp::RadarApp(AppConfig config) : config_(std::move(config)) {}

RadarApp::~RadarApp() {
    StopSerialThreads();
    if (serial_) serial_->Close();
}

bool RadarApp::Initialize(std::string* error) {
    if (!ConfigLoader::LoadCalibration(config_, &calibration_, error)) return false;

    filter_ = std::make_unique<TargetFilter>(1, config_.detection.filterTimeoutSec);
    InitializeGuessTable();

    for (const auto* name : kAllRobotNames) sendLastSerCoords_[name] = std::nullopt;
    for (std::size_t i = 0; i < kAllRobotNames.size(); ++i) {
        latestSerCoordX_[i].store(0, std::memory_order_relaxed);
        latestSerCoordY_[i].store(0, std::memory_order_relaxed);
        latestSerCoordValid_[i].store(false, std::memory_order_relaxed);
    }

    const std::string randomKey = GenerateRandomKey(6);
    if (randomKey.size() != 6) { if (error) *error = "failed to generate random key"; return false; }
    currentKey_ = KeyToArray(randomKey);

    if (!carDetector_.Load(config_.model.carEnginePath, config_.model.carClassNames,
                           config_.model.inputWidth, config_.model.inputHeight, error)) return false;
    if (!armorDetector_.Load(config_.model.armorEnginePath, config_.model.armorClassNames,
                             config_.model.inputWidth, config_.model.inputHeight, error)) return false;

    if (config_.serial.enable) {
        if (!OpenSerial(error)) return false;
        StartSerialThreads();
    }
    return true;
}

void RadarApp::InitializeGuessTable() {
    const auto now = std::chrono::steady_clock::now();
    guessTable_.clear(); guessIndex_.clear(); guessLastSwitch_.clear();
    guessTable_["R1"] = {cv::Point2f(1100.0F, 1400.0F), cv::Point2f(900.0F, 1400.0F)};
    guessTable_["R2"] = {cv::Point2f(870.0F, 1100.0F), cv::Point2f(1340.0F, 680.0F)};
    guessTable_["R7"] = {cv::Point2f(560.0F, 630.0F), cv::Point2f(560.0F, 870.0F)};
    guessTable_["B1"] = {cv::Point2f(1700.0F, 100.0F), cv::Point2f(1900.0F, 100.0F)};
    guessTable_["B2"] = {cv::Point2f(1930.0F, 400.0F), cv::Point2f(1460.0F, 820.0F)};
    guessTable_["B7"] = {cv::Point2f(2240.0F, 870.0F), cv::Point2f(2240.0F, 603.0F)};
    for (const auto& kv : guessTable_) {
        guessIndex_[kv.first] = 0;
        guessLastSwitch_[kv.first] = now;
    }
}

bool RadarApp::OpenSerial(std::string* error) {
    serial_ = std::make_unique<SerialPort>();
    if (!serial_->Open(config_.serial.port, config_.serial.baudrate, error)) return false;
    std::cout << "Serial opened: " << config_.serial.port << " @" << config_.serial.baudrate << std::endl;
    return true;
}

void RadarApp::StartSerialThreads() {
    running_.store(true);
    sendThread_ = std::thread(&RadarApp::SerialSendLoop, this);
    recvThread_ = std::thread(&RadarApp::SerialReceiveLoop, this);

    // start parser workers
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

    // stop parser threads
    parserRunning_.store(false);
    parseQueueCv_.notify_all();
    for (auto &t : parserThreads_) if (t.joinable()) t.join();
    parserThreads_.clear();
}

void RadarApp::SerialSendLoop() {
    uint8_t seq = 0;
    auto lastCmdTime = std::chrono::steady_clock::now();
    uint32_t handledDoubleEpoch = doubleTriggerEpoch_.load(std::memory_order_relaxed);
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
        for (auto& kv : guessIndex_) {
            const std::string& name = kv.first;
            const auto elapsed = now - guessLastSwitch_[name];
            if (elapsed >= std::chrono::duration<double>(config_.guessSwitchIntervalSec)) {
                kv.second = 1 - kv.second;
                guessLastSwitch_[name] = now;
            }
        }

        const std::map<std::string, cv::Point2f> allData = filter_->GetAllData();
        const std::vector<std::string> robotOrder = BuildRobotOrder(config_.team);
        std::vector<uint16_t> coords;
        coords.reserve(24);

        for (const std::string& name : robotOrder) {
            int sx = 0, sy = 0;
            auto dataIt = allData.find(name);
            if (dataIt != allData.end()) {
                const auto mapped = MapToSerCoords(name, dataIt->second.x, dataIt->second.y);
                sx = mapped.first; sy = mapped.second;
                sendLastSerCoords_[name] = mapped;
            } else {
                auto guessIt = guessTable_.find(name);
                if (guessIt != guessTable_.end()) {
                    const int idx = guessIndex_[name];
                    const cv::Point2f pred = guessIt->second[static_cast<std::size_t>(idx)];
                    const auto mapped = MapToSerCoords(name, pred.x, pred.y);
                    sx = mapped.first; sy = mapped.second;
                    sendLastSerCoords_[name] = mapped;
                } else {
                    const auto lastIt = sendLastSerCoords_.find(name);
                    if (lastIt != sendLastSerCoords_.end() && lastIt->second.has_value()) {
                        sx = lastIt->second->first; sy = lastIt->second->second;
                    }
                }
            }

            const uint16_t sxU16 = ClipToU16(sx);
            const uint16_t syU16 = ClipToU16(sy);
            coords.push_back(sxU16);
            coords.push_back(syU16);

            const int slot = RobotNameToIndex(name);
            if (slot >= 0) {
                latestSerCoordX_[static_cast<std::size_t>(slot)].store(static_cast<int>(sxU16), std::memory_order_relaxed);
                latestSerCoordY_[static_cast<std::size_t>(slot)].store(static_cast<int>(syU16), std::memory_order_relaxed);
                latestSerCoordValid_[static_cast<std::size_t>(slot)].store(true, std::memory_order_release);
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
            const uint16_t receiverId = (config_.team == Team::Red) ? 7 : 107;
            std::vector<uint8_t> customPacket;
            if (BuildRadarCustomData0301Packet(seq, senderId, receiverId, &customPacket, &error)) {
                if (serial_->Write(customPacket, &error)) {
                    std::string sentText = HexDump(customPacket) + " | sender=" + std::to_string(senderId)
                                           + " receiver=" + std::to_string(receiverId);
                    UpdateSerialStatus(&tx0301_, seq, sentText);
                }
            }
        }

        if (hasRadarInfo020E_.load(std::memory_order_acquire)) {
            const int safeChance = std::max(0, doubleVulnerabilityChance_.load(std::memory_order_relaxed));
            const int safeTriggered = std::max(0, opponentDoubleTriggered_.load(std::memory_order_relaxed));
            const int safeEncrypt = std::max(0, encryptLevel_.load(std::memory_order_relaxed));
            const int safeKeyMod = std::max(0, keyModifiable_.load(std::memory_order_relaxed));
            const uint8_t radarInfo = static_cast<uint8_t>((safeChance & 0x03) | ((safeTriggered & 0x01) << 2) |
                                                           ((safeEncrypt & 0x03) << 3) | ((safeKeyMod & 0x01) << 5));
            const RadarCmdPlan plan = GetRadarCmdSendPlan(radarInfo, true, config_.debugRequestKeyUpdate, config_.debugRequestEnemyKey);
            const uint32_t observedDoubleEpoch = doubleTriggerEpoch_.load(std::memory_order_relaxed);
            const bool immediateDoubleTrigger = plan.sendDouble && canTriggerDoubleNow_.load(std::memory_order_relaxed) &&
                                                (observedDoubleEpoch != handledDoubleEpoch);
            const bool periodicWindowReached = (now - lastCmdTime) >= std::chrono::seconds(10);
            const bool shouldSendNow = plan.shouldSend && (immediateDoubleTrigger || (periodicWindowReached && (!plan.sendDouble || (plan.passwordCmd == 1U || plan.passwordCmd == 2U))));

            if (shouldSendNow) {
                std::vector<uint8_t> cmdPacket;
                const uint8_t radarCmdToSend = chanceCounter_;
                if (BuildRadarCmdPacket(seq, radarCmdToSend, plan.passwordCmd, currentKey_, &cmdPacket, &error)) {
                    if (serial_->Write(cmdPacket, &error)) {
                            {
                                std::string sentText = HexDump(cmdPacket) + " | " + BuildRadarCmdStatus(radarCmdToSend, plan.passwordCmd, currentKey_);
                                UpdateSerialStatus(&tx0121_, seq, sentText);
                            }
                        if (immediateDoubleTrigger) handledDoubleEpoch = observedDoubleEpoch;
                        chanceCounter_++;
                        if (chanceCounter_ == 0) chanceCounter_ = 1;
                        lastCmdTime = now;
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

void RadarApp::SerialReceiveLoop() {
    SerialByteRingBuffer buffer(kMaxSerialBufferBytes);
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

            // 早期过滤：检查 cmdId（位于 buffer[5] 和 buffer[6]），丢弃非目标帧
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

            // enqueue frame for parser workers (move to queue under lock)
            {
                std::lock_guard<std::mutex> lk(parseQueueMu_);
                parseQueue_.push_back(std::move(frame));
            }
            parseQueueCv_.notify_one();
            buffer.erase_prefix(totalLen);
        }
    }
}

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

        // process only incoming commands of interest
        if (parsed->cmdId == 0x020E && parsed->payload.size() == 1U) {
            const RadarInfoBits bits = DecodeRadarInfoByte(parsed->payload[0]);
            doubleVulnerabilityChance_.store(bits.hasOpportunity, std::memory_order_relaxed);
            opponentDoubleTriggered_.store(bits.enemyTriggered, std::memory_order_relaxed);
            encryptLevel_.store(bits.encryptLevel, std::memory_order_relaxed);
            keyModifiable_.store(bits.keyModifiable, std::memory_order_relaxed);
            hasRadarInfo020E_.store(true, std::memory_order_release);

            const bool canTriggerNow = (bits.hasOpportunity > 0U) && (bits.enemyTriggered == 0U);
            const bool wasTriggerable = canTriggerDoubleNow_.exchange(canTriggerNow, std::memory_order_relaxed);
            if (canTriggerNow && !wasTriggerable) doubleTriggerEpoch_.fetch_add(1, std::memory_order_relaxed);

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

bool RadarApp::UpdateRadarInfoFromParsedPacket(const ParsedPacket& parsed) {
    if (parsed.cmdId == 0x020E && parsed.payload.size() == 1U) {
        const RadarInfoBits bits = DecodeRadarInfoByte(parsed.payload[0]);
        doubleVulnerabilityChance_.store(bits.hasOpportunity, std::memory_order_relaxed);
        opponentDoubleTriggered_.store(bits.enemyTriggered, std::memory_order_relaxed);
        encryptLevel_.store(bits.encryptLevel, std::memory_order_relaxed);
        keyModifiable_.store(bits.keyModifiable, std::memory_order_relaxed);
        hasRadarInfo020E_.store(true, std::memory_order_release);

        const bool canTriggerNow = (bits.hasOpportunity > 0U) && (bits.enemyTriggered == 0U);
        const bool wasTriggerable = canTriggerDoubleNow_.exchange(canTriggerNow, std::memory_order_relaxed);
        if (canTriggerNow && !wasTriggerable) doubleTriggerEpoch_.fetch_add(1, std::memory_order_relaxed);
        // include raw payload hex and parsed info for overlay
        std::string payloadHex = HexDump(parsed.payload);
        std::string parsedInfo = BuildRadarInfoStatus(parsed.payload[0], bits);
        std::string combined = std::string("cmd=0x020E seq=") + std::to_string(parsed.seq) + " payload=" + payloadHex + " | " + parsedInfo;
        UpdateSerialStatus(&rx020E_, parsed.seq, combined);
        return true;
    }
    if (parsed.cmdId == 0x0301) {
        RobotInteractionPositions0301 message;
        std::string error;
        if (!DecodeAllyRobotPositions0301(parsed, &message, &error)) {
            UpdateSerialStatus(&rx0301_, parsed.seq, std::string("decode failed: ") + error);
            return false;
        }

        std::ostringstream oss;
        oss << "cmd=0x0301 seq=" << static_cast<int>(parsed.seq)
            << " dataCmd=0x" << std::hex << std::uppercase << message.dataCmdId
            << " sender=" << std::dec << message.senderId
            << " receiver=" << message.receiverId
            << " hero=(" << message.positions.heroX << "," << message.positions.heroY << ")"
            << " engineer=(" << message.positions.engineerX << "," << message.positions.engineerY << ")";
        UpdateSerialStatus(&rx0301_, parsed.seq, oss.str());
        return true;
    }
    return false;
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
    if (config_.serial.enable) {
        serialMonitor_.Open();
    }

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

        while (running_) {
            cv::Mat frame;
            if (!dahengCamera.Read(&frame, &error)) {
                continue;
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

    int frameCnt = 0, fpsFrameCount = 0;
    double fpsDisplay = 0.0;
    auto fpsStart = std::chrono::steady_clock::now();
    double carInferMsSum = 0.0;
    double armorInferMsSum = 0.0;
    int inferSampleCount = 0;
    auto inferStatStart = std::chrono::steady_clock::now();

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

        carInferMsSum += carInferMs;
        armorInferMsSum += armorInferMs;
        ++inferSampleCount;

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

                if (added) {
                    auto ser = MapToSerCoords(name, static_cast<float>(mx), static_cast<float>(my));
                    int slot = RobotNameToIndex(name);
                    if (slot >= 0) {
                        latestSerCoordX_[static_cast<std::size_t>(slot)].store(ser.first, std::memory_order_relaxed);
                        latestSerCoordY_[static_cast<std::size_t>(slot)].store(ser.second, std::memory_order_relaxed);
                        latestSerCoordValid_[static_cast<std::size_t>(slot)].store(true, std::memory_order_release);
                        latestDetectedType_[static_cast<std::size_t>(slot)] = det.className;
                    }
                }
            }
        }

        // 地图绘制
        auto allFilterData = filter_->GetAllData();
        cv::Mat mapShow;
        if (config_.showUi) {
            mapShow = mapBaseShow.clone();
            const int r = std::max(4, static_cast<int>(std::lround(10.0F * mapScaleMin)));
            const double nameScale = std::max(0.55, 1.6 * static_cast<double>(mapScaleMin));
            const int nameThick = std::max(1, static_cast<int>(std::lround(2.2F * mapScaleMin)));
            for (const auto& kv : allFilterData) {
                float mx = kv.second.x, my = kv.second.y;
                float sx, sy;
                if (config_.team == Team::Red) { sx = 2800.0F - my; sy = mx; }
                else { sx = my; sy = 1500.0F - mx; }
                sx *= mapScaleX; sy *= mapScaleY;
                cv::Scalar col = TeamColor(kv.first);
                cv::circle(mapShow, cv::Point(static_cast<int>(sx), static_cast<int>(sy)), r, col, -1);
                // show only a single label: name (x,y) with a smaller font
                std::ostringstream coordText;
                coordText << kv.first << " (" << static_cast<int>(std::lround(mx)) << "," << static_cast<int>(std::lround(my)) << ")";
                const double labelScale = std::max(0.35, 0.5 * mapScaleMin);
                const int labelThick = 1;
                cv::putText(mapShow, coordText.str(),
                            cv::Point(static_cast<int>(sx) + 6, static_cast<int>(sy) - 6),
                            cv::FONT_HERSHEY_SIMPLEX, labelScale, cv::Scalar(255, 255, 255), labelThick);
            }
        }

        // 叠加 FPS
        if (config_.showUi && !imgShow.empty()) {
            cv::putText(imgShow, "FPS: " + cv::format("%.2f", fpsDisplay),
                        cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
        }

        // 串口监视窗口
        if (config_.showUi) {
            serialMonitor_.Update(tx0305_, tx0121_, tx0301_, rx020E_, rx0301_);
        }

        if (config_.showUi) {
            cv::imshow("img", imgShow);
            cv::imshow("map", mapShow);
        }

        int key = cv::waitKey(1) & 0xFF;
        if (key == 'q') { running_ = false; break; }
        fpsFrameCount++;
        auto tNow = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(tNow - fpsStart).count();
        if (elapsed > 1.0) {
            fpsDisplay = fpsFrameCount / elapsed;
            fpsFrameCount = 0;
            fpsStart = tNow;
            if (config_.debug) {
                std::cout << "FPS: " << fpsDisplay << std::endl;
            }
        }

        const double inferElapsed = std::chrono::duration<double>(tNow - inferStatStart).count();
        if (inferElapsed > 1.0) {
            if (config_.debug && inferSampleCount > 0) {
                std::cout << "infer car avg ms: " << (carInferMsSum / inferSampleCount)
                          << ", armor avg ms: " << (armorInferMsSum / inferSampleCount)
                          << ", samples: " << inferSampleCount << std::endl;
            }
            carInferMsSum = 0.0;
            armorInferMsSum = 0.0;
            inferSampleCount = 0;
            inferStatStart = tNow;
        }
    }

    running_ = false;
    if (grabThread.joinable()) grabThread.join();
    cv::destroyAllWindows();
}

}  // namespace radar26
