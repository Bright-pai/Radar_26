#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <vector>

namespace radar26 {

// Linux 串口封装，提供打开、关闭、读写和波特率转换能力。
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // 打开串口设备并配置为原始模式。
    bool Open(const std::string& device, int baudrate, std::string* error);

    // 关闭串口。
    void Close();

    // 判断串口是否处于打开状态。
    bool IsOpen() const;

    // 向串口写入字节流。
    bool Write(const std::vector<uint8_t>& data, std::string* error);
    // 从串口读取一段字节。
    ssize_t Read(uint8_t* buffer, std::size_t size, std::string* error);

private:
    int ToTermiosBaudrate(int baudrate) const;

    int fd_ = -1;
};

}  // namespace radar26
