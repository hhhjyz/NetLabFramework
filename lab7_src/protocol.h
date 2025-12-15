#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <arpa/inet.h> // for htonl, ntohl
#include <string>

// === 设计思路：定长包头 + 变长包体 ===

// 1. Magic Number: 用于区分 Lab7 协议和 HTTP 协议
// 'L', 'A', 'B', '7' 的 ASCII 码
const uint32_t MAGIC_LAB7 = 0x4C414237; 

// 2. 消息类型定义
enum MessageType : uint32_t {
    // 请求 (Request)
    REQ_CONNECT   = 0x01, // 连接/握手
    REQ_TIME      = 0x02, // 获取时间
    REQ_NAME      = 0x03, // 获取名字
    REQ_LIST      = 0x04, // 获取列表
    REQ_SEND_MSG  = 0x05, // 发送消息 (Body: "TargetID|Message")
    REQ_EXIT      = 0x06, // 断开连接

    // 响应 (Response) / 指示 (Indication)
    RES_OK        = 0x10, // 通用成功 (Body: 消息内容)
    RES_ERROR     = 0x11, // 通用失败 (Body: 错误原因)
    RES_LIST      = 0x12, // 列表响应 (Body: 格式化的列表字符串)
    IND_RECV_MSG  = 0x20  // 收到转发消息 (Body: "SrcID|Message")
};

// 3. 定长包头结构 (12 字节)
// 必须要使用 #pragma pack(1) 确保编译器不进行字节对齐填充，保证结构体紧凑
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;   // 魔数，必须为 MAGIC_LAB7
    uint32_t type;    // 消息类型 (MessageType)
    uint32_t length;  // Body 的长度 (不包含 Header 本身)

    // 序列化：主机字节序 -> 网络字节序 (发送前调用)
    void host_to_network() {
        magic = htonl(magic);
        type = htonl(type);
        length = htonl(length);
    }

    // 反序列化：网络字节序 -> 主机字节序 (接收后调用)
    void network_to_host() {
        magic = ntohl(magic);
        type = ntohl(type);
        length = ntohl(length);
    }
};
#pragma pack(pop)

// 4. 辅助常量
const size_t HEADER_SIZE = sizeof(PacketHeader);

#endif // PROTOCOL_H