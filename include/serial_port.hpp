#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <vector>

namespace radar26 {

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool Open(const std::string& device, int baudrate, std::string* error);
    void Close();

    bool IsOpen() const;

    bool Write(const std::vector<uint8_t>& data, std::string* error);
    ssize_t Read(uint8_t* buffer, std::size_t size, std::string* error);

private:
    int ToTermiosBaudrate(int baudrate) const;

    int fd_ = -1;
};

}  // namespace radar26
