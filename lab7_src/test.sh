#!/bin/bash
# 并发触发多个 client 同时进入“获取时间 N 次”的测试脚本
# 请根据你本地的可执行 client 路径与端口修改前面的配置

CLIENT_PATH="./client"    # 客户端可执行文件相对或绝对路径
CLIENTS=1                   # 启动多少个客户端并发（示例值 2）
INPUT_CONN_OPT="1"        # 客户端交互中的: 连接服务器 的菜单选项（视你的客户端实现而定）
INPUT_IP="127.0.0.1"      # 服务器 IP（本机测试用 127.0.0.1）
INPUT_PORT="2996"         # 服务器端口（请改为你的 server 使用的端口）
INPUT_ACTION_OPT="2"      # 客户端交互中的: 获取时间 的菜单选项（视你的客户端实现而定）
INPUT_EXIT_OPT="0"        # 退出程序的选项

# 每次获取时间的次数（如果客户端本身不循环发送，可在这里用脚本重复发送）
REQ_PER_ACTION=100

mkfifo /tmp/sync_barrier
exec 3<>/tmp/sync_barrier # 以读写模式打开 FIFO 描述符3

for i in $(seq 1 $CLIENTS); do
  {
    # 交互式输入：连接服务器
    echo "$INPUT_CONN_OPT"
    echo "$INPUT_IP"
    echo "$INPUT_PORT"

    # 等待同步信号（从描述符3读取）
    read -u 3 line

    # 触发获取时间操作。若客户端每次操作只发送一次请求，则下面循环会连续发送多次动作以达到 N 次
    # 如果你的客户端实现已在一次动作中自动发出多次请求（例如已经在代码内循环 100 次），
    # 请将下面的循环替换为单次 echo "$INPUT_ACTION_OPT"
    for ((k=1;k<=REQ_PER_ACTION;k++)); do
      echo "$INPUT_ACTION_OPT"
    done

    # 给予接收展示的时间，然后退出
    sleep 1
    echo "$INPUT_EXIT_OPT" || true

  } | "$CLIENT_PATH" | sed "s/^/[Client $i] /" &
done

sleep 1
# 向所有阻塞的客户端发送同步唤醒信号（写入 CLIENTS 次以唤醒所有进程）
for i in $(seq 1 $CLIENTS); do
  echo >&3
done

exec 3>&- # 关闭描述符
wait
rm /tmp/sync_barrier

echo "All clients finished."