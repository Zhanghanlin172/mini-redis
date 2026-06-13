#include <iostream>
#include <csignal>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#include "resp.h"
#include "store.h"
#include "persistence.h"

static volatile bool running = true;

void signal_handler(int) { running = false; }

// 命令处理器：根据 RESP 命令数组执行对应操作，返回 RESP 响应字符串
std::string execute_command(const std::vector<std::string>& args, KVStore& store, Persistence& persist) {
    if (args.empty()) return RespValue::error("ERR empty command");

    std::string cmd = args[0];
    for (auto& c : cmd) c = toupper(c); // Redis 命令不区分大小写

    try {
        // ---- KEY 命令 ----
        if (cmd == "PING") {
            return "+PONG\r\n";
        }

        if (cmd == "DEL" && args.size() >= 2) {
            int count = 0;
            for (size_t i = 1; i < args.size(); i++) {
                if (store.del(args[i])) count++;
            }
            return RespValue::make_int(count);
        }

        if (cmd == "EXISTS" && args.size() == 2) {
            return RespValue::make_int(store.exists(args[1]) ? 1 : 0);
        }

        if (cmd == "KEYS" && args.size() == 2) {
            auto keys = store.keys(args[1]);
            return RespValue::array_str(keys);
        }

        if (cmd == "EXPIRE" && args.size() == 3) {
            return RespValue::make_int(store.expire(args[1], std::stoi(args[2])) ? 1 : 0);
        }

        if (cmd == "TTL" && args.size() == 2) {
            return RespValue::make_int(store.ttl(args[1]));
        }

        // ---- STRING 命令 ----
        if (cmd == "SET" && args.size() == 3) {
            store.set(args[1], args[2]);
            persist.append("SET " + args[1] + " " + args[2]);
            return RespValue::ok();
        }

        if (cmd == "GET" && args.size() == 2) {
            auto val = store.get(args[1]);
            return val ? RespValue::bulk(*val) : RespValue::nil();
        }

        if (cmd == "INCR" && args.size() == 2) {
            return RespValue::make_int(store.incr(args[1]));
        }

        if (cmd == "DECR" && args.size() == 2) {
            return RespValue::make_int(store.decr(args[1]));
        }

        if (cmd == "MSET" && args.size() >= 3 && args.size() % 2 == 1) {
            for (size_t i = 1; i < args.size(); i += 2) {
                store.set(args[i], args[i + 1]);
                persist.append("SET " + args[i] + " " + args[i + 1]);
            }
            return RespValue::ok();
        }

        if (cmd == "MGET" && args.size() >= 2) {
            std::vector<std::string> results;
            for (size_t i = 1; i < args.size(); i++) {
                auto val = store.get(args[i]);
                results.push_back(val ? *val : "");
            }
            return RespValue::array_str(results);
        }

        // ---- LIST 命令 ----
        if (cmd == "LPUSH" && args.size() >= 3) {
            std::vector<std::string> vals(args.begin() + 2, args.end());
            return RespValue::make_int(store.lpush(args[1], vals));
        }

        if (cmd == "RPUSH" && args.size() >= 3) {
            std::vector<std::string> vals(args.begin() + 2, args.end());
            return RespValue::make_int(store.rpush(args[1], vals));
        }

        if (cmd == "LPOP" && args.size() == 2) {
            auto val = store.lpop(args[1]);
            return val ? RespValue::bulk(*val) : RespValue::nil();
        }

        if (cmd == "RPOP" && args.size() == 2) {
            auto val = store.rpop(args[1]);
            return val ? RespValue::bulk(*val) : RespValue::nil();
        }

        if (cmd == "LRANGE" && args.size() == 4) {
            auto vals = store.lrange(args[1], std::stoi(args[2]), std::stoi(args[3]));
            return RespValue::array_str(vals);
        }

        if (cmd == "LLEN" && args.size() == 2) {
            return RespValue::make_int(store.llen(args[1]));
        }

        // ---- HASH 命令 ----
        if (cmd == "HSET" && args.size() == 4) {
            return RespValue::make_int(store.hset(args[1], args[2], args[3]));
        }

        if (cmd == "HGET" && args.size() == 3) {
            auto val = store.hget(args[1], args[2]);
            return val ? RespValue::bulk(*val) : RespValue::nil();
        }

        if (cmd == "HDEL" && args.size() >= 3) {
            std::vector<std::string> fields(args.begin() + 2, args.end());
            return RespValue::make_int(store.hdel(args[1], fields));
        }

        if (cmd == "HGETALL" && args.size() == 2) {
            auto fields = store.hgetall(args[1]);
            return RespValue::array_str(fields);
        }

        // ---- SET 命令 ----
        if (cmd == "SADD" && args.size() >= 3) {
            std::vector<std::string> members(args.begin() + 2, args.end());
            return RespValue::make_int(store.sadd(args[1], members));
        }

        if (cmd == "SMEMBERS" && args.size() == 2) {
            auto members = store.smembers(args[1]);
            return RespValue::array_str(members);
        }

        if (cmd == "SREM" && args.size() >= 3) {
            std::vector<std::string> members(args.begin() + 2, args.end());
            return RespValue::make_int(store.srem(args[1], members));
        }

        // ---- SORTED SET 命令 ----
        if (cmd == "ZADD" && args.size() == 4) {
            return RespValue::make_int(store.zadd(args[1], std::stod(args[2]), args[3]));
        }

        if (cmd == "ZRANGE" && args.size() == 4) {
            auto members = store.zrange(args[1], std::stoi(args[2]), std::stoi(args[3]));
            return RespValue::array_str(members);
        }

        if (cmd == "ZCARD" && args.size() == 2) {
            return RespValue::make_int(store.zcard(args[1]));
        }

        if (cmd == "ZSCORE" && args.size() == 3) {
            auto score = store.zscore(args[1], args[2]);
            if (score) {
                std::ostringstream ss;
                ss << *score;
                return RespValue::bulk(ss.str());
            }
            return RespValue::nil();
        }

        // ---- INFO ----
        if (cmd == "DBSIZE") {
            return RespValue::make_int(store.size());
        }

        if (cmd == "QUIT") {
            return "+BYE\r\n";
        }

    } catch (const std::exception& e) {
        return RespValue::error("ERR " + std::string(e.what()));
    }

    return RespValue::error("ERR unknown command '" + args[0] + "'");
}

int main(int argc, char* argv[]) {
    int port = 6379;
    std::string aof_file = "data.aof";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-p" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--aof" && i + 1 < argc) aof_file = argv[++i];
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    KVStore store;
    Persistence persist(aof_file);

    // 启动时恢复数据
    std::cout << "加载持久化数据...\n";
    persist.restore(store);
    std::cout << "已加载 " << store.size() << " 个 key\n";

    // Socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, SOMAXCONN);

    std::cout << "\n"
              << "  ╔══════════════════════════════════╗\n"
              << "  ║      Mini Redis v1.0 (C++17)    ║\n"
              << "  ║  支持 String/List/Hash/Set/ZSet ║\n"
              << "  ║  支持 TTL · AOF持久化 · RESP  ║\n"
              << "  ╚══════════════════════════════════╝\n\n";
    std::cout << "  监听端口: " << port << "\n";
    std::cout << "  AOF 文件: " << aof_file << "\n";
    std::cout << "  连接命令: redis-cli -p " << port << "\n\n";

    while (running) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) continue;

        RespParser parser;
        char buf[4096];

        // 接受并处理客户端命令
        timeval tv{5, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        while (running) {
            ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;

            std::string data(buf, n);
            auto cmd = parser.feed(data);

            if (cmd) {
                std::string response = execute_command(*cmd, store, persist);
                send(client, response.c_str(), response.size(), 0);

                if ((*cmd)[0] == "QUIT" || (*cmd)[0] == "quit") break;
            }
        }

        close(client);
    }

    // 关闭前保存快照
    std::cout << "\n正在关闭服务器...\n";
    persist.save(store);

    close(server_fd);
    return 0;
}
