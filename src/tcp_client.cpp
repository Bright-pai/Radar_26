/**
 * @file tcp_client.cpp
 * @brief TCP 客户端实现
 *
 * 提供基于 Linux socket 的 TCP 客户端功能：
 * - 非阻塞连接（支持超时控制，默认 2 秒连接超时）
 * - 阻塞发送（支持发送超时和自动重试）
 * - 自动检测连接断开（EPIPE / ECONNRESET）
 * - 禁用 Nagle 算法以降低小包发送延迟
 *
 * 使用方式：
 *   1. 调用 Connect(ip, port) 连接服务器
 *   2. 调用 Send(data) 发送二进制数据
 *   3. 调用 Close() 或析构函数断开连接
 *
 * 注意：本模块仅负责 TCP 传输层，不涉及应用层协议。
 */

#include "tcp_client.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>
#include <iostream>

namespace radar26 {

// ============================================================================
// 析构函数
// ============================================================================

/**
 * @brief 析构函数，自动关闭连接，释放 socket 资源。
 *
 * 副作用：
 * - 如果 socket fd_ 仍处于打开状态，调用 close() 关闭它
 * - 将 connected_ 置为 false
 */
TcpClient::~TcpClient() {
    Close();
}

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * @brief 将指定 socket 文件描述符设置为非阻塞模式。
 *
 * 非阻塞模式用于 connect() 阶段，配合 poll() 实现带超时的连接。
 * connect() 成功后，调用者应将 socket 恢复为阻塞模式以简化后续 send() 逻辑。
 *
 * @param fd    要设置的 socket 文件描述符
 * @param error 输出参数，失败时写入错误描述字符串（可为 nullptr 表示不关心错误信息）
 * @return true  设置成功
 * @return false 设置失败，error 中会写入具体原因
 *
 * 副作用：
 * - 通过 fcntl() 修改 fd 的文件状态标志，添加 O_NONBLOCK
 */
static bool SetNonBlocking(int fd, std::string* error) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        if (error) *error = "fcntl(F_GETFL) failed: " + std::string(std::strerror(errno));
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        if (error) *error = "fcntl(F_SETFL) failed: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

// ============================================================================
// Connect - 建立 TCP 连接
// ============================================================================

/**
 * @brief 连接到指定的 TCP 服务器（IP + 端口）。
 *
 * 连接流程：
 *   1. 如果已有旧连接，先关闭（防止 fd 泄漏）
 *   2. 创建 TCP socket（AF_INET, SOCK_STREAM）
 *   3. 设置为非阻塞模式
 *   4. 设置 TCP_NODELAY 禁用 Nagle 算法（小包不等待合并，立即发出）
 *   5. 调用非阻塞 connect() —— 此时通常返回 -1 且 errno == EINPROGRESS
 *   6. 使用 poll() 等待连接完成，超时时间 2 秒
 *   7. 检查 socket 错误状态（SO_ERROR），确认连接是否真正建立
 *   8. 恢复阻塞模式，设置发送超时（SO_SNDTIMEO = 3 秒）
 *
 * @param ip    目标服务器 IPv4 地址（点分十进制，如 "192.168.1.100"）
 * @param port  目标服务器端口号
 * @param error 输出参数，失败时写入错误描述字符串（可为 nullptr）
 * @return true  连接成功，connected_ 置为 true，fd_ 为有效 socket
 * @return false 连接失败，内部已调用 Close() 清理资源，fd_ 置为 -1
 *
 * 副作用：
 * - 修改成员变量 fd_（新 socket 文件描述符）
 * - 修改成员变量 connected_（连接状态）
 * - 向 stdout 打印连接成功日志
 * - 如果之前已连接，旧的 fd_ 会被关闭
 */
bool TcpClient::Connect(const std::string& ip, int port, std::string* error) {
    // 如果已有旧连接，先关闭，防止 fd 泄漏
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    connected_ = false;

    // 创建 TCP socket
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        if (error != nullptr) *error = "tcp socket() failed: " + std::string(std::strerror(errno));
        return false;
    }

    // 设置为非阻塞模式，以便配合 poll() 实现带超时的 connect
    if (!SetNonBlocking(fd_, error)) {
        Close();
        return false;
    }

    // TCP_NODELAY：禁用 Nagle 算法，确保小数据包（如协议帧头）立即发送而不等待合并
    int flag = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // 构造目标地址结构
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        if (error != nullptr) *error = "tcp inet_pton() failed for: " + ip;
        Close();
        return false;
    }

    // 非阻塞 connect：正常情况返回 -1 且 errno == EINPROGRESS，表示连接正在进行中
    int ret = connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        // 非 EINPROGRESS 的错误说明连接立即失败（如目标不可达）
        if (error != nullptr) *error = "tcp connect() failed: " + std::string(std::strerror(errno));
        Close();
        return false;
    }

    if (ret == 0) {
        // 极少数情况下本地连接可能立刻成功
        connected_ = true;
        return true;
    }

    // 使用 poll() 等待 socket 变为可写状态，表示连接完成（或失败）
    // 超时时间 2000ms（2 秒）
    struct pollfd pfd;
    pfd.fd = fd_;
    pfd.events = POLLOUT;
    int pollRet = poll(&pfd, 1, 2000);  // 2000ms

    if (pollRet < 0) {
        // poll() 自身失败（如被信号中断）
        if (error != nullptr) *error = "tcp poll() failed: " + std::string(std::strerror(errno));
        Close();
        return false;
    }
    if (pollRet == 0) {
        // poll() 超时，连接未在 2 秒内完成
        if (error != nullptr) *error = "tcp connect timeout (2s) to " + ip + ":" + std::to_string(port);
        Close();
        return false;
    }

    // poll() 返回 > 0，但还需通过 SO_ERROR 确认连接是否真正成功
    // （非阻塞 connect 即使对方拒绝，poll 也会返回可写，需要通过 SO_ERROR 检测）
    int soErr = 0;
    socklen_t soErrLen = sizeof(soErr);
    if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soErr, &soErrLen) < 0 || soErr != 0) {
        if (error != nullptr) *error = "tcp connect refused: " + std::string(std::strerror(soErr));
        Close();
        return false;
    }

    // 连接成功，恢复为阻塞模式（后续 send() 使用阻塞 + SO_SNDTIMEO 超时）
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

    // 设置收发超时 1 秒（send/recv 阻塞超过此时间则返回 EAGAIN/EWOULDBLOCK）
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    connected_ = true;
    return true;
}

// ============================================================================
// Close - 关闭连接
// ============================================================================

/**
 * @brief 关闭 TCP 连接并释放 socket 资源。
 *
 * 副作用：
 * - 如果 fd_ >= 0，调用 close() 关闭 socket
 * - fd_ 置为 -1
 * - connected_ 置为 false
 *
 * 注意：此函数是幂等的，重复调用不会出错。
 */
void TcpClient::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    connected_ = false;
}

// ============================================================================
// IsConnected - 查询连接状态
// ============================================================================

/**
 * @brief 查询当前 TCP 连接是否处于已连接状态。
 *
 * @return true  已连接
 * @return false 未连接或已断开
 *
 * 注意：此函数仅返回内存中的状态标志，不会主动探测连接是否仍然存活。
 *       连接可能在另一端断开后短暂时间内仍返回 true，直到下一次 send() 检测到错误。
 */
bool TcpClient::IsConnected() const {
    return connected_;
}

// ============================================================================
// Send - 发送数据
// ============================================================================

/**
 * @brief 通过 TCP 连接发送二进制数据。
 *
 * 采用循环发送策略，确保所有数据都被发送完毕（处理 send() 的部分写入场景）。
 * 如果遇到可恢复错误（EINTR 信号中断、EAGAIN/EWOULDBLOCK 缓冲区满），
 * 会短暂休眠后重试。
 *
 * 遇到致命错误时的处理：
 * - EPIPE（对方已关闭读端）或 ECONNRESET（对方发送 RST）
 *   -> 将 connected_ 置为 false，记录日志
 * - 其他 send() 错误 -> 直接返回失败
 *
 * @param data  要发送的字节数据（std::vector<uint8_t>）
 * @param error 输出参数，失败时写入错误描述字符串（可为 nullptr）
 * @return true  所有数据发送成功
 * @return false 发送失败，error 中会写入具体原因
 *
 * 副作用：
 * - 成功时：无（仅发送数据）
 * - 失败时：如果检测到连接断开（EPIPE/ECONNRESET），将 connected_ 置为 false
 *           并向 stderr 打印连接断开日志
 */
bool TcpClient::Send(const std::vector<uint8_t>& data, std::string* error) {
    // 前置检查：必须处于已连接状态
    if (!connected_ || fd_ < 0) {
        if (error != nullptr) *error = "tcp not connected";
        return false;
    }

    const uint8_t* ptr = data.data();
    std::size_t remaining = data.size();

    // 循环发送直到所有数据都发送完毕
    while (remaining > 0) {
        // MSG_NOSIGNAL：防止对端关闭时触发 SIGPIPE 信号导致进程终止
        const ssize_t n = send(fd_, ptr, remaining, MSG_NOSIGNAL);

        if (n < 0) {
            // EINTR：系统调用被信号中断，可安全重试
            if (errno == EINTR) continue;

            // EAGAIN / EWOULDBLOCK：发送缓冲区满，短暂休眠后重试
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            // EPIPE：对方已关闭读端（收到 SIGPIPE 但被 MSG_NOSIGNAL 抑制）
            // ECONNRESET：对方发送 RST 重置连接
            // 这些情况下标记为未连接
            if (errno == EPIPE || errno == ECONNRESET) {
                connected_ = false;
                std::cerr << "[TCP] connection lost" << std::endl;
            }

            if (error != nullptr) *error = "tcp send() failed: " + std::string(std::strerror(errno));
            return false;
        }

        // send() 返回 0 通常表示发送缓冲区暂时无空间，短暂休眠后重试
        if (n == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // 成功发送了 n 字节，移动指针，减少剩余计数
        ptr += n;
        remaining -= static_cast<std::size_t>(n);
    }

    return true;
}

// ============================================================================
// Read - 接收数据（Client 模式读取）
// ============================================================================

/**
 * @brief 从 TCP 连接读取原始字节数据。
 *
 * 行为与 SerialPort::Read 一致：非阻塞超时读（依赖 SO_RCVTIMEO），
 * EINTR 自动重试，超时返回 0，错误返回 <0。
 *
 * @param buffer 接收缓冲区指针
 * @param size   缓冲区大小
 * @param error  输出参数，失败时写入错误描述
 * @return >0 实际读取字节数，0 超时/无数据，<0 错误
 */
ssize_t TcpClient::Read(uint8_t* buffer, std::size_t size, std::string* error) {
    if (!connected_ || fd_ < 0) {
        if (error) *error = "tcp not connected";
        return -1;
    }
    ssize_t n;
    do {
        n = recv(fd_, buffer, size, 0);
    } while (n < 0 && errno == EINTR);
    if (n == 0) {
        // 对端正常关闭连接（TCP FIN）
        connected_ = false;
        if (error) *error = "tcp connection closed by peer";
        return -1;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        if (errno == EPIPE || errno == ECONNRESET) {
            connected_ = false;
        }
        if (error) *error = "tcp recv() failed: " + std::string(std::strerror(errno));
        return -1;
    }
    return (n < 0) ? 0 : n;
}

}  // namespace radar26
