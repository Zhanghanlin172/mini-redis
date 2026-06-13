#ifndef RESP_H
#define RESP_H

#include <string>
#include <vector>
#include <sstream>
#include <optional>
#include <chrono>

// RESP (REdis Serialization Protocol) 请求/响应解析器
// 支持：简单字符串(+), 错误(-), 整数(:), 批量字符串($), 数组(*)

enum class RespType { STRING, ERROR, INTEGER, BULK, ARRAY, NULL_BULK };

struct RespValue {
    RespType type = RespType::NULL_BULK;
    std::string str;
    int64_t integer = 0;
    std::vector<RespValue> array;

    bool is_null() const { return type == RespType::NULL_BULK; }

    std::string serialize() const;
    static std::string ok();
    static std::string nil();
    static std::string make_int(int64_t n);
    static std::string bulk(const std::string& s);
    static std::string bulk_null();
    static std::string error(const std::string& msg);
    static std::string array_str(const std::vector<std::string>& items);
};

// RESP 请求解析器（增量式，处理 TCP 流）
class RespParser {
public:
    // 喂入原始数据，返回解析完成的命令数组
    // 返回 nullopt 表示数据不完整，需要继续接收
    std::optional<std::vector<std::string>> feed(const std::string& data);

private:
    std::string buffer_;
};

// --- RespValue 序列化实现 ---

inline std::string RespValue::ok()            { return "+OK\r\n"; }
inline std::string RespValue::nil()           { return "$-1\r\n"; }
inline std::string RespValue::bulk_null()     { return "$-1\r\n"; }

inline std::string RespValue::make_int(int64_t n) {
    return ":" + std::to_string(n) + "\r\n";
}

inline std::string RespValue::bulk(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

inline std::string RespValue::error(const std::string& msg) {
    return "-" + msg + "\r\n";
}

inline std::string RespValue::array_str(const std::vector<std::string>& items) {
    std::string resp = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) {
        resp += bulk(item);
    }
    return resp;
}

// --- RespParser 实现 ---

inline std::optional<std::vector<std::string>> RespParser::feed(const std::string& data) {
    buffer_ += data;

    // 检查是否收到完整命令：*N\r\n 开头的数组
    if (buffer_.empty() || buffer_[0] != '*') {
        // 不完整的请求
        return std::nullopt;
    }

    // 查找 CRLF
    auto crlf = buffer_.find("\r\n");
    if (crlf == std::string::npos) return std::nullopt;

    // 解析数组长度
    int array_len = 0;
    try {
        array_len = std::stoi(buffer_.substr(1, crlf - 1));
    } catch (...) {
        buffer_.clear();
        return std::vector<std::string>{"__ERROR__"};
    }

    // 逐项解析
    size_t pos = crlf + 2;
    std::vector<std::string> result;

    for (int i = 0; i < array_len; ++i) {
        if (pos >= buffer_.size()) return std::nullopt;

        if (buffer_[pos] != '$') return std::nullopt;

        auto next_crlf = buffer_.find("\r\n", pos);
        if (next_crlf == std::string::npos) return std::nullopt;

        try {
            int bulk_len = std::stoi(buffer_.substr(pos + 1, next_crlf - pos - 1));
            pos = next_crlf + 2;

            if (pos + bulk_len + 2 > buffer_.size()) return std::nullopt;

            result.push_back(buffer_.substr(pos, bulk_len));
            pos += bulk_len + 2; // 跳过 \r\n
        } catch (...) {
            buffer_.clear();
            return std::vector<std::string>{"__ERROR__"};
        }
    }

    // 消费已解析的数据
    buffer_.erase(0, pos);
    return result;
}

#endif // RESP_H
