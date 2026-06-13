#ifndef STORE_H
#define STORE_H

#include <string>
#include <unordered_map>
#include <list>
#include <set>
#include <vector>
#include <functional>
#include <chrono>
#include <algorithm>
#include <shared_mutex>
#include <optional>

using Clock = std::chrono::steady_clock;

// 存储值类型：String / List / Hash / Set / Sorted Set
struct StoreEntry {
    enum Type { STRING, LIST, HASH, SET, ZSET } type = STRING;

    // String
    std::string str_val;

    // List
    std::list<std::string> list_val;

    // Hash
    std::unordered_map<std::string, std::string> hash_val;

    // Set
    std::set<std::string> set_val;

    // Sorted Set: pair<score, member>
    // 底层用两个数据结构：sorted by score, hash by member
    std::set<std::pair<double, std::string>> zset_by_score;
    std::unordered_map<std::string, double> zset_by_member;

    // TTL: 过期时间（steady_clock::time_point），std::nullopt 表示永不过期
    std::optional<Clock::time_point> expire_at;
};

// 内存键值存储引擎（线程安全）
class KVStore {
public:
    KVStore() = default;

    // --- Key 管理 ---
    bool del(const std::string& key);
    bool exists(const std::string& key);
    std::vector<std::string> keys(const std::string& pattern);
    bool expire(const std::string& key, int seconds);
    int64_t ttl(const std::string& key);

    // --- String 命令 ---
    void set(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    int64_t incr(const std::string& key);
    int64_t decr(const std::string& key);

    // --- List 命令 ---
    int64_t lpush(const std::string& key, const std::vector<std::string>& values);
    int64_t rpush(const std::string& key, const std::vector<std::string>& values);
    std::optional<std::string> lpop(const std::string& key);
    std::optional<std::string> rpop(const std::string& key);
    std::vector<std::string> lrange(const std::string& key, int start, int stop);
    int64_t llen(const std::string& key);

    // --- Hash 命令 ---
    int64_t hset(const std::string& key, const std::string& field, const std::string& value);
    std::optional<std::string> hget(const std::string& key, const std::string& field);
    int64_t hdel(const std::string& key, const std::vector<std::string>& fields);
    std::vector<std::string> hgetall(const std::string& key);

    // --- Set 命令 ---
    int64_t sadd(const std::string& key, const std::vector<std::string>& members);
    std::vector<std::string> smembers(const std::string& key);
    int64_t srem(const std::string& key, const std::vector<std::string>& members);

    // --- Sorted Set 命令 ---
    int64_t zadd(const std::string& key, double score, const std::string& member);
    std::vector<std::string> zrange(const std::string& key, int start, int stop);
    int64_t zcard(const std::string& key);
    std::optional<double> zscore(const std::string& key, const std::string& member);

    // --- 内部 ---
    size_t size() const;

private:
    mutable std::shared_mutex mtx_;
    std::unordered_map<std::string, StoreEntry> data_;

    // 检查 key 是否过期，过期则自动删除
    bool is_expired(const std::string& key) const;
    void auto_delete(const std::string& key);
};

// --- Key 管理 ---

inline bool KVStore::del(const std::string& key) {
    std::unique_lock lock(mtx_);
    return data_.erase(key) > 0;
}

inline bool KVStore::exists(const std::string& key) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    if (it->second.expire_at && Clock::now() > *it->second.expire_at) {
        // 懒删除：先返回 false，下次写操作时自动清理
        return false;
    }
    return true;
}

inline std::vector<std::string> KVStore::keys(const std::string& pattern) {
    std::shared_lock lock(mtx_);
    std::vector<std::string> result;
    auto now = Clock::now();
    for (const auto& [k, v] : data_) {
        if (v.expire_at && now > *v.expire_at) continue;
        // 简单通配符匹配：* 表示任意
        if (pattern == "*") {
            result.push_back(k);
        } else if (pattern.size() > 1 && pattern.back() == '*') {
            std::string prefix = pattern.substr(0, pattern.size() - 1);
            if (k.size() >= prefix.size() && k.substr(0, prefix.size()) == prefix) {
                result.push_back(k);
            }
        } else if (k == pattern) {
            result.push_back(k);
        }
    }
    return result;
}

inline bool KVStore::expire(const std::string& key, int seconds) {
    std::unique_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    it->second.expire_at = Clock::now() + std::chrono::seconds(seconds);
    return true;
}

inline int64_t KVStore::ttl(const std::string& key) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return -2;
    if (!it->second.expire_at) return -1;
    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        *it->second.expire_at - Clock::now()).count();
    return remaining > 0 ? remaining : -2;
}

// --- String ---

inline void KVStore::set(const std::string& key, const std::string& value) {
    std::unique_lock lock(mtx_);
    StoreEntry& entry = data_[key];
    entry.type = StoreEntry::STRING;
    entry.str_val = value;
    entry.expire_at.reset();
}

inline std::optional<std::string> KVStore::get(const std::string& key) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    if (is_expired(key)) return std::nullopt;
    if (it->second.type != StoreEntry::STRING) return std::nullopt;
    return it->second.str_val;
}

inline int64_t KVStore::incr(const std::string& key) {
    std::unique_lock lock(mtx_);
    auto& entry = data_[key];
    if (entry.type != StoreEntry::STRING || entry.str_val.empty()) {
        entry.type = StoreEntry::STRING;
        entry.str_val = "0";
    }
    int64_t n = 0;
    try { n = std::stoll(entry.str_val); } catch (...) { n = 0; }
    n++;
    entry.str_val = std::to_string(n);
    return n;
}

inline int64_t KVStore::decr(const std::string& key) {
    std::unique_lock lock(mtx_);
    auto& entry = data_[key];
    if (entry.type != StoreEntry::STRING || entry.str_val.empty()) {
        entry.type = StoreEntry::STRING;
        entry.str_val = "0";
    }
    int64_t n = 0;
    try { n = std::stoll(entry.str_val); } catch (...) { n = 0; }
    n--;
    entry.str_val = std::to_string(n);
    return n;
}

// --- List ---

inline int64_t KVStore::lpush(const std::string& key, const std::vector<std::string>& values) {
    std::unique_lock lock(mtx_);
    auto& entry = data_[key];
    entry.type = StoreEntry::LIST;
    for (const auto& v : values) {
        entry.list_val.push_front(v);
    }
    return entry.list_val.size();
}

inline int64_t KVStore::rpush(const std::string& key, const std::vector<std::string>& values) {
    std::unique_lock lock(mtx_);
    auto& entry = data_[key];
    entry.type = StoreEntry::LIST;
    for (const auto& v : values) {
        entry.list_val.push_back(v);
    }
    return entry.list_val.size();
}

inline std::optional<std::string> KVStore::lpop(const std::string& key) {
    std::unique_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end() || it->second.list_val.empty()) return std::nullopt;
    std::string val = it->second.list_val.front();
    it->second.list_val.pop_front();
    return val;
}

inline std::optional<std::string> KVStore::rpop(const std::string& key) {
    std::unique_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end() || it->second.list_val.empty()) return std::nullopt;
    std::string val = it->second.list_val.back();
    it->second.list_val.pop_back();
    return val;
}

inline std::vector<std::string> KVStore::lrange(const std::string& key, int start, int stop) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return {};

    const auto& list = it->second.list_val;
    int size = static_cast<int>(list.size());
    if (start < 0) start = size + start;
    if (stop < 0) stop = size + stop;
    if (start < 0) start = 0;
    if (stop >= size) stop = size - 1;

    std::vector<std::string> result;
    if (start > stop) return result;

    int idx = 0;
    for (const auto& v : list) {
        if (idx >= start && idx <= stop) result.push_back(v);
        if (idx > stop) break;
        idx++;
    }
    return result;
}

inline int64_t KVStore::llen(const std::string& key) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return 0;
    return it->second.list_val.size();
}

// --- Hash ---

inline int64_t KVStore::hset(const std::string& key, const std::string& field, const std::string& value) {
    std::unique_lock lock(mtx_);
    auto& entry = data_[key];
    entry.type = StoreEntry::HASH;
    bool existed = entry.hash_val.count(field) > 0;
    entry.hash_val[field] = value;
    return existed ? 0 : 1;
}

inline std::optional<std::string> KVStore::hget(const std::string& key, const std::string& field) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    auto fit = it->second.hash_val.find(field);
    if (fit == it->second.hash_val.end()) return std::nullopt;
    return fit->second;
}

inline int64_t KVStore::hdel(const std::string& key, const std::vector<std::string>& fields) {
    std::unique_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return 0;
    int64_t count = 0;
    for (const auto& f : fields) {
        count += it->second.hash_val.erase(f);
    }
    return count;
}

inline std::vector<std::string> KVStore::hgetall(const std::string& key) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return {};
    std::vector<std::string> result;
    for (const auto& [k, v] : it->second.hash_val) {
        result.push_back(k);
        result.push_back(v);
    }
    return result;
}

// --- Set ---

inline int64_t KVStore::sadd(const std::string& key, const std::vector<std::string>& members) {
    std::unique_lock lock(mtx_);
    auto& entry = data_[key];
    entry.type = StoreEntry::SET;
    int64_t added = 0;
    for (const auto& m : members) {
        if (entry.set_val.insert(m).second) added++;
    }
    return added;
}

inline std::vector<std::string> KVStore::smembers(const std::string& key) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return {};
    return {it->second.set_val.begin(), it->second.set_val.end()};
}

inline int64_t KVStore::srem(const std::string& key, const std::vector<std::string>& members) {
    std::unique_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return 0;
    int64_t removed = 0;
    for (const auto& m : members) {
        removed += it->second.set_val.erase(m);
    }
    return removed;
}

// --- Sorted Set (ZSet) ---

inline int64_t KVStore::zadd(const std::string& key, double score, const std::string& member) {
    std::unique_lock lock(mtx_);
    auto& entry = data_[key];
    entry.type = StoreEntry::ZSET;

    // 如果 member 已存在，先删旧数据
    auto it = entry.zset_by_member.find(member);
    if (it != entry.zset_by_member.end()) {
        double old_score = it->second;
        entry.zset_by_score.erase({old_score, member});
    }

    entry.zset_by_score.insert({score, member});
    entry.zset_by_member[member] = score;
    return it == entry.zset_by_member.end() ? 1 : 0;
}

inline std::vector<std::string> KVStore::zrange(const std::string& key, int start, int stop) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return {};

    const auto& zset = it->second.zset_by_score;
    int size = static_cast<int>(zset.size());
    if (start < 0) start = size + start;
    if (stop < 0) stop = size + stop;
    if (start < 0) start = 0;
    if (stop >= size) stop = size - 1;

    std::vector<std::string> result;
    int idx = 0;
    for (const auto& [score, member] : zset) {
        if (idx >= start && idx <= stop) result.push_back(member);
        if (idx > stop) break;
        idx++;
    }
    return result;
}

inline int64_t KVStore::zcard(const std::string& key) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return 0;
    return it->second.zset_by_score.size();
}

inline std::optional<double> KVStore::zscore(const std::string& key, const std::string& member) {
    std::shared_lock lock(mtx_);
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    auto mit = it->second.zset_by_member.find(member);
    if (mit == it->second.zset_by_member.end()) return std::nullopt;
    return mit->second;
}

inline size_t KVStore::size() const {
    std::shared_lock lock(mtx_);
    return data_.size();
}

inline bool KVStore::is_expired(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    if (it->second.expire_at && Clock::now() > *it->second.expire_at) {
        return true;
    }
    return false;
}

#endif // STORE_H
