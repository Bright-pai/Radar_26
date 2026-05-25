/**
 * @file tcp_server.cpp
 * @brief TCP 服务器实现（监听端 + 帧解析）
 *
 * 提供基于 Linux socket 的 TCP 服务器功能：
 * - 单连接模型：一次只接受一个客户端连接
 * - 基于环形缓冲区的流式数据接收
 * - 实时帧解析：从字节流中提取以 0xA5 为帧头、7 字节头 + 可变长度负载 + 2 字节校验尾的完整帧
 * - 解析出的完整帧通过回调函数 onFrame 传递给上层处理
 *
 * 帧格式：
 *   帧头(1B) | 数据长度(2B, 小端) | ...负载(dataLen字节)... | 校验尾(2B)
 *   帧头固定为 0xA5
 *   帧总长度 = 7 + dataLen + 2 = 9 + dataLen
 *
 * 使用方式：
 *   1. 构造 TcpServer 对象
 *   2. 调用 Start(port, callback) 启动监听
 *   3. 调用 Stop() 停止服务
 */

#include "tcp_server.hpp"
#include "serial_protocol.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace radar26 {

// ============================================================================
// 析构函数
// ============================================================================

/**
 * @brief 析构函数，自动停止服务器并释放所有资源。
 *
 * 副作用：
 * - 调用 Stop() 关闭监听 socket 并等待工作线程退出
 */
TcpServer::~TcpServer() { Stop(); }

// ============================================================================
// Start - 启动 TCP 服务器
// ============================================================================

/**
 * @brief 启动 TCP 服务器，开始在指定端口上监听客户端连接。
 *
 * 内部创建一个独立的工作线程（std::thread），在该线程中执行 socket 监听和
 * 数据接收逻辑。工作线程会循环接受客户端连接，每次连接中持续读取数据并解析帧。
 *
 * @param port    监听的 TCP 端口号
 * @param onFrame 帧回调函数，签名为 void(const std::vector<uint8_t>&)
 *                当收到一个完整且校验通过的帧时被调用
 * @param error   输出参数，失败时写入错误描述字符串（可为 nullptr）
 * @return true  服务器启动成功（工作线程已创建）
 * @return false 暂不返回 false（当前实现总是返回 true）
 *
 * 副作用：
 * - 将 running_ 原子标志置为 true
 * - 创建新的 std::thread 工作线程
 * - 如果之前有正在运行的服务器，会先调用 Stop() 停止它
 */
bool TcpServer::Start(int port, FrameCallback onFrame, std::string* error) {
    // 如果已有正在运行的服务，先停止旧服务
    if (running_.load()) Stop();

    running_.store(true);
    thread_ = std::thread(&TcpServer::Worker, this, port, std::move(onFrame));
    return true;
}

// ============================================================================
// Stop - 停止 TCP 服务器
// ============================================================================

/**
 * @brief 停止 TCP 服务器，关闭监听 socket 并等待工作线程退出。
 *
 * 副作用：
 * - 将 running_ 原子标志置为 false（通知工作线程退出）
 * - 如果 listenFd_ 有效，先调用 shutdown() 再 close() 关闭监听 socket
 * - 阻塞等待工作线程 join() 完成
 * - listenFd_ 置为 -1
 *
 * 注意：此函数是幂等的，可安全重复调用。
 */
void TcpServer::Stop() {
    running_.store(false);

    // 关闭监听 socket：先 shutdown 通知对端，再 close 释放资源
    if (listenFd_ >= 0) {
        shutdown(listenFd_, SHUT_RDWR);
        close(listenFd_);
        listenFd_ = -1;
    }

    // 等待工作线程退出
    if (thread_.joinable()) thread_.join();
}

// ============================================================================
// Worker - 工作线程主循环
// ============================================================================

/**
 * @brief TCP 服务器的工作线程入口函数。
 *
 * 执行流程：
 *   1. 创建监听 socket，设置 SO_REUSEADDR + SO_REUSEPORT（允许快速重启）
 *   2. 绑定到 0.0.0.0:port（监听所有网络接口）
 *   3. 调用 listen() 开始监听（backlog = 1，只接受一个连接）
 *   4. 主循环：
 *      a. accept() 等待客户端连接
 *      b. 对每个客户端，循环 recv() 读取数据到环形缓冲区
 *      c. 在环形缓冲区中扫描帧头 0xA5
 *      d. 解析帧头中的 dataLen 字段
 *      e. 当缓冲区中数据足够组成完整帧时，提取帧数据
 *      f. 通过 ParsePacket() 校验帧完整性
 *      g. 校验通过则调用 onFrame 回调
 *      h. 从环形缓冲区中移除已处理的帧数据
 *
 * 环形缓冲区设计：
 *   - ring: 固定大小 64KB 的环形缓冲区
 *   - head: 缓冲区中有效数据的起始位置索引
 *   - sz:   缓冲区中有效数据的字节数
 *   - 写入：将新数据追加到 (head + sz) % ring.size() 位置
 *   - 读取：从 head 位置开始读取
 *   - 丢弃：head = (head + 1) % ring.size(), sz -= 1
 *   - 如果缓冲区满（sz >= ring.size()），自动丢弃最旧的数据
 *
 * 帧扫描与解析：
 *   - 帧头 0xA5 位于 ring[head]
 *   - 数据长度 dataLen 由 ring[head+1] 和 ring[head+2] 组成（小端序 uint16_t）
 *   - dataLen 最大值限制为 kMaxPayload = 512 字节（防止异常数据导致错误解析）
 *   - 完整帧长度 = 7（帧头+帧类型+数据长度2B+序列号+CRC8）+ dataLen + 2（CRC16校验尾）
 *   - 当 sz < frameLen 时，等待更多数据到达
 *
 * 退出条件：
 *   - running_ 被外部设置为 false（调用 Stop()）
 *   - recv() 返回 0（客户端正常断开）
 *   - recv() 返回负值且非 EINTR（读取错误）
 *
 * @param port    监听的端口号
 * @param onFrame 帧回调函数（当完整帧解析成功后调用）
 *
 * 副作用：
 * - 设置 listenFd_ 成员变量
 * - 向 stdout/stderr 打印连接/断开/错误日志
 * - 当 running_ 为 false 时自动关闭 listenFd_
 */
void TcpServer::Worker(int port, FrameCallback onFrame) {
    // --- 步骤 1：创建监听 socket ---
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "[TCP-SRV:" << port << "] socket() failed: " << std::strerror(errno) << std::endl;
        running_.store(false);
        return;
    }

    // 设置 SO_REUSEADDR + SO_REUSEPORT，允许服务重启时立即绑定同一端口
    // 避免 TIME_WAIT 状态导致 bind() 失败
    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    // --- 步骤 2：绑定地址 ---
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;   // 监听所有网络接口（0.0.0.0）
    addr.sin_port = htons(static_cast<uint16_t>(port));
    
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[TCP-SRV:" << port << "] bind() failed: " << std::strerror(errno) << std::endl;
        close(listenFd_);
        listenFd_ = -1;
        running_.store(false);
        return;
    }

    // --- 步骤 3：开始监听 ---
    // backlog = 1：同一时间只允许一个客户端连接排队
    if (listen(listenFd_, 1) < 0) {
        std::cerr << "[TCP-SRV:" << port << "] listen() failed: " << std::strerror(errno) << std::endl;
        close(listenFd_);
        listenFd_ = -1;
        running_.store(false);
        return;
    }
    std::cout << "[TCP-SRV:" << port << "] listening on 0.0.0.0:" << port << std::endl;

    // --- 步骤 4：主循环 - 接受客户端连接并处理数据 ---
    while (running_.load()) {
        // 4a. 阻塞等待客户端连接
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

        // accept() 返回负数可能是监听 socket 被关闭（Stop() 触发）或错误
        if (clientFd < 0) {
            if (!running_.load()) break;   // Stop() 触发的关闭，正常退出
            continue;                       // 其他错误，继续尝试 accept
        }

        // 4b. 打印客户端 IP 地址
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        std::cout << "[TCP-SRV:" << port << "] connected: " << ip << std::endl;

        // 4c. 初始化环形缓冲区及相关变量
        // ring: 64KB 环形缓冲区
        // head: 有效数据起始位置索引
        // sz:   有效数据字节数
        // buf:  每次 recv() 的临时接收缓冲区
        // kMaxPayload: 帧数据负载最大长度限制（防止畸形数据导致错误解析）
        std::vector<uint8_t> ring(65536);
        std::size_t head = 0, sz = 0;
        uint8_t buf[4096];
        const std::size_t kMaxPayload = 512;

        // 4d. 接收循环：持续读取客户端数据
        while (running_.load()) {
            ssize_t n = recv(clientFd, buf, sizeof(buf), 0);

            // recv() 返回负值：EINTR（信号中断）可重试，其他错误断开连接
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            // recv() 返回 0：对方正常关闭连接
            if (n == 0) break;

            // 4e. 将新收到的数据写入环形缓冲区
            // 如果缓冲区已满（sz >= ring.size()），先丢弃最旧的一个字节以腾出空间
            for (ssize_t i = 0; i < n; ++i) {
                if (sz >= ring.size()) {
                    head = (head + 1) % ring.size();  // 丢弃旧数据：head 前进
                    --sz;
                }
                ring[(head + sz) % ring.size()] = buf[i];  // 新数据追加到尾部
                ++sz;
            }

            // 4f. 帧扫描与解析
            // 帧最小长度为 9 字节（帧头1 + 帧类型1 + 数据长度2 + 序列号1 + CRC8_1 + CRC16校验尾2）
            while (sz >= 9) {
                // 4f-i. 跳过非帧头字节，定位帧头 0xA5
                while (sz > 0 && ring[head] != 0xA5) {
                    head = (head + 1) % ring.size();
                    --sz;
                }
                if (sz < 9) break;  // 数据不足以容纳最小帧

                // 4f-ii. 读取数据长度字段（2 字节，小端序）
                uint16_t dataLen = static_cast<uint16_t>(ring[(head + 1) % ring.size()])
                                 | (static_cast<uint16_t>(ring[(head + 2) % ring.size()]) << 8);

                // 如果 dataLen 超出合理范围，这个 0xA5 可能不是真正的帧头，
                // 丢弃这一个字节后重新扫描
                if (dataLen > kMaxPayload) {
                    head = (head + 1) % ring.size();
                    --sz;
                    continue;
                }

                // 4f-iii. 计算完整帧长度
                // frameLen = 7（固定头部长度） + dataLen + 2（CRC16 校验尾）
                std::size_t frameLen = 7 + dataLen + 2;

                // 数据不足以构成完整帧，等待更多数据到达
                if (sz < frameLen) break;

                // 4f-iv. 从环形缓冲区提取完整帧数据
                std::vector<uint8_t> frame;
                frame.reserve(frameLen);
                for (std::size_t i = 0; i < frameLen; ++i)
                    frame.push_back(ring[(head + i) % ring.size()]);

                // 4f-v. 校验帧完整性（CRC 校验）
                // ParsePacket 返回 std::optional，有效帧通过 onFrame 回调传递
                if (ParsePacket(frame).has_value()) onFrame(frame);

                // 4f-vi. 从环形缓冲区中移除已处理的帧数据
                head = (head + frameLen) % ring.size();
                sz -= frameLen;
            }
        }

        // 4g. 客户端断开连接，清理资源
        close(clientFd);
        std::cout << "[TCP-SRV:" << port << "] disconnected" << std::endl;
    }

    // --- 步骤 5：退出清理 ---
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
    std::cout << "[TCP-SRV:" << port << "] stopped" << std::endl;
}

}  // namespace radar26
