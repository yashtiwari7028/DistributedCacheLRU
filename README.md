# 🧠 Distributed LRU Cache System with ZooKeeper

This project implements a Distributed Least Recently Used (LRU) Cache system in C++ that:
- Uses ZooKeeper for service discovery and metadata registration.
- Supports sharding and replication.
- Ensures thread-safe read/write access using `shared_mutex`.
- Includes a simple API simulation layer.

---

## 📦 Features

- Distributed Architecture: Multiple shards, each with a primary cache and two replicas.
- Thread Safety: Uses `shared_mutex` for concurrent read/write access.
- ZooKeeper Integration: Each cache node registers itself in ZooKeeper (`/cache/{nodeId}`).
- Sharding: Keys are hashed to shards using a modulo operation.
- Failover Support: If the primary shard is down, replicas are used.
- Simulation: Simple CLI-based API (`insertAPI`, `retrieveAPI`).

---



