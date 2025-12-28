#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <map>
#include <ctime>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol.h"

#define SERVER_PORT 2996

// 全局变量：存储在线客户端 <SocketFD, IP:Port>
std::map<int, std::string> online_clients;
std::mutex clients_mtx;

// 全局退出标志
std::atomic<bool> server_running(true);
int server_fd = -1;

// 信号处理函数
void signal_handler(int signum) {
    std::cout << "\n[Info] Received signal " << signum << ", shutting down server..." << std::endl;
    server_running = false;
    
    // 关闭服务器socket，使accept()返回错误
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
}

// 辅助函数：确保读取指定长度的数据（处理 TCP 拆包）
bool recv_full(int sock, char* buffer, size_t length) {
    size_t total_read = 0;
    while (total_read < length && server_running) {
        int valread = recv(sock, buffer + total_read, length - total_read, 0);
        if (valread <= 0) return false; // 连接断开或错误
        total_read += valread;
    }
    return total_read == length;
}

// 辅助函数：发送协议包
bool send_packet(int sock, uint32_t type, const std::string& body) {
    PacketHeader header;
    header.magic = MAGIC_LAB7;
    header.type = type;
    header.length = body.size();
    header.host_to_network(); // 序列化

    // 1. 发送 Header
    if (send(sock, &header, HEADER_SIZE, 0) < 0) return false;
    
    // 2. 发送 Body
    if (body.size() > 0) {
        if (send(sock, body.c_str(), body.size(), 0) < 0) return false;
    }
    return true;
}

// 处理 HTTP 请求 (Lab8 预留桩代码)
void handle_http(int sock) {
    char buffer[4096];
    recv(sock, buffer, sizeof(buffer), 0); // 简单读走数据
    std::string response = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nHello from Lab7 Server (HTTP Mode)";
    send(sock, response.c_str(), response.size(), 0);
}

// 客户端处理线程
void client_handler(int client_sock, std::string client_addr) {
    // 注册客户端
    {
        std::lock_guard<std::mutex> lock(clients_mtx);
        online_clients[client_sock] = client_addr;
    }
    std::cout << "[Info] Client " << client_addr << " (ID:" << client_sock << ") connected." << std::endl;

    // 发送欢迎消息 (Lab7 Test 1 要求)
    send_packet(client_sock, RES_OK, "Welcome to Lab7 Server (Protocol v1.0)");

    bool is_running = true; // 控制循环的标志

    while (is_running && server_running) {
        // === 协议嗅探与边界识别 ===
        char header_buf[HEADER_SIZE];
        
        // 1. 使用 MSG_PEEK 偷看头部，不从缓冲区移除
        int peek_len = recv(client_sock, header_buf, HEADER_SIZE, MSG_PEEK);
        if (peek_len <= 0) break; // 断开

        // 2. 检查是否为 HTTP (Lab8 兼容)
        if (peek_len >= 4) {
            if (strncmp(header_buf, "GET ", 4) == 0 || strncmp(header_buf, "POST", 4) == 0) {
                std::cout << "[Info] Detected HTTP Request from " << client_sock << std::endl;
                handle_http(client_sock);
                is_running = false; // 处理完 HTTP 后退出循环
                continue;
            }
        }

        // 3. 检查是否为 Lab7 协议
        // 必须读够 12 字节才能判断完整的 Header
        if (peek_len < (int)HEADER_SIZE) { 
             // 还没收够头部的长度，继续等待下一次循环（实际高并发可能需要更复杂的缓冲逻辑，此处简化）
             continue; 
        }

        PacketHeader* peek_header = (PacketHeader*)header_buf;
        // 注意：peek 到的数据是网络字节序，需要转换来检查
        if (ntohl(peek_header->magic) != MAGIC_LAB7) {
            std::cerr << "[Error] Unknown protocol magic. Closing." << std::endl;
            break;
        }

        // === 正式读取数据 ===
        // 1. 确认是 Lab7 协议，正式将 Header 从缓冲区读走
        if (!recv_full(client_sock, header_buf, HEADER_SIZE)) break;
        PacketHeader header = *(PacketHeader*)header_buf;
        header.network_to_host(); // 反序列化

        // 2. 读取 Body
        std::string body;
        if (header.length > 0) {
            std::vector<char> body_buf(header.length); 
            if (!recv_full(client_sock, body_buf.data(), header.length)) break;
            body.assign(body_buf.data(), header.length);
        }

        // === 业务逻辑 ===
        switch (header.type) {
            case REQ_TIME: {
                time_t now = time(0);
                std::string t(ctime(&now));
                if (!t.empty()) t.pop_back();
                send_packet(client_sock, RES_OK, t);
                break;
            }
            case REQ_NAME: {
                char hostname[128];
                gethostname(hostname, sizeof(hostname));
                send_packet(client_sock, RES_OK, std::string(hostname));
                break;
            }
            case REQ_LIST: {
                std::string list_str = "ID\tAddress\n";
                {
                    std::lock_guard<std::mutex> lock(clients_mtx);
                    for (const auto& client : online_clients) {
                        list_str += std::to_string(client.first) + "\t" + client.second + "\n";
                    }
                }
                send_packet(client_sock, RES_LIST, list_str);
                break;
            }
            case REQ_SEND_MSG: {
                // Body 格式: "TargetID:Message"
                size_t delim = body.find(':');
                if (delim != std::string::npos) {
                    try {
                        int target_id = std::stoi(body.substr(0, delim));
                        std::string msg_content = body.substr(delim + 1);
                        bool sent = false;
                        
                        // 查找目标并转发
                        {
                            std::lock_guard<std::mutex> lock(clients_mtx);
                            if (online_clients.count(target_id)) {
                                std::string fwd = std::to_string(client_sock) + "|" + msg_content;
                                send_packet(target_id, IND_RECV_MSG, fwd);
                                sent = true;
                            }
                        }
                        
                        // 回复发送者结果
                        send_packet(client_sock, sent ? RES_OK : RES_ERROR, sent ? "Sent." : "User not found.");
                    } catch (...) {
                        send_packet(client_sock, RES_ERROR, "Invalid ID format.");
                    }
                } else {
                    send_packet(client_sock, RES_ERROR, "Format error (ID:Msg).");
                }
                break;
            }
            case REQ_EXIT: {
                is_running = false; // 退出循环
                break;
            }
            default:
                std::cout << "[Warn] Unknown Msg Type: " << header.type << std::endl;
        }
    }

    // 清理工作
    close(client_sock);
    {
        std::lock_guard<std::mutex> lock(clients_mtx);
        online_clients.erase(client_sock);
    }
    std::cout << "[Info] Client " << client_addr << " disconnected." << std::endl;
}

int main() {
    struct sockaddr_in address;
    int opt = 1;

    // 注册信号处理函数（检测退出指令）
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // kill 命令

    // 创建 Socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 端口复用
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    // 绑定
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 监听
    if (listen(server_fd, 20) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Lab7 Server (Protocol Aware) listening on " << SERVER_PORT << "..." << std::endl;
    std::cout << "[Info] Press Ctrl+C to shutdown server." << std::endl;

    // 保存客户端处理线程
    std::vector<std::thread> client_threads;

    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        
        if (client_sock < 0) {
            if (server_running) {
                perror("accept failed");
            }
            continue;
        }

        std::string ip_port = std::string(inet_ntoa(client_addr.sin_addr)) + ":" + std::to_string(ntohs(client_addr.sin_port));
        
        // 启动新线程处理客户端
        std::thread(client_handler, client_sock, ip_port).detach();
    }

    // 服务器关闭：关闭所有客户端连接
    std::cout << "[Info] Closing all client connections..." << std::endl;
    {
        std::lock_guard<std::mutex> lock(clients_mtx);
        for (const auto& client : online_clients) {
            close(client.first);
        }
        online_clients.clear();
    }

    std::cout << "[Info] Server shutdown complete." << std::endl;
    return 0;
}