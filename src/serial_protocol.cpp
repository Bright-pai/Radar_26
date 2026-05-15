#include "serial_protocol.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>

namespace radar26 {
namespace {

constexpr uint8_t kSof = 0xA5;

constexpr uint8_t kCrc8Tab[256] = {
    0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83, 0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41,
    0x9d, 0xc3, 0x21, 0x7f, 0xfc, 0xa2, 0x40, 0x1e, 0x5f, 0x01, 0xe3, 0xbd, 0x3e, 0x60, 0x82, 0xdc,
    0x23, 0x7d, 0x9f, 0xc1, 0x42, 0x1c, 0xfe, 0xa0, 0xe1, 0xbf, 0x5d, 0x03, 0x80, 0xde, 0x3c, 0x62,
    0xbe, 0xe0, 0x02, 0x5c, 0xdf, 0x81, 0x63, 0x3d, 0x7c, 0x22, 0xc0, 0x9e, 0x1d, 0x43, 0xa1, 0xff,
    0x46, 0x18, 0xfa, 0xa4, 0x27, 0x79, 0x9b, 0xc5, 0x84, 0xda, 0x38, 0x66, 0xe5, 0xbb, 0x59, 0x07,
    0xdb, 0x85, 0x67, 0x39, 0xba, 0xe4, 0x06, 0x58, 0x19, 0x47, 0xa5, 0xfb, 0x78, 0x26, 0xc4, 0x9a,
    0x65, 0x3b, 0xd9, 0x87, 0x04, 0x5a, 0xb8, 0xe6, 0xa7, 0xf9, 0x1b, 0x45, 0xc6, 0x98, 0x7a, 0x24,
    0xf8, 0xa6, 0x44, 0x1a, 0x99, 0xc7, 0x25, 0x7b, 0x3a, 0x64, 0x86, 0xd8, 0x5b, 0x05, 0xe7, 0xb9,
    0x8c, 0xd2, 0x30, 0x6e, 0xed, 0xb3, 0x51, 0x0f, 0x4e, 0x10, 0xf2, 0xac, 0x2f, 0x71, 0x93, 0xcd,
    0x11, 0x4f, 0xad, 0xf3, 0x70, 0x2e, 0xcc, 0x92, 0xd3, 0x8d, 0x6f, 0x31, 0xb2, 0xec, 0x0e, 0x50,
    0xaf, 0xf1, 0x13, 0x4d, 0xce, 0x90, 0x72, 0x2c, 0x6d, 0x33, 0xd1, 0x8f, 0x0c, 0x52, 0xb0, 0xee,
    0x32, 0x6c, 0x8e, 0xd0, 0x53, 0x0d, 0xef, 0xb1, 0xf0, 0xae, 0x4c, 0x12, 0x91, 0xcf, 0x2d, 0x73,
    0xca, 0x94, 0x76, 0x28, 0xab, 0xf5, 0x17, 0x49, 0x08, 0x56, 0xb4, 0xea, 0x69, 0x37, 0xd5, 0x8b,
    0x57, 0x09, 0xeb, 0xb5, 0x36, 0x68, 0x8a, 0xd4, 0x95, 0xcb, 0x29, 0x77, 0xf4, 0xaa, 0x48, 0x16,
    0xe9, 0xb7, 0x55, 0x0b, 0x88, 0xd6, 0x34, 0x6a, 0x2b, 0x75, 0x97, 0xc9, 0x4a, 0x14, 0xf6, 0xa8,
    0x74, 0x2a, 0xc8, 0x96, 0x15, 0x4b, 0xa9, 0xf7, 0xb6, 0xe8, 0x0a, 0x54, 0xd7, 0x89, 0x6b, 0x35,
};

constexpr uint16_t kCrc16Tab[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf, 0x8c48, 0x9dc1, 0xaf5a, 0xbed3,
    0xca6c, 0xdbe5, 0xe97e, 0xf8f7, 0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876, 0x2102, 0x308b, 0x0210, 0x1399,
    0x6726, 0x76af, 0x4434, 0x55bd, 0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c, 0xbdcb, 0xac42, 0x9ed9, 0x8f50,
    0xfbef, 0xea66, 0xd8fd, 0xc974, 0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3, 0x5285, 0x430c, 0x7197, 0x601e,
    0x14a1, 0x0528, 0x37b3, 0x263a, 0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9, 0xef4e, 0xfec7, 0xcc5c, 0xddd5,
    0xa96a, 0xb8e3, 0x8a78, 0x9bf1, 0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70, 0x8408, 0x9581, 0xa71a, 0xb693,
    0xc22c, 0xd3a5, 0xe13e, 0xf0b7, 0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036, 0x18c1, 0x0948, 0x3bd3, 0x2a5a,
    0x5ee5, 0x4f6c, 0x7df7, 0x6c7e, 0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd, 0xb58b, 0xa402, 0x9699, 0x8710,
    0xf3af, 0xe226, 0xd0bd, 0xc134, 0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3, 0x4a44, 0x5bcd, 0x6956, 0x78df,
    0x0c60, 0x1de9, 0x2f72, 0x3efb, 0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a, 0xe70e, 0xf687, 0xc41c, 0xd595,
    0xa12a, 0xb0a3, 0x8238, 0x93b1, 0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330, 0x7bc7, 0x6a4e, 0x58d5, 0x495c,
    0x3de3, 0x2c6a, 0x1ef1, 0x0f78,
};

std::vector<uint8_t> ToLittleEndian(uint16_t value) {
    return {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF)};
}

void AppendU16(std::vector<uint8_t>* dst, uint16_t value) {
    dst->push_back(static_cast<uint8_t>(value & 0xFF));
    dst->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

bool ReadU16Le(const std::vector<uint8_t>& payload, std::size_t offset, uint16_t* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "output uint16 pointer is null";
        }
        return false;
    }
    if (offset + 2U > payload.size()) {
        if (error != nullptr) {
            *error = "payload length is too short for uint16 field";
        }
        return false;
    }

    *out = static_cast<uint16_t>(payload[offset]) | (static_cast<uint16_t>(payload[offset + 1U]) << 8U);
    return true;
}

bool ReadFloatLe(const std::vector<uint8_t>& payload, std::size_t offset, float* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "output float pointer is null";
        }
        return false;
    }
    if (offset + 4U > payload.size()) {
        if (error != nullptr) {
            *error = "payload length is too short for float field";
        }
        return false;
    }

    const uint32_t bits = static_cast<uint32_t>(payload[offset]) |
                          (static_cast<uint32_t>(payload[offset + 1U]) << 8U) |
                          (static_cast<uint32_t>(payload[offset + 2U]) << 16U) |
                          (static_cast<uint32_t>(payload[offset + 3U]) << 24U);
    std::memcpy(out, &bits, sizeof(float));
    return true;
}

}  // namespace

uint8_t Crc8(const uint8_t* data, std::size_t length, uint8_t init) {
    uint8_t crc = init;
    for (std::size_t i = 0; i < length; ++i) {
        crc = kCrc8Tab[crc ^ data[i]];
    }
    return crc;
}

uint16_t Crc16(const uint8_t* data, std::size_t length, uint16_t init) {
    uint16_t crc = init;
    for (std::size_t i = 0; i < length; ++i) {
        crc = static_cast<uint16_t>((crc >> 8) ^ kCrc16Tab[(crc ^ data[i]) & 0xFF]);
    }
    return crc;
}

std::vector<uint8_t> BuildGeneralPacket(uint8_t seq, uint16_t cmdId, const std::vector<uint8_t>& dataBytes) {
    std::vector<uint8_t> packet;
    packet.reserve(1 + 2 + 1 + 1 + 2 + dataBytes.size() + 2);

    packet.push_back(kSof);
    AppendU16(&packet, static_cast<uint16_t>(dataBytes.size()));
    packet.push_back(seq);

    std::array<uint8_t, 4> headerForCrc = {
        kSof,
        static_cast<uint8_t>(dataBytes.size() & 0xFF),
        static_cast<uint8_t>((dataBytes.size() >> 8) & 0xFF),
        seq,
    };
    const uint8_t crc8 = Crc8(headerForCrc.data(), headerForCrc.size(), 0xFF);
    packet.push_back(crc8);

    AppendU16(&packet, cmdId);
    packet.insert(packet.end(), dataBytes.begin(), dataBytes.end());

    const uint16_t crc16 = Crc16(packet.data(), packet.size(), 0xFFFF);
    AppendU16(&packet, crc16);
    return packet;
}

bool BuildPositionPacket(uint8_t seq, const std::vector<uint16_t>& coords, std::vector<uint8_t>* packet,
                         std::string* error) {
    if (coords.size() != 24U) {
        if (error != nullptr) {
            *error = "position packet needs exactly 24 coordinate values";
        }
        return false;
    }

    std::vector<uint8_t> data;
    data.reserve(48);
    for (const uint16_t value : coords) {
        AppendU16(&data, value);
    }

    *packet = BuildGeneralPacket(seq, 0x0305, data);
    return true;
}

bool BuildRadarCmdPacket(uint8_t seq, uint8_t radarCmd, uint8_t passwordCmd, const std::array<uint8_t, 6>& key,
                         std::vector<uint8_t>* packet, std::string* error) {
    (void)error;
    std::vector<uint8_t> data;
    data.reserve(8);
    data.push_back(radarCmd);
    data.push_back(passwordCmd);
    data.insert(data.end(), key.begin(), key.end());
    *packet = BuildGeneralPacket(seq, 0x0121, data);
    return true;
}

bool BuildRadarCustomData0301Packet(uint8_t seq, uint16_t senderId, uint16_t receiverId,
                                    std::vector<uint8_t>* packet, std::string* error) {
    if (packet == nullptr) {
        if (error != nullptr) *error = "packet output is null";
        return false;
    }

    std::vector<uint8_t> data;
    data.reserve(72);
    AppendU16(&data, 0x0206);
    AppendU16(&data, senderId);
    AppendU16(&data, receiverId);

    for (int i = 0; i < 66; ++i) {
        data.push_back(static_cast<uint8_t>(i + 1));
    }

    *packet = BuildGeneralPacket(seq, 0x0301, data);
    return true;
}

std::optional<ParsedPacket> ParsePacket(const std::vector<uint8_t>& packet) {
    if (packet.size() < 9U || packet[0] != kSof) {
        return std::nullopt;
    }

    const uint16_t dataLen = static_cast<uint16_t>(packet[1]) | (static_cast<uint16_t>(packet[2]) << 8);
    const std::size_t expected = 1U + 2U + 1U + 1U + 2U + dataLen + 2U;
    if (packet.size() != expected) {
        return std::nullopt;
    }
    
    std::array<uint8_t, 4> headerForCrc = {packet[0], packet[1], packet[2], packet[3]};
    const uint8_t recvCrc8 = packet[4];
    const uint8_t computedCrc8 = Crc8(headerForCrc.data(), headerForCrc.size(), 0xFF);
    if (computedCrc8 != recvCrc8) {
        return std::nullopt;
    }

    const uint16_t recvCrc16 = static_cast<uint16_t>(packet[packet.size() - 2]) |
                               (static_cast<uint16_t>(packet[packet.size() - 1]) << 8);
    const uint16_t computedCrc16 = Crc16(packet.data(), packet.size() - 2, 0xFFFF);
    if (computedCrc16 != recvCrc16) {
        return std::nullopt;
    }

    ParsedPacket result;
    result.seq = packet[3];
    result.cmdId = static_cast<uint16_t>(packet[5]) | (static_cast<uint16_t>(packet[6]) << 8);
    result.dataLen = dataLen;
    result.payload.assign(packet.begin() + 7, packet.begin() + 7 + dataLen);
    return result;
}

RadarInfoBits DecodeRadarInfoByte(uint8_t radarInfo) {
    RadarInfoBits bits;
    bits.hasOpportunity = static_cast<uint8_t>(radarInfo & 0x03);
    bits.enemyTriggered = static_cast<uint8_t>((radarInfo >> 2) & 0x01);
    bits.encryptLevel = static_cast<uint8_t>((radarInfo >> 3) & 0x03);
    bits.keyModifiable = static_cast<uint8_t>((radarInfo >> 5) & 0x01);
    return bits;
}

bool DecodeAllyRobotPositions020B(const ParsedPacket& packet, AllyRobotPositions020B* positions,
                                  std::string* error) {
    if (positions == nullptr) {
        if (error != nullptr) {
            *error = "positions output is null";
        }
        return false;
    }

    if (packet.cmdId != 0x020B) {
        if (error != nullptr) {
            *error = "cmd id is not 0x020B";
        }
        return false;
    }

    if (packet.payload.size() != 40U) {
        if (error != nullptr) {
            *error = "0x020B payload length must be 40 bytes";
        }
        return false;
    }

    return ReadFloatLe(packet.payload, 0U, &positions->heroX, error) &&
           ReadFloatLe(packet.payload, 4U, &positions->heroY, error) &&
           ReadFloatLe(packet.payload, 8U, &positions->engineerX, error) &&
           ReadFloatLe(packet.payload, 12U, &positions->engineerY, error) &&
           ReadFloatLe(packet.payload, 16U, &positions->infantry3X, error) &&
           ReadFloatLe(packet.payload, 20U, &positions->infantry3Y, error) &&
           ReadFloatLe(packet.payload, 24U, &positions->infantry4X, error) &&
           ReadFloatLe(packet.payload, 28U, &positions->infantry4Y, error) &&
           ReadFloatLe(packet.payload, 32U, &positions->reserved0, error) &&
           ReadFloatLe(packet.payload, 36U, &positions->reserved1, error);
}

bool DecodeAllyRobotPositions0301(const ParsedPacket& packet, RobotInteractionPositions0301* message,
                                  std::string* error) {
    if (message == nullptr) {
        if (error != nullptr) {
            *error = "message output is null";
        }
        return false;
    }

    if (packet.cmdId != 0x0301) {
        if (error != nullptr) {
            *error = "cmd id is not 0x0301";
        }
        return false;
    }

    if (packet.payload.size() != 46U) {
        if (error != nullptr) {
            *error = "0x0301 payload length must be 46 bytes";
        }
        return false;
    }

    if (!ReadU16Le(packet.payload, 0U, &message->dataCmdId, error) ||
        !ReadU16Le(packet.payload, 2U, &message->senderId, error) ||
        !ReadU16Le(packet.payload, 4U, &message->receiverId, error)) {
        return false;
    }

    if (message->dataCmdId != 0x0200U) {
        if (error != nullptr) {
            *error = "0x0301 sub-content id is not 0x0200";
        }
        return false;   
    }

    AllyRobotPositions020B positions;
    if (!ReadFloatLe(packet.payload, 6U, &positions.heroX, error) ||
        !ReadFloatLe(packet.payload, 10U, &positions.heroY, error) ||
        !ReadFloatLe(packet.payload, 14U, &positions.engineerX, error) ||
        !ReadFloatLe(packet.payload, 18U, &positions.engineerY, error) ||
        !ReadFloatLe(packet.payload, 22U, &positions.infantry3X, error) ||
        !ReadFloatLe(packet.payload, 26U, &positions.infantry3Y, error) ||
        !ReadFloatLe(packet.payload, 30U, &positions.infantry4X, error) ||
        !ReadFloatLe(packet.payload, 34U, &positions.infantry4Y, error) ||
        !ReadFloatLe(packet.payload, 38U, &positions.reserved0, error) ||
        !ReadFloatLe(packet.payload, 42U, &positions.reserved1, error)) {
        return false;
    }

    message->positions = positions;
    return true;
}   

RadarCmdPlan GetRadarCmdSendPlan(uint8_t radarInfo, bool requestDouble, bool requestKeyUpdate,
                                 bool requestEnemyKey) {
    RadarCmdPlan plan;
    plan.bits = DecodeRadarInfoByte(radarInfo);

    const bool hasOpportunity = plan.bits.hasOpportunity > 0;
    const bool enemyTriggered = plan.bits.enemyTriggered == 1;
    const bool keyModifiable = plan.bits.keyModifiable == 1;

    plan.sendDouble = requestDouble && hasOpportunity && !enemyTriggered;

    if (requestEnemyKey) {
        plan.passwordCmd = 2;
    } else if (requestKeyUpdate && keyModifiable) {
        plan.passwordCmd = 1;
    } else {
        plan.passwordCmd = 0;
    }

    plan.shouldSend = plan.sendDouble || (plan.passwordCmd == 1) || (plan.passwordCmd == 2);

    if (plan.sendDouble) {
        plan.reasons.emplace_back("double_request_allowed");
    } else if (requestDouble) {
        if (!hasOpportunity) {
            plan.reasons.emplace_back("no_double_opportunity");
        }
        if (enemyTriggered) {
            plan.reasons.emplace_back("enemy_already_triggered");
        }
    }
    if (requestKeyUpdate) {
        plan.reasons.emplace_back(keyModifiable ? "key_update_allowed" : "key_not_modifiable");
    }
    if (requestEnemyKey) {
        plan.reasons.emplace_back("enemy_key_transfer_requested");
    }

    return plan;
}

std::string GenerateRandomKey(std::size_t length) {
    static constexpr char kCharset[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, static_cast<int>(sizeof(kCharset) - 2));

    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        out.push_back(kCharset[dist(rng)]);
    }
    return out;
}

}  // namespace radar26
