CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread
TARGET   := mini-redis
SRC_DIR  := src
OBJ_DIR  := build

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "[链接] $@"
	$(CXX) $(CXXFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@echo "[编译] $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) data.aof

run: all
	./$(TARGET) -p 6379

test: all
	@echo "=== 功能测试 ==="
	./$(TARGET) -p 6380 &
	@sleep 1
	@echo "--- SET/GET ---"
	@echo -e "*3\r\n\$3\r\nSET\r\n\$3\r\nfoo\r\n\$3\r\nbar\r\n" | nc -w1 localhost 6380
	@echo -e "*2\r\n\$3\r\nGET\r\n\$3\r\nfoo\r\n" | nc -w1 localhost 6380
	@echo "--- LIST ---"
	@echo -e "*3\r\n\$5\r\nLPUSH\r\n\$4\r\nmylist\r\n\$1\r\na\r\n" | nc -w1 localhost 6380
	@echo -e "*4\r\n\$6\r\nLRANGE\r\n\$4\r\nmylist\r\n\$1\r\n0\r\n\$2\r\n-1\r\n" | nc -w1 localhost 6380
	@echo "--- HASH ---"
	@echo -e "*4\r\n\$4\r\nHSET\r\n\$4\r\nuser\r\n\$4\r\nname\r\n\$6\r\nAlice\r\n" | nc -w1 localhost 6380
	@echo -e "*3\r\n\$4\r\nHGET\r\n\$4\r\nuser\r\n\$4\r\nname\r\n" | nc -w1 localhost 6380
	@echo "--- ZSET ---"
	@echo -e "*4\r\n\$4\r\nZADD\r\n\$6\r\nscores\r\n\$2\r\n90\r\n\$5\r\nAlice\r\n" | nc -w1 localhost 6380
	@echo -e "*4\r\n\$6\r\nZRANGE\r\n\$6\r\nscores\r\n\$1\r\n0\r\n\$2\r\n-1\r\n" | nc -w1 localhost 6380
	@echo "--- DONE ---"
	@kill %1 2>/dev/null; true
