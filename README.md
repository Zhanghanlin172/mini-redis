# Mini Redis

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](https://en.cppreference.com/)
[![License](https://img.shields.io/badge/license-MIT-green)]()

用 C++17 从零实现的 Redis 兼容键值存储系统，支持 5 种数据结构、TTL 过期、AOF 持久化。

## 特性

- **RESP 协议**：完整实现 Redis 序列化协议（REdis Serialization Protocol），兼容 `redis-cli`
- **5 种数据结构**：String、List（双端链表）、Hash、Set、Sorted Set（有序集合）
- **TTL 过期**：基于 `steady_clock` 的键过期机制，支持 EXPIRE/TTL 命令
- **AOF 持久化**：追加写日志，重启自动恢复数据
- **线程安全**：`shared_mutex` 读写锁保护，支持并发访问
- **命令行兼容**：直接使用标准 `redis-cli` 连接操作

## 快速开始

```bash
git clone https://github.com/Zhanghanlin172/mini-redis.git
cd mini-redis
make
./mini-redis -p 6379
```

然后用 `redis-cli` 连接：

```bash
redis-cli -p 6379
> SET name Alice
OK
> GET name
"Alice"
> LPUSH queue task1 task2
(integer) 2
> ZADD ranking 99 Bob
(integer) 1
> EXPIRE name 60
(integer) 1
```

## 支持的命令

| 类型 | 命令 |
|------|------|
| Key | `DEL`, `EXISTS`, `KEYS`, `EXPIRE`, `TTL` |
| String | `SET`, `GET`, `INCR`, `DECR`, `MSET`, `MGET` |
| List | `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LRANGE`, `LLEN` |
| Hash | `HSET`, `HGET`, `HDEL`, `HGETALL` |
| Set | `SADD`, `SMEMBERS`, `SREM` |
| Sorted Set | `ZADD`, `ZRANGE`, `ZCARD`, `ZSCORE` |
| Server | `PING`, `DBSIZE`, `QUIT` |

## 架构

```
redis-cli ──► Socket ──► RespParser ──► Command Router
                                            │
                              ┌─────────────┼─────────────┐
                              │             │             │
                          KVStore      Persistence     TTL Manager
                     (shared_mutex)    (AOF file)    (steady_clock)
```

## 技术栈

- C++17
- POSIX Socket API
- `shared_mutex` 读写锁
- AOF 持久化

## 相关项目

- [cpp-http-server](https://github.com/Zhanghanlin172/cpp-http-server) — C++ 多线程 HTTP 服务器
