#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace radar26 {

// 简单的 TCP 客户端，用于向外部系统发送少量控制字节。
class TcpClient {
public:
    TcpClient() = default;
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    // 连接到指定 IP:Port 的 TCP 服务器。
    bool Connect(const std::string& ip, int port, std::string* error);
    // 断开连接。
    void Close();
    bool IsConnected() const;

    // 发送原始字节数据。
    bool Send(const std::vector<uint8_t>& data, std::string* error);
    // 读取原始字节（用于 Client 模式接收数据）。
    // 返回: >0=读到的字节数, 0=连接关闭, <0=错误
    ssize_t Read(uint8_t* buffer, std::size_t size, std::string* error);

private:
    int fd_ = -1;
    bool connected_ = false;
};

}  // namespace radar26
