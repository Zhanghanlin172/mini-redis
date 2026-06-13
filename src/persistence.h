#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>
#include "store.h"

namespace fs = std::filesystem;

// RDB 风格持久化（简化版 AOF）
class Persistence {
public:
    explicit Persistence(const std::string& filename);

    // 追加写命令到 AOF 文件
    void append(const std::string& command);

    // 从文件恢复数据
    void restore(KVStore& store);

    // 触发快照（简化：直接写全部数据）
    void save(const KVStore& store);

private:
    std::string filename_;
    std::ofstream file_;
};

inline Persistence::Persistence(const std::string& filename) : filename_(filename) {
    file_.open(filename_, std::ios::app);
}

inline void Persistence::append(const std::string& command) {
    if (file_.is_open()) {
        file_ << command << std::endl;
        file_.flush();
    }
}

inline void Persistence::restore(KVStore& store) {
    if (!fs::exists(filename_)) return;

    std::ifstream in(filename_);
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) continue;

        // 简化恢复：只恢复 SET 命令（SET key value）
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "SET") {
            std::string key, value;
            iss >> key;
            // 值可能包含空格，读取剩余部分
            std::getline(iss, value);
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);
            store.set(key, value);
        }
        // 后续可扩展支持更多命令的恢复
    }

    in.close();
}

inline void Persistence::save(const KVStore& /*store*/) {
    // 简化：全量快照目前依赖 AOF 恢复
    // TODO: 实现 RDB 二进制格式快照
}

#endif // PERSISTENCE_H
