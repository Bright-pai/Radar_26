#pragma once

#include "radar_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace radar26 {

// 解析雷达和裁判系统数据时使用的辅助位信息。
struct RadarInfoBits {
    uint8_t hasOpportunity = 0;
    uint8_t enemyTriggered = 0;
    uint8_t encryptLevel = 0;
    uint8_t keyModifiable = 0;
};

// 根据当前雷达状态、调试选项和密钥生成的发送计划。
struct RadarCmdPlan {
    bool shouldSend = false;
    bool sendDouble = false;
    uint8_t passwordCmd = 0;
    RadarInfoBits bits;
    std::vector<std::string> reasons;
};

// 通用数据包解析结果，包含序号、命令字、数据长度和负载。
struct ParsedPacket {
    uint8_t seq = 0;
    uint16_t cmdId = 0;
    uint16_t dataLen = 0;
    std::vector<uint8_t> payload;
};

// 0x020B 格式里记录的友方机器人坐标字段。
struct AllyRobotPositions020B {
    float heroX = 0.0F;
    float heroY = 0.0F;
    float engineerX = 0.0F;
    float engineerY = 0.0F;
    float infantry3X = 0.0F;
    float infantry3Y = 0.0F;
    float infantry4X = 0.0F;
    float infantry4Y = 0.0F;
    float reserved0 = 0.0F;
    float reserved1 = 0.0F;
};

// 0x0301 交互位置包的标准结构，包含数据命令和坐标区块。
struct RobotInteractionPositions0301 {
    uint16_t dataCmdId = 0;
    uint16_t senderId = 0;
    uint16_t receiverId = 0;
    AllyRobotPositions020B positions;
};

// 0x0301 的自定义雷达数据区，主要包含子命令、发送者、接收者和 66 字节数据。
struct RadarCustomData0301 {
    uint16_t subId = 0x0206;
    uint16_t senderId = 0;
    uint16_t receiverId = 0;
    std::array<uint8_t, 66> data{};
};

// 计算协议用 CRC8 校验值。
uint8_t Crc8(const uint8_t* data, std::size_t length, uint8_t init = 0xFF);
// 计算协议用 CRC16 校验值。
uint16_t Crc16(const uint8_t* data, std::size_t length, uint16_t init = 0xFFFF);

// 生成通用帧：帧头 + 长度 + 序号 + CRC8 + 命令字 + 数据 + CRC16。
std::vector<uint8_t> BuildGeneralPacket(uint8_t seq, uint16_t cmdId, const std::vector<uint8_t>& dataBytes);

// 组装位置坐标包，长度必须严格等于 24 个坐标值。
bool BuildPositionPacket(uint8_t seq, const std::vector<uint16_t>& coords, std::vector<uint8_t>* packet,
                         std::string* error);

// 组装雷达命令包，包含雷达指令、密码指令和 6 字节密钥。
bool BuildRadarCmdPacket(uint8_t seq, uint16_t senderId, uint8_t radarCmd, uint8_t passwordCmd,
                         const std::array<uint8_t, 6>& key, std::vector<uint8_t>* packet, std::string* error);

// 组装 0x0301 自定义数据包，用于向裁判系统发送固定格式数据。
bool BuildRadarCustomData0301Packet(uint8_t seq, uint16_t senderId, uint16_t receiverId,
                                    std::vector<uint8_t>* packet, std::string* error);

// 解析原始字节帧并校验 CRC。
std::optional<ParsedPacket> ParsePacket(const std::vector<uint8_t>& packet);
// 把雷达信息字节拆成各个位域含义。
RadarInfoBits DecodeRadarInfoByte(uint8_t radarInfo);
// 解析友方机器人位置的 0x020B 包。
bool DecodeAllyRobotPositions020B(const ParsedPacket& packet, AllyRobotPositions020B* positions,
                                  std::string* error);
// 解析机器人交互位置的 0x0301 包。
bool DecodeAllyRobotPositions0301(const ParsedPacket& packet, RobotInteractionPositions0301* message,
                                  std::string* error);

// 解析 0x0001 比赛状态包：比赛阶段 + 剩余时间。
bool DecodeGameStatus0001(const ParsedPacket& packet, GameStatus* status, std::string* error);
// 解析 0x0003 前哨站血量包。
bool DecodeOutpostHealth0003(const ParsedPacket& packet, OutpostHealth* health, std::string* error);

// 根据雷达信息位和调试选项生成发送计划。
RadarCmdPlan GetRadarCmdSendPlan(uint8_t radarInfo, bool requestDouble, bool requestKeyUpdate,
                                 bool requestEnemyKey);

// 生成指定长度的随机密钥字符串。
std::string GenerateRandomKey(std::size_t length = 6);

}  // namespace radar26
