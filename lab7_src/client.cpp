#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstring>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol.h"

#define SERVER_PORT 8080

int sock = -1;
std::atomic<bool> is_running(false);

// 辅助：读取完整数据
bool recv_full(int sock, char* buffer, size_t length) {
    size_t total = 0;
    while (total < length) {
        int val = recv(sock, buffer + total, length - total, 0);
        if (val <= 0) return false;
        total += val;
    }
    return true;
}

// 发送请求
void send_request(uint32_t type, const std::string& body = "") {
    if (sock == -1) return;
    
    PacketHeader header;
    header.magic = MAGIC_LAB7;
    header.type = type;
    header.length = body.size();
    header.host_to_network();

    // 1. 发 Header
    send(sock, &header, HEADER_SIZE, 0);
    // 2. 发 Body
    if (body.size() > 0) {
        send(sock, body.c_str(), body.size(), 0);
    }
}

// 接收线程
void receive_thread_func() {
    while (is_running) {
        if (sock == -1) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

        char header_buf[HEADER_SIZE];
        // 1. 读取头部
        if (!recv_full(sock, header_buf, HEADER_SIZE)) {
            if (is_running) {
                std::cout << "\n[Info] Server disconnected.\n> " << std::flush;
                close(sock); sock = -1;
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

        // 3. 解析展示
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
                // Body: SrcID|Message
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
    is_running = true;
    std::thread receiver(receive_thread_func);
    receiver.detach();

    int choice;
    while (is_running) {
        print_menu();
        if (!(std::cin >> choice)) {
            std::cin.clear(); std::cin.ignore(10000, '\n'); continue;
        }

        switch (choice) {
            case 1: {
                if (sock != -1) { std::cout << "Already connected.\n"; break; }
                std::string ip; int port;
                std::cout << "IP (def 127.0.0.1): "; std::cin >> ip;
                if (ip == "d") ip = "127.0.0.1";
                port = SERVER_PORT;

                sock = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in serv_addr;
                serv_addr.sin_family = AF_INET;
                serv_addr.sin_port = htons(port);
                inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);

                if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                    perror("Conn failed"); close(sock); sock = -1;
                } else {
                    // 连接成功后，可选：发送一个握手包
                    // send_request(REQ_CONNECT, "I am Client");
                }
                break;
            }
            case 2: send_request(REQ_TIME); break;
            case 3: send_request(REQ_NAME); break;
            case 4: send_request(REQ_LIST); break;
            case 5: {
                int tid; std::string msg;
                std::cout << "Target ID: "; std::cin >> tid;
                std::cout << "Message: "; std::cin.ignore(); std::getline(std::cin, msg);
                send_request(REQ_SEND_MSG, std::to_string(tid) + ":" + msg);
                break;
            }
            case 6: 
                if (sock != -1) { send_request(REQ_EXIT); close(sock); sock = -1; }
                break;
            case 0:
                if (sock != -1) { send_request(REQ_EXIT); close(sock); }
                is_running = false; exit(0);
            default: std::cout << "Invalid.\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return 0;
}