# 编译器设置
CXX = g++
# 编译选项：
# -std=c++11: 使用 C++11 标准 (std::thread 需要)
# -Wall: 开启所有警告
# -g: 生成调试信息 (方便 GDB 调试)
# -pthread: 链接线程库 (多线程必须)
CXXFLAGS = -std=c++11 -Wall -g -pthread

# 目标文件
SERVER_TARGET = server
CLIENT_TARGET = client

# 源文件
SERVER_SRC = server.cpp
CLIENT_SRC = client.cpp

# 头文件依赖
HEADERS = protocol.h

# 伪目标 (Phony Targets)
.PHONY: all clean run_server

# 默认目标：编译服务端和客户端
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# 编译服务端
# $@ 代表目标文件名 (server), $< 代表第一个依赖文件 (server.cpp)
$(SERVER_TARGET): $(SERVER_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC)

# 编译客户端
$(CLIENT_TARGET): $(CLIENT_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(CLIENT_SRC)

# 清理编译生成的文件
clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET) *.o

# 快捷命令：运行服务端 (方便测试)
run_server: $(SERVER_TARGET)
	./$(SERVER_TARGET)

test:
	conda activate zju_comp
	python3 local_test.py --lab 7 --test 1 --host 127.0.0.1 --port 8080
	python3 local_test.py --lab 7 --test 3 --host 127.0.0.1 --port 8080 --threads 20