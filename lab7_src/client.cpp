#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstring>
#include <atomic>
#include <mutex>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol.h"

#define SERVER_PORT 2996

// 全局变量
int sock = -1;
std::atomic<bool> is_connected(false);      // 连接状态
std::atomic<bool> receiver_running(false);  // 接收线程运行标志
std::thread* receiver_thread = nullptr;     // 接收线程指针
std::mutex sock_mtx;                        // socket 操作锁

// 辅助：读取完整数据
bool recv_full(int sock, char* buffer, size_t length) {
    size_t total = 0;
    while (total < length && receiver_running) {
        int val = recv(sock, buffer + total, length - total, 0);
        if (val <= 0) return false;
        total += val;
    }
    return total == length;
}

// 发送请求（带连接状态检查）
bool send_request(uint32_t type, const std::string& body = "") {
    std::lock_guard<std::mutex> lock(sock_mtx);
    if (sock == -1 || !is_connected) {
        std::cout << "[Error] Not connected to server.\n";
        return false;
    }
    
    PacketHeader header;
    header.magic = MAGIC_LAB7;
    header.type = type;
    header.length = body.size();
    header.host_to_network();

    // 1. 发 Header
    if (send(sock, &header, HEADER_SIZE, 0) < 0) return false;
    // 2. 发 Body
    if (body.size() > 0) {
        if (send(sock, body.c_str(), body.size(), 0) < 0) return false;
    }
    return true;
}

// 接收线程函数
void receive_thread_func() {
    while (receiver_running && is_connected) {
        if (sock == -1) break;

        char header_buf[HEADER_SIZE];
        // 1. 读取头部
        if (!recv_full(sock, header_buf, HEADER_SIZE)) {
            if (receiver_running && is_connected) {
                std::cout << "\n[Info] Server disconnected.\n> " << std::flush;
                is_connected = false;
            }
            break;
        }

        PacketHeader header = *(PacketHeader*)header_buf;
        header.network_to_host();

        if (header.magic != MAGIC_LAB7) {
            std::cout << "\n[Error] Invalid Protocol Magic.\n> " << std::flush;
            break;
        }

        // 2. 读取包体
        std::string body;
        if (header.length > 0) {
            std::vector<char> body_buf(header.length);
            if (!recv_full(sock, body_buf.data(), header.length)) break;
            body.assign(body_buf.data(), header.length);
        }

        // 3. 解析展示（模拟消息队列处理，直接在接收线程打印）
        switch (header.type) {
            case RES_OK:
                std::cout << "\n[Server]: " << body << "\n> " << std::flush;
                break;
            case RES_ERROR:
                std::cout << "\n[Error]: " << body << "\n> " << std::flush;
                break;
            case RES_LIST:
                std::cout << "\n=== Online Clients ===\n" << body << "\n> " << std::flush;
                break;
            case IND_RECV_MSG: {
                // Body: SrcID|Message (指示消息：服务器转发的别的客户端的消息)
                size_t delim = body.find('|');
                if (delim != std::string::npos) {
                    std::string src = body.substr(0, delim);
                    std::string msg = body.substr(delim + 1);
                    std::cout << "\n\n>>> Message from Client " << src << ": " << msg << "\n\n> " << std::flush;
                }
                break;
            }
            default:
                std::cout << "\n[Unknown Type " << header.type << "]: " << body << "\n> " << std::flush;
        }
    }
    receiver_running = false;
}

// 断开连接函数
void disconnect() {
    if (!is_connected && sock == -1) {
        std::cout << "[Info] Not connected.\n";
        return;
    }
    
    // 1. 发送退出请求
    if (is_connected && sock != -1) {
        // 直接发送，不经过 send_request 避免死锁
        PacketHeader header;
        header.magic = MAGIC_LAB7;
        header.type = REQ_EXIT;
        header.length = 0;
        header.host_to_network();
        send(sock, &header, HEADER_SIZE, 0);
    }
    
    // 2. 设置标志，通知子线程退出
    is_connected = false;
    receiver_running = false;
    
    // 3. 关闭 socket（这会使 recv 返回错误，子线程退出）
    {
        std::lock_guard<std::mutex> lock(sock_mtx);
        if (sock != -1) {
            close(sock);
            sock = -1;
        }
    }
    
    // 4. 等待子线程关闭
    if (receiver_thread != nullptr && receiver_thread->joinable()) {
        receiver_thread->join();
        delete receiver_thread;
        receiver_thread = nullptr;
    }
    
    std::cout << "[Info] Disconnected.\n";
}

void print_menu() {
    std::cout << "\n=== Lab7 Custom Protocol Client ===\n";
    std::cout << "1. Connect\n";
    std::cout << "2. Get Time\n";
    std::cout << "3. Get Server Name\n";
    std::cout << "4. Get Client List\n";
    std::cout << "5. Send Message\n";
    std::cout << "6. Disconnect\n";
    std::cout << "0. Exit\n";
    std::cout << "> ";
}

int main() {
    int choice;
    bool running = true;
    
    while (running) {
        print_menu();
        if (!(std::cin >> choice)) {
            std::cin.clear(); 
            std::cin.ignore(10000, '\n'); 
            continue;
        }

        switch (choice) {
            case 1: {  // 连接功能
                if (is_connected) { 
                    std::cout << "[Info] Already connected.\n"; 
                    break; 
                }
                
                std::string ip;
                std::cout << "Server IP (enter 'd' for 127.0.0.1): "; 
                std::cin >> ip;
                if (ip == "d") ip = "127.0.0.1";
                
                // 创建 socket
                sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock < 0) {
                    perror("Socket creation failed");
                    break;
                }
                
                struct sockaddr_in serv_addr;
                serv_addr.sin_family = AF_INET;
                serv_addr.sin_port = htons(SERVER_PORT);
                inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);

                // 调用 connect
                std::cout << "[Info] Connecting to " << ip << ":" << SERVER_PORT << "...\n";
                if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                    perror("Connection failed"); 
                    close(sock); 
                    sock = -1;
                } else {
                    // 连接成功，设置状态
                    is_connected = true;
                    receiver_running = true;
                    std::cout << "[Info] Connected successfully!\n";
                    
                    // 创建接收数据的子线程
                    receiver_thread = new std::thread(receive_thread_func);
                }
                break;
            }
            
            case 2:  // 获取时间
                if (!is_connected) { std::cout << "[Error] Please connect first.\n"; break; }
                send_request(REQ_TIME); 
                break;
                
            case 3:  // 获取名字
                if (!is_connected) { std::cout << "[Error] Please connect first.\n"; break; }
                send_request(REQ_NAME); 
                break;
                
            case 4:  // 获取客户端列表
                if (!is_connected) { std::cout << "[Error] Please connect first.\n"; break; }
                send_request(REQ_LIST); 
                break;
                
            case 5: {  // 发送消息
                if (!is_connected) { std::cout << "[Error] Please connect first.\n"; break; }
                int tid; 
                std::string msg;
                std::cout << "Target Client ID: "; 
                std::cin >> tid;
                std::cout << "Message: "; 
                std::cin.ignore(); 
                std::getline(std::cin, msg);
                send_request(REQ_SEND_MSG, std::to_string(tid) + ":" + msg);
                break;
            }
            
            case 6:  // 断开连接
                disconnect();
                break;
                
            case 0:  // 退出
                // 如果已连接，先断开
                if (is_connected) {
                    disconnect();
                }
                running = false;
                std::cout << "[Info] Goodbye!\n";
                break;
                
            default: 
                std::cout << "[Error] Invalid choice.\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}