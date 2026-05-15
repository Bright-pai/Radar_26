#include "serial_port.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <thread>

namespace radar26 {

SerialPort::~SerialPort() {
    Close();
}

bool SerialPort::Open(const std::string& device, int baudrate, std::string* error) {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }

    fd_ = open(device.c_str(), O_RDWR | O_NOCTTY);
    if (fd_ < 0) {
        if (error != nullptr) {
            *error = "open serial failed: " + std::string(std::strerror(errno));
        }
        return false;
    }

    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) {
        if (error != nullptr) {
            *error = "tcgetattr failed: " + std::string(std::strerror(errno));
        }
        Close();
        return false;
    }

    cfmakeraw(&tty);

    const int speed = ToTermiosBaudrate(baudrate);
    if (speed == -1) {
        if (error != nullptr) {
            *error = "unsupported baudrate: " + std::to_string(baudrate);
        }
        Close();
        return false;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        if (error != nullptr) {
            *error = "tcsetattr failed: " + std::string(std::strerror(errno));
        }
        Close();
        return false;
    }

    tcflush(fd_, TCIOFLUSH);

    return true;
}

void SerialPort::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool SerialPort::IsOpen() const {
    return fd_ >= 0;
}

bool SerialPort::Write(const std::vector<uint8_t>& data, std::string* error) {
    if (fd_ < 0) {
        if (error != nullptr) {
            *error = "serial is not open";
        }
        return false;
    }

    const uint8_t* ptr = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t n = write(fd_, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (error != nullptr) {
                *error = "write failed: " + std::string(std::strerror(errno));
            }
            return false;
        }
        if (n == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        ptr += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

ssize_t SerialPort::Read(uint8_t* buffer, std::size_t size, std::string* error) {
    if (fd_ < 0) {
        if (error != nullptr) {
            *error = "serial is not open";
        }
        return -1;
    }

    ssize_t n;
    do {
        n = read(fd_, buffer, size);
    } while (n < 0 && errno == EINTR);

    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        if (error != nullptr) {
            *error = "read failed: " + std::string(std::strerror(errno));
        }
        return -1;
    }
    return (n < 0) ? 0 : n;
}

int SerialPort::ToTermiosBaudrate(int baudrate) const {
    switch (baudrate) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    case 230400:
        return B230400;
#ifdef B460800
    case 460800:
        return B460800;
#endif
#ifdef B921600
    case 921600:
        return B921600;
#endif
    default:
        return -1;
    }
}

}  // namespace radar26
