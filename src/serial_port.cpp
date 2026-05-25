#include "serial_port.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <thread>

namespace radar26 {

//==============================================================================
// SerialPort::~SerialPort()
// 功能：析构函数，释放串口资源。
// 参数：无。
// 返回：无。
// 副作用：如果串口处于打开状态，则关闭底层文件描述符，释放内核资源。
//==============================================================================
SerialPort::~SerialPort() {
    Close();
}

//==============================================================================
// SerialPort::Open()
// 功能：打开指定串口设备文件，配置波特率、数据位(8)、无校验、1停止位，
//        并切换为原始模式(raw mode)，使串口适于雷达站二进制数据收发。
//        同时设置 VMIN=0、VTIME=1，实现带超时的非阻塞读取行为。
// 参数：
//   device   - 串口设备路径，例如 "/dev/ttyUSB0"
//   baudrate - 波特率，如 115200、921600 等
//   error    - [出参] 可选的错误信息输出指针；失败时写入具体失败原因
// 返回：true 表示打开并配置成功；false 表示失败（可通过 error 获取原因）。
// 副作用：若之前已有打开的串口，会先关闭旧的再打开新的；
//          成功后将 fd_ 设置为有效文件描述符；
//          配置终端属性并清空输入输出缓冲区(tcflush)。
//==============================================================================
bool SerialPort::Open(const std::string& device, int baudrate, std::string* error) {
    // 如果已有打开的串口，先关闭避免文件描述符泄漏
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }

    // 以读写模式打开设备文件，O_NOCTTY 防止此设备成为进程的控制终端
    fd_ = open(device.c_str(), O_RDWR | O_NOCTTY);
    if (fd_ < 0) {
        if (error != nullptr) {
            *error = "open serial failed: " + std::string(std::strerror(errno));
        }
        return false;
    }

    // 获取当前终端属性
    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) {
        if (error != nullptr) {
            *error = "tcgetattr failed: " + std::string(std::strerror(errno));
        }
        Close();
        return false;
    }

    // 切换为原始模式：禁用输入处理(如行缓冲、回显)、输出处理(如换行转换)、
    // 软件流控等，使收发数据完全透明。
    cfmakeraw(&tty);

    // 将用户传入的波特率数字转换为 termios 宏常量
    const int speed = ToTermiosBaudrate(baudrate);
    if (speed == -1) {
        if (error != nullptr) {
            *error = "unsupported baudrate: " + std::to_string(baudrate);
        }
        Close();
        return false;
    }

    // 同时设置输入和输出波特率
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 配置控制标志：
    // CLOCAL - 忽略调制解调器控制线
    // CREAD  - 启用接收器
    tty.c_cflag |= (CLOCAL | CREAD);
    // 无校验位
    tty.c_cflag &= ~PARENB;
    // 1个停止位(清除CSTOPB即1位，设置CSTOPB为2位)
    tty.c_cflag &= ~CSTOPB;
    // 清除数据位掩码后设置为8位数据位
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    // VMIN=0, VTIME=1: 非阻塞读取模式，最多等待0.1秒(1个十分之一秒)后返回。
    // 即使没有数据可读，read()也会在超时后返回0，而不是无限阻塞。
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    // 立即应用终端属性设置
    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        if (error != nullptr) {
            *error = "tcsetattr failed: " + std::string(std::strerror(errno));
        }
        Close();
        return false;
    }

    // 清空串口的输入和输出缓冲区，丢弃残留数据
    tcflush(fd_, TCIOFLUSH);

    return true;
}

//==============================================================================
// SerialPort::Close()
// 功能：关闭串口，释放底层文件描述符。
// 参数：无。
// 返回：无。
// 副作用：将 fd_ 重置为 -1；后续所有读写操作将失败。
//         幂等操作，多次调用不会出错。
//==============================================================================
void SerialPort::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

//==============================================================================
// SerialPort::IsOpen()
// 功能：判断串口当前是否处于打开状态。
// 参数：无。
// 返回：true 表示串口已打开且可用；false 表示串口未打开。
// 副作用：无（纯查询，不修改任何状态）。
//==============================================================================
bool SerialPort::IsOpen() const {
    return fd_ >= 0;
}

//==============================================================================
// SerialPort::Write()
// 功能：将完整的字节缓冲区写入串口。采用循环写入的方式处理短写(partial write)
//        和可恢复的错误(EINTR/EAGAIN/EWOULDBLOCK)，保证所有数据最终被写出
//        或遇到不可恢复错误时返回失败。
// 参数：
//   data  - 要发送的字节向量
//   error - [出参] 可选的错误信息输出指针
// 返回：true 表示全部数据已成功写入；false 表示写入过程中发生不可恢复错误。
// 副作用：将数据通过串口物理发送到对端设备；
//          遇到 EAGAIN/EWOULDBLOCK 时会短暂睡眠 1ms 后重试；
//          遇到 EINTR(被信号中断)会立即重试。
//==============================================================================
bool SerialPort::Write(const std::vector<uint8_t>& data, std::string* error) {
    if (fd_ < 0) {
        if (error != nullptr) {
            *error = "serial is not open";
        }
        return false;
    }

    const uint8_t* ptr = data.data();
    std::size_t remaining = data.size();
    // 循环写入直到所有字节被消费
    while (remaining > 0) {
        const ssize_t n = write(fd_, ptr, remaining);
        if (n < 0) {
            // EINTR: 系统调用被信号中断，重试即可
            if (errno == EINTR) {
                continue;
            }
            // EAGAIN/EWOULDBLOCK: 缓冲区满(非阻塞模式下)，等待1ms后重试
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            // 其他错误为不可恢复错误
            if (error != nullptr) {
                *error = "write failed: " + std::string(std::strerror(errno));
            }
            return false;
        }
        // n==0 表示没有写入任何数据(极少发生)，短暂等待后重试
        if (n == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        // 推进指针，减少剩余字节数
        ptr += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

//==============================================================================
// SerialPort::Read()
// 功能：从串口读取数据到用户提供的缓冲区。在 EINTR 信号中断时会自动重试。
//        由于 Open() 中设置了 VMIN=0/VTIME=1，没有数据时会在约0.1秒超时后返回0。
// 参数：
//   buffer - [出参] 存放读取数据的缓冲区指针，调用者负责保证足够大小
//   size   - 期望读取的最大字节数
//   error  - [出参] 可选的错误信息输出指针
// 返回：>0 表示实际读取的字节数；0 表示当前无数据可读(超时返回)；
//        -1 表示发生不可恢复错误(可通过 error 获取原因)。
// 副作用：从串口内核缓冲区取出数据，缓冲区中的数据被消费。
//==============================================================================
ssize_t SerialPort::Read(uint8_t* buffer, std::size_t size, std::string* error) {
    if (fd_ < 0) {
        if (error != nullptr) {
            *error = "serial is not open";
        }
        return -1;
    }

    ssize_t n;
    // EINTR(信号中断)时自动重试，这是最常见的可恢复错误
    do {
        n = read(fd_, buffer, size);
    } while (n < 0 && errno == EINTR);

    // EAGAIN/EWOULDBLOCK 表示当前无数据可读（非阻塞模式下正常行为），
    // 不视为错误，返回 0 即可。
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        if (error != nullptr) {
            *error = "read failed: " + std::string(std::strerror(errno));
        }
        return -1;
    }
    // n<0 只可能是 EAGAIN/EWOULDBLOCK，统一返回 0 表示无数据
    return (n < 0) ? 0 : n;
}

//==============================================================================
// SerialPort::ToTermiosBaudrate()
// 功能：将用户传入的整数波特率值转换为 termios 库使用的波特率宏常量(B9600 等)。
//        支持常用波特率，部分高速率(460800/921600)需系统头文件支持。
// 参数：
//   baudrate - 整数波特率，如 115200
// 返回：对应的 termios 波特率宏常量(如 B115200)；-1 表示不支持的波特率。
// 副作用：无（纯转换函数，无 I/O 操作）。
//==============================================================================
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
    // 460800 波特率在某些旧系统上可能未定义，使用条件编译保护
    case 460800:
        return B460800;
#endif
#ifdef B921600
    // 921600 波特率同样需要条件编译
    case 921600:
        return B921600;
#endif
    default:
        return -1;
    }
}

}  // namespace radar26
