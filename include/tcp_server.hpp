#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace radar26 {

// TCP 服务器：监听端口，按 0xA5 协议帧解析完整帧后回调。
class TcpServer {
public:
    using FrameCallback = std::function<void(const std::vector<uint8_t>& frame)>;

    TcpServer() = default;
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool Start(int port, FrameCallback onFrame, std::string* error);
    void Stop();
    bool IsRunning() const { return running_.load(); }

private:
    void Worker(int port, FrameCallback onFrame);
    int listenFd_ = -1;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace radar26
