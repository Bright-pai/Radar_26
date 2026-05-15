#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace radar26 {

struct RadarInfoBits {
    uint8_t hasOpportunity = 0;
    uint8_t enemyTriggered = 0;
    uint8_t encryptLevel = 0;
    uint8_t keyModifiable = 0;
};

struct RadarCmdPlan {
    bool shouldSend = false;
    bool sendDouble = false;
    uint8_t passwordCmd = 0;
    RadarInfoBits bits;
    std::vector<std::string> reasons;
};

struct ParsedPacket {
    uint8_t seq = 0;
    uint16_t cmdId = 0;
    uint16_t dataLen = 0;
    std::vector<uint8_t> payload;
};

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

struct RobotInteractionPositions0301 {
    uint16_t dataCmdId = 0;
    uint16_t senderId = 0;
    uint16_t receiverId = 0;
    AllyRobotPositions020B positions;
};

struct RadarCustomData0301 {
    uint16_t subId = 0x0206;
    uint16_t senderId = 0;
    uint16_t receiverId = 0;
    std::array<uint8_t, 66> data{};
};

uint8_t Crc8(const uint8_t* data, std::size_t length, uint8_t init = 0xFF);
uint16_t Crc16(const uint8_t* data, std::size_t length, uint16_t init = 0xFFFF);

std::vector<uint8_t> BuildGeneralPacket(uint8_t seq, uint16_t cmdId, const std::vector<uint8_t>& dataBytes);

bool BuildPositionPacket(uint8_t seq, const std::vector<uint16_t>& coords, std::vector<uint8_t>* packet,
                         std::string* error);

bool BuildRadarCmdPacket(uint8_t seq, uint16_t senderId, uint8_t radarCmd, uint8_t passwordCmd,
                         const std::array<uint8_t, 6>& key, std::vector<uint8_t>* packet, std::string* error);

bool BuildRadarCustomData0301Packet(uint8_t seq, uint16_t senderId, uint16_t receiverId,
                                    std::vector<uint8_t>* packet, std::string* error);

std::optional<ParsedPacket> ParsePacket(const std::vector<uint8_t>& packet);
RadarInfoBits DecodeRadarInfoByte(uint8_t radarInfo);
bool DecodeAllyRobotPositions020B(const ParsedPacket& packet, AllyRobotPositions020B* positions,
                                  std::string* error);
bool DecodeAllyRobotPositions0301(const ParsedPacket& packet, RobotInteractionPositions0301* message,
                                  std::string* error);

RadarCmdPlan GetRadarCmdSendPlan(uint8_t radarInfo, bool requestDouble, bool requestKeyUpdate,
                                 bool requestEnemyKey);

std::string GenerateRandomKey(std::size_t length = 6);

}  // namespace radar26
