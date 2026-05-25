#include "serial_protocol.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>

namespace radar26 {
namespace {

//==============================================================================
// 协议常量
//==============================================================================

// 每个数据包的帧起始符(Start of Frame)，固定为 0xA5
constexpr uint8_t kSof = 0xA5;

//==============================================================================
// CRC8 查找表（多项式 0x5E / x^8+x^5+x^4+1）
// 用于计算协议帧头的 8 位 CRC 校验值。
// 该表和 CRC-8-Dallas/Maxim (DS18B20) 使用的查找表相同。
//==============================================================================
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

//==============================================================================
// CRC16 查找表（多项式 0x1189 / x^16+x^12+x^3+x+1）
// 用于计算整个数据包的 16 位 CRC 校验值，保护包含帧头在内的全部字节。
// 该表基于 CRC-16-CCITT 标准的变体。
//==============================================================================
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

//==============================================================================
// ToLittleEndian()
// 功能：将 16 位无符号整数转换为 2 字节小端序向量（低字节在前）。
//        用于构建符合裁判系统协议要求的字节序列。
// 参数：
//   value - 需要转换的 16 位整数
// 返回：包含 2 个字节的 vector<uint8_t>，索引0为低字节，索引1为高字节。
// 副作用：无（纯函数，仅创建并返回新向量）。
//==============================================================================
std::vector<uint8_t> ToLittleEndian(uint16_t value) {
    return {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF)};
}

//==============================================================================
// AppendU16()
// 功能：将 16 位无符号整数以小端序追加到目标字节向量的末尾。
//        与 ToLittleEndian 逻辑相同，但直接修改已有向量，避免额外分配。
// 参数：
//   dst   - [出参] 目标字节向量指针，数据被追加到此向量的尾部
//   value - 要追加的 16 位整数
// 返回：无。
// 副作用：修改 dst 指向的向量，向其中追加 2 个字节。
//==============================================================================
void AppendU16(std::vector<uint8_t>* dst, uint16_t value) {
    dst->push_back(static_cast<uint8_t>(value & 0xFF));         // 低字节
    dst->push_back(static_cast<uint8_t>((value >> 8) & 0xFF)); // 高字节
}

//==============================================================================
// ReadU16Le()
// 功能：从字节负载中的指定偏移量处读取一个小端序 uint16_t 值。
//        先进行边界检查确保不会越界读取，然后组装两个字节。
// 参数：
//   payload - 包含原始字节的负载向量
//   offset  - 读取起始偏移量（从0开始）
//   out     - [出参] 存放读取结果的指针，不可为 nullptr
//   error   - [出参] 可选的错误信息输出指针
// 返回：true 表示读取成功；false 表示越界或 out 为空。
// 副作用：修改 out 指向的值。
//==============================================================================
bool ReadU16Le(const std::vector<uint8_t>& payload, std::size_t offset, uint16_t* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "output uint16 pointer is null";
        }
        return false;
    }
    // 边界检查：偏移量 + 2 不能超过负载大小
    if (offset + 2U > payload.size()) {
        if (error != nullptr) {
            *error = "payload length is too short for uint16 field";
        }
        return false;
    }

    // 按小端序组装：低字节在 offset，高字节在 offset+1
    *out = static_cast<uint16_t>(payload[offset]) | (static_cast<uint16_t>(payload[offset + 1U]) << 8U);
    return true;
}

//==============================================================================
// ReadFloatLe()
// 功能：从字节负载中的指定偏移量处读取一个小端序 float 值（IEEE 754 单精度）。
//        先读取 4 个字节按小端序组装成 uint32_t 位模式，再通过 memcpy 安全地
//        转换为 float（避免 strict-aliasing 违规）。
// 参数：
//   payload - 包含原始字节的负载向量
//   offset  - 读取起始偏移量（从0开始）
//   out     - [出参] 存放读取结果的指针，不可为 nullptr
//   error   - [出参] 可选的错误信息输出指针
// 返回：true 表示读取成功；false 表示越界或 out 为空。
// 副作用：修改 out 指向的 float 值。
//==============================================================================
bool ReadFloatLe(const std::vector<uint8_t>& payload, std::size_t offset, float* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "output float pointer is null";
        }
        return false;
    }
    // 边界检查：偏移量 + 4 不能超过负载大小（float 占 4 字节）
    if (offset + 4U > payload.size()) {
        if (error != nullptr) {
            *error = "payload length is too short for float field";
        }
        return false;
    }

    // 按小端序组装 4 字节位模式：payload[offset]是最低字节，payload[offset+3]是最高字节
    const uint32_t bits = static_cast<uint32_t>(payload[offset]) |
                          (static_cast<uint32_t>(payload[offset + 1U]) << 8U) |
                          (static_cast<uint32_t>(payload[offset + 2U]) << 16U) |
                          (static_cast<uint32_t>(payload[offset + 3U]) << 24U);
    // 通过 memcpy 避免 strict-aliasing 违规和未定义行为
    std::memcpy(out, &bits, sizeof(float));
    return true;
}

}  // namespace

//==============================================================================
// Crc8()
// 功能：使用 CRC-8-Dallas/Maxim 算法计算 8 位 CRC 校验值。
//        该 CRC 用于保护协议帧头的前 4 个字节（帧起始符 + 数据长度 + 序号），
//        确保帧头信息在传输过程中未被篡改。
// 参数：
//   data   - 待校验数据的起始指针
//   length - 待校验数据的字节长度
//   init   - CRC 初始值，默认为 0xFF（协议规定的初始值）
// 返回：8 位 CRC 校验值。
// 副作用：无（纯计算函数，不修改任何状态）。
// 使用示例：uint8_t crc = Crc8(header.data(), 4, 0xFF);
//==============================================================================
uint8_t Crc8(const uint8_t* data, std::size_t length, uint8_t init) {
    uint8_t crc = init;
    for (std::size_t i = 0; i < length; ++i) {
        crc = kCrc8Tab[crc ^ data[i]];
    }
    return crc;
}

//==============================================================================
// Crc16()
// 功能：使用 CRC-16-CCITT 变体算法计算 16 位 CRC 校验值。
//        该 CRC 保护整个数据包（帧头 + 命令字 + 数据），不包括帧尾的 CRC16 字段本身。
//        接收端通过重新计算并与收到的 CRC16 比对来检测传输/篡改错误。
// 参数：
//   data   - 待校验数据的起始指针
//   length - 待校验数据的字节长度
//   init   - CRC 初始值，默认为 0xFFFF（协议规定的初始值）
// 返回：16 位 CRC 校验值。
// 副作用：无（纯计算函数，不修改任何状态）。
//==============================================================================
uint16_t Crc16(const uint8_t* data, std::size_t length, uint16_t init) {
    uint16_t crc = init;
    for (std::size_t i = 0; i < length; ++i) {
        crc = static_cast<uint16_t>((crc >> 8) ^ kCrc16Tab[(crc ^ data[i]) & 0xFF]);
    }
    return crc;
}

//==============================================================================
// BuildGeneralPacket()
// 功能：构造符合裁判系统串口协议的通用数据帧。这是所有数据包的基础组装函数，
//        所有业务包(位置包、命令包、自定义包等)最终都通过它完成封装。
//
//        帧格式（小端序）：
//        偏移 字节数  字段说明
//        ────────────────────────
//        0    1     SOF (0xA5) 帧起始符
//        1    2     dataLen    数据区字节长度(uint16 LE)
//        3    1     seq        帧序号，用于匹配请求和响应
//        4    1     CRC8       帧头校验（对前4字节计算）
//        5    2     cmdId      命令字(uint16 LE)，如 0x0301/0x0305
//        7   dataLen payload   数据负载
//        尾  2     CRC16      整帧校验（对前7+dataLen字节计算）
//
// 参数：
//   seq      - 帧序号，0-255循环，用于追踪收发配对
//   cmdId    - 16位命令字，标识此帧的业务类型
//   dataBytes - 数据负载字节，其长度会被写入帧头的 dataLen 字段
// 返回：完整的传输就绪字节向量，包含帧头、负载和两个 CRC 校验字段。
// 副作用：无（纯构造函数，仅创建并返回新向量，无 I/O 或全局状态修改）。
//==============================================================================
std::vector<uint8_t> BuildGeneralPacket(uint8_t seq, uint16_t cmdId, const std::vector<uint8_t>& dataBytes) {
    std::vector<uint8_t> packet;
    // 预分配内存以减少重分配：SOF(1) + dataLen(2) + seq(1) + crc8(1) + cmdId(2) + dataBytes(n) + crc16(2)
    packet.reserve(1 + 2 + 1 + 1 + 2 + dataBytes.size() + 2);

    // 帧起始符
    packet.push_back(kSof);
    // 数据区长度（小端序）
    AppendU16(&packet, static_cast<uint16_t>(dataBytes.size()));
    // 帧序号
    packet.push_back(seq);

    // ---- 计算 CRC8 对帧头前4字节(SOF + dataLen_L + dataLen_H + seq)的校验 ----
    std::array<uint8_t, 4> headerForCrc = {
        kSof,
        static_cast<uint8_t>(dataBytes.size() & 0xFF),         // dataLen 低字节
        static_cast<uint8_t>((dataBytes.size() >> 8) & 0xFF), // dataLen 高字节
        seq,
    };
    const uint8_t crc8 = Crc8(headerForCrc.data(), headerForCrc.size(), 0xFF);
    packet.push_back(crc8);

    // 命令字（小端序）
    AppendU16(&packet, cmdId);
    // 数据负载
    packet.insert(packet.end(), dataBytes.begin(), dataBytes.end());

    // ---- 计算 CRC16 对包体全部字节（不含 crc16 字段自身）的校验 ----
    const uint16_t crc16 = Crc16(packet.data(), packet.size(), 0xFFFF);
    // CRC16 追加在包尾（小端序）
    AppendU16(&packet, crc16);
    return packet;
}

//==============================================================================
// BuildPositionPacket()
// 功能：组装位置坐标包(命令字 0x0305)。将 24 个 uint16_t 坐标值按小端序
//        打包为 48 字节数据区，然后通过 BuildGeneralPacket 封装为完整帧。
//        24 个坐标对应 12 个机器人、每个机器人 2 个坐标值(x,y)。
// 参数：
//   seq    - 帧序号
//   coords - 坐标值向量，必须恰好包含 24 个 uint16_t 值
//   packet - [出参] 存放生成的数据包，不可为 nullptr
//   error  - [出参] 可选的错误信息输出指针
// 返回：true 表示组装成功；false 表示坐标数量不为 24。
// 副作用：修改 packet 指向的向量，将其替换为新生成的完整协议帧。
//==============================================================================
bool BuildPositionPacket(uint8_t seq, const std::vector<uint16_t>& coords, std::vector<uint8_t>* packet,
                         std::string* error) {
    if (coords.size() != 24U) {
        if (error != nullptr) {
            *error = "position packet needs exactly 24 coordinate values";
        }
        return false;
    }

    // 将 24 个坐标值逐对转换为小端序字节（每个 uint16 → 2字节，共 48 字节）
    std::vector<uint8_t> data;
    data.reserve(48);
    for (const uint16_t value : coords) {
        AppendU16(&data, value);
    }

    // 用命令字 0x0305 构造通用帧
    *packet = BuildGeneralPacket(seq, 0x0305, data);
    return true;
}

//==============================================================================
// BuildRadarCmdPacket()
// 功能：组装雷达命令包(命令字 0x0301)。该包用于向裁判系统发送雷达控制指令，
//        包括双倍触发指令(radarCmd)、密码操作指令(passwordCmd)和 6 字节密钥。
//
//        数据区布局（共 14 字节）：
//        偏移 字节  字段
//        ──────────────────
//        0    2    sub_id = 0x0121 (子命令标识)
//        2    2    sender_id (发送者ID：红方=9，蓝方=109)
//        4    2    receiver_id = 0x8080 (固定接收者ID)
//        6    1    radar_cmd (雷达指令：0=无操作, 1=双倍触发)
//        7    1    password_cmd (密码指令：0=无操作, 1=更新密码, 2=发送敌方密码)
//        8    6    key (6字节密钥)
//
// 参数：
//   seq        - 帧序号
//   senderId   - 发送者ID（红方 9 / 蓝方 109）
//   radarCmd   - 雷达指令码
//   passwordCmd - 密码指令码
//   key        - 6 字节密钥数组
//   packet     - [出参] 存放生成的数据包，不可为 nullptr
//   error      - [出参] 可选的错误信息输出指针
// 返回：true 表示组装成功；false 表示 packet 为空指针。
// 副作用：修改 packet 指向的向量，将其替换为新生成的完整协议帧。
//==============================================================================
bool BuildRadarCmdPacket(uint8_t seq, uint16_t senderId, uint8_t radarCmd, uint8_t passwordCmd,
                         const std::array<uint8_t, 6>& key, std::vector<uint8_t>* packet, std::string* error) {
    if (packet == nullptr) {
        if (error != nullptr) *error = "packet output is null";
        return false;
    }

    // 按裁判系统协议规定的顺序构造数据区
    std::vector<uint8_t> data;
    data.reserve(14);
    AppendU16(&data, 0x0121);        // sub_id: 子命令标识，固定为 0x0121
    AppendU16(&data, senderId);      // sender_id: 红方=9, 蓝方=109
    AppendU16(&data, 0x8080);        // receiver_id: 固定为 0x8080（裁判系统）
    data.push_back(radarCmd);        // radar_cmd: 雷达触发指令
    data.push_back(passwordCmd);     // password_cmd: 密码修改/传输指令
    data.insert(data.end(), key.begin(), key.end());  // key: 6字节密钥

    // 用命令字 0x0301 构造通用帧
    *packet = BuildGeneralPacket(seq, 0x0301, data);
    return true;
}

//==============================================================================
// BuildRadarCustomData0301Packet()
// 功能：组装 0x0301 自定义数据包，用于向裁判系统发送固定格式的测试/调试数据。
//        数据区包含 66 字节的递增测试数据(1,2,3,...,66)，主要用途为调试和
//        验证串口通信链路的正确性。
//
//        数据区布局（共 72 字节）：
//        偏移 字节  字段
//        ──────────────────
//        0    2    sub_id = 0x0206
//        2    2    sender_id
//        4    2    receiver_id
//        6    66   固定递增测试数据 (1,2,3,...,66)
//
// 参数：
//   seq        - 帧序号
//   senderId   - 发送者ID
//   receiverId - 接收者ID
//   packet     - [出参] 存放生成的数据包，不可为 nullptr
//   error      - [出参] 可选的错误信息输出指针
// 返回：true 表示组装成功；false 表示 packet 为空指针。
// 副作用：修改 packet 指向的向量，将其替换为新生成的完整协议帧。
//==============================================================================
bool BuildRadarCustomData0301Packet(uint8_t seq, uint16_t senderId, uint16_t receiverId,
                                    std::vector<uint8_t>* packet, std::string* error) {
    if (packet == nullptr) {
        if (error != nullptr) *error = "packet output is null";
        return false;
    }

    std::vector<uint8_t> data;
    data.reserve(72);
    AppendU16(&data, 0x0206);    // sub_id: 子命令标识
    AppendU16(&data, senderId);  // sender_id
    AppendU16(&data, receiverId); // receiver_id

    // 填充 66 字节递增测试数据(值为 1 到 66)，用于调试和链路验证
    for (int i = 0; i < 66; ++i) {
        data.push_back(0);
    }

    *packet = BuildGeneralPacket(seq, 0x0301, data);
    return true;
}

//==============================================================================
// ParsePacket()
// 功能：解析接收到的原始字节帧，进行格式校验和 CRC 双重校验。
//        这是所有接收数据的人口函数——任何从串口收到的完整包都应先经过它验证。
//
//        校验步骤：
//        1. 最小长度检查（至少 9 字节：SOF + dataLen(2) + seq + crc8 + cmdId(2) + crc16(2)）
//        2. 帧起始符检查（第一个字节必须为 0xA5）
//        3. 帧总长度检查（根据 dataLen 字段计算预期长度并与实际长度比对）
//        4. CRC8 校验（对帧头前 4 字节重新计算并与收到的 CRC8 比对）
//        5. CRC16 校验（对除 CRC16 字段外的全部字节重新计算并与收到的 CRC16 比对）
//
// 参数：
//   packet - 从串口/TCP 接收到的原始字节帧（可能是完整包，也可能是碎片）
// 返回：解析成功时返回包含 seq/cmdId/dataLen/payload 的 ParsedPacket；
//       任何一个检查失败返回 std::nullopt。
// 副作用：无（纯解析函数，不修改任何全局状态）。
// 注意：此函数不对数据负载内容做语义级校验，只保证格式正确和传输完整。
//==============================================================================
std::optional<ParsedPacket> ParsePacket(const std::vector<uint8_t>& packet) {
    // --- 第1步：最小长度校验 ---
    // 最少需要 9 字节：SOF(1) + dataLen(2) + seq(1) + crc8(1) + cmdId(2) + crc16(2) = 9
    if (packet.size() < 9U || packet[0] != kSof) {
        return std::nullopt;
    }

    // --- 第2步：解析数据长度并验证帧总长度 ---
    // dataLen 位于偏移 1-2，按小端序解析
    const uint16_t dataLen = static_cast<uint16_t>(packet[1]) | (static_cast<uint16_t>(packet[2]) << 8);
    // 预期总长度 = SOF(1) + dataLen(2) + seq(1) + crc8(1) + cmdId(2) + payload(dataLen) + crc16(2)
    const std::size_t expected = 1U + 2U + 1U + 1U + 2U + dataLen + 2U;
    if (packet.size() != expected) {
        return std::nullopt;
    }

    // --- 第3步：CRC8 校验（验证帧头完整性） ---
    // 帧头 CRC 覆盖：SOF(1) + dataLen(2) + seq(1)，共 4 字节
    std::array<uint8_t, 4> headerForCrc = {packet[0], packet[1], packet[2], packet[3]};
    const uint8_t recvCrc8 = packet[4];  // 收到的 CRC8
    const uint8_t computedCrc8 = Crc8(headerForCrc.data(), headerForCrc.size(), 0xFF);
    if (computedCrc8 != recvCrc8) {
        return std::nullopt;
    }

    // --- 第4步：CRC16 校验（验证整包完整性） ---
    // CRC16 覆盖：包中除最后2字节 CRC16 字段外的全部字节
    // 收到的 CRC16 位于包尾 2 字节，按小端序解析
    const uint16_t recvCrc16 = static_cast<uint16_t>(packet[packet.size() - 2]) |
                               (static_cast<uint16_t>(packet[packet.size() - 1]) << 8);
    const uint16_t computedCrc16 = Crc16(packet.data(), packet.size() - 2, 0xFFFF);
    if (computedCrc16 != recvCrc16) {
        return std::nullopt;
    }

    // --- 所有校验通过，填充解析结果 ---
    ParsedPacket result;
    result.seq = packet[3];  // 帧序号
    // 命令字位于偏移 5-6，按小端序解析
    result.cmdId = static_cast<uint16_t>(packet[5]) | (static_cast<uint16_t>(packet[6]) << 8);
    result.dataLen = dataLen;
    // 数据负载从偏移 7 开始，长度为 dataLen 字节
    result.payload.assign(packet.begin() + 7, packet.begin() + 7 + dataLen);
    return result;
}

//==============================================================================
// DecodeRadarInfoByte()
// 功能：解析雷达信息字节的各位域含义。裁判系统通过一个 8 位字节
//        传递雷达当前的各种状态信息，此函数将其拆解为可读的结构体字段。
//
//        bit 布局（从 LSB 开始）：
//        bit0-1 (2位): hasOpportunity  - 双倍触发机会（0=无机会, 1-3有不同含义）
//        bit2   (1位): enemyTriggered  - 敌方是否已触发双倍
//        bit3-4 (2位): encryptLevel    - 加密等级
//        bit5   (1位): keyModifiable   - 密钥是否可修改
//        bit6-7 (2位): 保留
//
// 参数：
//   radarInfo - 从 0x020E 包中提取的雷达信息字节
// 返回：RadarInfoBits 结构体，包含所有位域的解码结果。
// 副作用：无（纯位运算，不修改任何状态）。
//==============================================================================
RadarInfoBits DecodeRadarInfoByte(uint8_t radarInfo) {
    RadarInfoBits bits;
    // bit0-1: 双倍触发机会（取低 2 位）
    bits.hasOpportunity = static_cast<uint8_t>(radarInfo & 0x03);
    // bit2: 敌方触发标志（右移 2 位后取低 1 位）
    bits.enemyTriggered = static_cast<uint8_t>((radarInfo >> 2) & 0x01);
    // bit3-4: 加密等级（右移 3 位后取低 2 位）
    bits.encryptLevel = static_cast<uint8_t>((radarInfo >> 3) & 0x03);
    // bit5: 密钥可修改标志（右移 5 位后取低 1 位）
    bits.keyModifiable = static_cast<uint8_t>((radarInfo >> 5) & 0x01);
    return bits;
}

//==============================================================================
// DecodeAllyRobotPositions020B()
// 功能：解析 0x020B 命令字对应的友方机器人坐标包。该包包含 10 个 float 字段
//       （共 40 字节），按小端序编码，每对(x,y)表示一个机器人的位置。
//
//        坐标顺序：英雄(x,y) → 工程(x,y) → 步兵3(x,y) → 步兵4(x,y) → 保留0(x,y) → 保留1(x,y)
//
// 参数：
//   packet    - 已验证 CRC 的解析数据包，其 cmdId 必须为 0x020B
//   positions - [出参] 存放解析结果，不可为 nullptr
//   error     - [出参] 可选的错误信息输出指针
// 返回：true 表示解析成功；false 表示命令字不匹配、负载长度不对或位置指针为空。
// 副作用：修改 positions 指向的 AllyRobotPositions020B 结构体的所有字段。
//==============================================================================
bool DecodeAllyRobotPositions020B(const ParsedPacket& packet, AllyRobotPositions020B* positions,
                                  std::string* error) {
    if (positions == nullptr) {
        if (error != nullptr) {
            *error = "positions output is null";
        }
        return false;
    }

    // 只处理 0x020B 命令字
    if (packet.cmdId != 0x020B) {
        if (error != nullptr) {
            *error = "cmd id is not 0x020B";
        }
        return false;
    }

    // 0x020B 的 payload 固定为 40 字节（10 个 float * 4 字节/float）
    if (packet.payload.size() != 40U) {
        if (error != nullptr) {
            *error = "0x020B payload length must be 40 bytes";
        }
        return false;
    }

    // 按协议顺序依次解析 10 个 float 字段
    // 每个字段间距 4 字节（float 大小）
    // 任意一个字段读取失败则整个解析失败
    return ReadFloatLe(packet.payload, 0U, &positions->heroX, error) &&       // 英雄 X
           ReadFloatLe(packet.payload, 4U, &positions->heroY, error) &&       // 英雄 Y
           ReadFloatLe(packet.payload, 8U, &positions->engineerX, error) &&   // 工程 X
           ReadFloatLe(packet.payload, 12U, &positions->engineerY, error) &&   // 工程 Y
           ReadFloatLe(packet.payload, 16U, &positions->infantry3X, error) &&  // 步兵3 X
           ReadFloatLe(packet.payload, 20U, &positions->infantry3Y, error) &&  // 步兵3 Y
           ReadFloatLe(packet.payload, 24U, &positions->infantry4X, error) &&  // 步兵4 X
           ReadFloatLe(packet.payload, 28U, &positions->infantry4Y, error) &&  // 步兵4 Y
           ReadFloatLe(packet.payload, 32U, &positions->reserved0, error) &&   // 保留字段0
           ReadFloatLe(packet.payload, 36U, &positions->reserved1, error);     // 保留字段1
}

//==============================================================================
// DecodeAllyRobotPositions0301()
// 功能：解析 0x0301 命令字对应的机器人交互位置包。该包是 0x0301 通用交互帧
//        的一个子类型(sub-content id = 0x0200)，包含发送者/接收者 ID 和
//        与 0x020B 格式相同的 10 个 float 坐标字段。
//
//        payload 布局（共 46 字节）：
//        偏移 字节  字段
//        ──────────────────
//        0    2   data_cmd_id (必须为 0x0200)
//        2    2   sender_id
//        4    2   receiver_id
//        6    40  坐标数据（10个float，同 0x020B 布局）
//
// 参数：
//   packet  - 已验证 CRC 的解析数据包，其 cmdId 必须为 0x0301
//   message - [出参] 存放解析结果，不可为 nullptr
//   error   - [出参] 可选的错误信息输出指针
// 返回：true 表示解析成功；false 表示命令字不匹配、payload 长度不对或子命令字不对。
// 副作用：修改 message 指向的 RobotInteractionPositions0301 结构体的所有字段。
//==============================================================================
bool DecodeAllyRobotPositions0301(const ParsedPacket& packet, RobotInteractionPositions0301* message,
                                  std::string* error) {
    if (message == nullptr) {
        if (error != nullptr) {
            *error = "message output is null";
        }
        return false;
    }

    // 只处理 0x0301 命令字
    if (packet.cmdId != 0x0301) {
        if (error != nullptr) {
            *error = "cmd id is not 0x0301";
        }
        return false;
    }

    // 0x0301 交互位置包固定为 46 字节：dataCmdId(2) + senderId(2) + receiverId(2) + 坐标(40)
    if (packet.payload.size() != 46U) {
        if (error != nullptr) {
            *error = "0x0301 payload length must be 46 bytes";
        }
        return false;
    }

    // 先读取前 6 字节的头部字段
    if (!ReadU16Le(packet.payload, 0U, &message->dataCmdId, error) ||
        !ReadU16Le(packet.payload, 2U, &message->senderId, error) ||
        !ReadU16Le(packet.payload, 4U, &message->receiverId, error)) {
        return false;
    }

    // 子命令字必须为 0x0200（机器人交互位置数据标识）
    if (message->dataCmdId != 0x0200U) {
        if (error != nullptr) {
            *error = "0x0301 sub-content id is not 0x0200";
        }
        return false;
    }

    // 从偏移 6 开始解析 10 个 float 坐标（布局与 0x020B 完全一致）
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

//==============================================================================
// DecodeGameStatus0001()
// 功能：解析 0x0001 命令字对应的比赛状态包(game_status_t)。
//        从 11 字节负载中提取比赛类型、比赛阶段和阶段剩余时间。
//
//        payload 布局（共 11 字节）：
//        偏移 字节  字段
//        ──────────────────
//        0    1    game_type[3:0] | game_progress[7:4]
//        1    2    stage_remain_time (uint16 LE，单位：秒)
//        3    8    SyncTimeStamp (本函数不解析，保留给上层使用)
//
// 参数：
//   packet - 已验证 CRC 的解析数据包，其 cmdId 必须为 0x0001
//   status - [出参] 存放解析结果，不可为 nullptr
//   error  - [出参] 可选的错误信息输出指针
// 返回：true 表示解析成功；false 表示命令字不匹配、payload 长度不对或 status 为空。
// 副作用：修改 status 指向的 GameStatus 结构体的所有字段。
//==============================================================================
bool DecodeGameStatus0001(const ParsedPacket& packet, GameStatus* status, std::string* error) {
    if (status == nullptr) {
        if (error != nullptr) *error = "status output is null";
        return false;
    }
    if (packet.cmdId != 0x0001) {
        if (error != nullptr) *error = "cmd id is not 0x0001";
        return false;
    }
    // 0x0001 的 payload 固定为 11 字节
    if (packet.payload.size() != 11U) {
        if (error != nullptr) *error = "0x0001 payload length must be 11 bytes";
        return false;
    }
    const uint8_t first = packet.payload[0];
    // bit0-3: 比赛类型（如 1=RMUC, 2=RMUT, 3=RMUC_TechnicalChallenge 等）
    status->game_type = first & 0x0F;
    // bit4-7: 比赛阶段（如 0=准备阶段, 1=自检阶段, 2=5秒倒计时, 4=比赛中, 5=结算中）
    status->game_progress = (first >> 4) & 0x0F;
    // payload[1-2]: 阶段剩余时间（小端序 uint16_t，单位秒）
    status->stage_remain_time = static_cast<uint16_t>(packet.payload[1]) |
                                (static_cast<uint16_t>(packet.payload[2]) << 8);
    return true;
}

//==============================================================================
// DecodeOutpostHealth0003()
// 功能：解析 0x0003 命令字对应的前哨站血量包。
//        从 16 字节负载的偏移 12-13 处提取血量值(uint16 LE)，
//        其余字节不在此函数中解析。
//
//        payload 布局（共 16 字节）：
//        偏移  字节  字段
//        ──────────────────
//        0-11  12    其他数据（本函数不解析）
//        12    2     health (uint16 LE，前哨站当前血量)
//        14    2     其他数据（本函数不解析）
//
// 参数：
//   packet - 已验证 CRC 的解析数据包，其 cmdId 必须为 0x0003
//   health - [出参] 存放解析结果，不可为 nullptr
//   error  - [出参] 可选的错误信息输出指针
// 返回：true 表示解析成功；false 表示命令字不匹配、payload 长度不对或 health 为空。
// 副作用：修改 health 指向的 OutpostHealth 结构体的 health 字段。
//==============================================================================
bool DecodeOutpostHealth0003(const ParsedPacket& packet, OutpostHealth* health, std::string* error) {
    if (health == nullptr) {
        if (error != nullptr) *error = "health output is null";
        return false;
    }
    if (packet.cmdId != 0x0003) {
        if (error != nullptr) *error = "cmd id is not 0x0003";
        return false;
    }
    // 0x0003 的 payload 固定为 16 字节
    if (packet.payload.size() != 16U) {
        if (error != nullptr) *error = "0x0003 payload length must be 16 bytes";
        return false;
    }
    // 血量字段位于 payload 偏移 12-13，小端序 uint16_t
    // 注意：负载文档中 payload[12] 是低字节，payload[13] 是高字节
    health->health = static_cast<uint16_t>(packet.payload[12]) |
                     (static_cast<uint16_t>(packet.payload[13]) << 8);
    return true;
}

//==============================================================================
// GetRadarCmdSendPlan()
// 功能：根据当前雷达信息字节和上层调试选项，生成一份完整的雷达命令发送计划。
//        该计划综合如下因素决定是否发送命令及发送什么命令：
//
//        决策逻辑：
//        1. 双倍触发(double)：满足所有条件(有机会 + 敌方未触发 + 上层请求)时设置 sendDouble=true
//        2. 密码命令(passwordCmd)：
//           - requestEnemyKey=true  → passwordCmd=2（传输敌方密钥）
//           - requestKeyUpdate=true 且 keyModifiable=true → passwordCmd=1（更新密码）
//           - 其他情况 → passwordCmd=0（无密码操作）
//        3. shouldSend = sendDouble OR (passwordCmd==1) OR (passwordCmd==2)
//        4. 无论是否发送，都会在 reasons 中记录每个条件的判断结果，便于调试
//
// 参数：
//   radarInfo        - 从裁判系统收到的雷达信息字节
//   requestDouble    - 上层是否请求双倍触发（通常来自调试界面开关）
//   requestKeyUpdate  - 上层是否请求更新密钥
//   requestEnemyKey  - 上层是否请求获取敌方密钥
// 返回：RadarCmdPlan 结构体，包含是否发送、发送内容和所有判断理由。
// 副作用：无（纯决策逻辑，不触发任何发送）。
//==============================================================================
RadarCmdPlan GetRadarCmdSendPlan(uint8_t radarInfo, bool requestDouble, bool requestKeyUpdate,
                                 bool requestEnemyKey) {
    RadarCmdPlan plan;
    // 先解码雷达信息字节
    plan.bits = DecodeRadarInfoByte(radarInfo);

    // 提取各个标志位的布尔含义
    const bool hasOpportunity = plan.bits.hasOpportunity > 0;   // 是否有双倍触发机会
    const bool enemyTriggered = plan.bits.enemyTriggered == 1;  // 敌方是否已触发双倍
    const bool keyModifiable = plan.bits.keyModifiable == 1;    // 当前是否可以修改密钥

    // 双倍触发必须同时满足：上层请求 + 有机会 + 敌方未触发
    plan.sendDouble = requestDouble && hasOpportunity && !enemyTriggered;

    // 密码命令优先级：敌方密钥请求 > 密钥更新请求 > 无操作
    if (requestEnemyKey) {
        plan.passwordCmd = 2;  // 请求传输敌方密钥
    } else if (requestKeyUpdate && keyModifiable) {
        plan.passwordCmd = 1;  // 请求更新密码
    } else {
        plan.passwordCmd = 0;  // 不执行密码操作
    }

    // shouldSend 为 true 表示至少有一个需要发送的操作
    plan.shouldSend = plan.sendDouble || (plan.passwordCmd == 1) || (plan.passwordCmd == 2);

    // ---- 记录决策理由，便于调试和界面显示 ----
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

//==============================================================================
// GenerateRandomKey()
// 功能：生成指定长度的随机密钥字符串。字符集包含数字(0-9)、大写字母(A-Z)
//        和小写字母(a-z)，共 62 个字符。使用 Mersenne Twister 19937 伪随机
//        数生成器以保证良好的随机分布。每个线程拥有独立的随机数生成器实例
//        (thread_local)，避免多线程竞争。
// 参数：
//   length - 期望生成的密钥长度，默认 6
// 返回：包含随机字符的 std::string，长度等于 length 参数。
// 副作用：修改当前线程的伪随机数生成器内部状态；无其他副作用。
//==============================================================================
std::string GenerateRandomKey(std::size_t length) {
    // 字符集：数字 + 大写字母 + 小写字母，共 62 个字符
    static constexpr char kCharset[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    // thread_local 确保每个线程有独立的随机数生成器，避免数据竞争
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
