// Distributed LRU Cache with ZooKeeper Registration

#include <iostream>
#include <unordered_map>
#include <list>
#include <vector>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <cstring>
#define THREADED
extern "C" {
    #include <zookeeper/zookeeper.h>
}

using namespace std;

zhandle_t* zkHandle = nullptr;

void watcher(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx) {
    if (type == ZOO_SESSION_EVENT && state == ZOO_CONNECTED_STATE) {
        cout << "[ZK] Connected to ZooKeeper." << endl;
    }
}

void connectToZooKeeper() {
    zkHandle = zookeeper_init("127.0.0.1:2181", watcher, 30000, 0, 0, 0);
    if (!zkHandle) {
        cerr << "[ZK] Failed to connect to ZooKeeper!" << endl;
        exit(1);
    }
}

class LRUCache {
private:
    size_t capacity;
    list<pair<string, string>> lruList;
    unordered_map<string, list<pair<string, string>>::iterator> cacheMap;
    mutable shared_mutex cacheMutex;

public:
    LRUCache(size_t cap) : capacity(cap) {}

    string get(const string& key) {
        shared_lock<shared_mutex> readLock(cacheMutex);
        if (cacheMap.find(key) == cacheMap.end()) return "[MISS]";
        lruList.splice(lruList.begin(), lruList, cacheMap[key]);
        return cacheMap[key]->second;
    }

    void put(const string& key, const string& value) {
        unique_lock<shared_mutex> writeLock(cacheMutex);
        if (cacheMap.find(key) != cacheMap.end()) {
            lruList.splice(lruList.begin(), lruList, cacheMap[key]);
            cacheMap[key]->second = value;
        } else {
            if (lruList.size() >= capacity) {
                auto last = lruList.back();
                cacheMap.erase(last.first);
                lruList.pop_back();
            }
            lruList.push_front({key, value});
            cacheMap[key] = lruList.begin();
        }
    }
};

class CacheNode {
public:
    string nodeId;
    bool isPrimary;
    LRUCache cache;

    CacheNode(string id, bool primary, size_t size)
        : nodeId(id), isPrimary(primary), cache(size) {
        registerWithZookeeper();
    }

    void registerWithZookeeper() {
        string path = "/cache/" + nodeId;
        string data = isPrimary ? "primary" : "replica";
        int rc = zoo_create(zkHandle, path.c_str(), data.c_str(), data.length(),
                            &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL, nullptr, 0);
        if (rc != ZOK) {
            cerr << "[ZK] Failed to register node " << nodeId << " with ZooKeeper! Error code: " << rc << endl;
        } else {
            cout << "[ZK] Registered node " << nodeId << " at path " << path << endl;
        }
    }

    string retrieve(const string& key) {
        return cache.get(key);
    }

    void insert(const string& key, const string& value) {
        cache.put(key, value);
    }
};

class ShardManager {
private:
    vector<CacheNode*> primaries;
    vector<vector<CacheNode*>> replicas;
    vector<bool> primaryStatus;

public:
    void registerPrimary(CacheNode* node) {
        primaries.push_back(node);
        primaryStatus.push_back(true);
    }

    void registerReplicas(int index, vector<CacheNode*> replicaList) {
        if (index >= replicas.size()) replicas.resize(index + 1);
        replicas[index] = replicaList;
    }

    void disablePrimary(size_t idx) {
        if (idx < primaryStatus.size()) {
            primaryStatus[idx] = false;
            cout << "[WARNING] Primary shard " << idx << " is DOWN!" << endl;
        }
    }

    size_t getShardIndex(const string& key) {
        hash<string> hasher;
        return hasher(key) % primaries.size();
    }

    void insert(const string& key, const string& value) {
        size_t idx = getShardIndex(key);
        if (primaryStatus[idx]) primaries[idx]->insert(key, value);
        for (auto* replica : replicas[idx]) {
            replica->insert(key, value);
        }
    }

    string retrieve(const string& key) {
        size_t idx = getShardIndex(key);
        if (primaryStatus[idx]) {
            string val = primaries[idx]->retrieve(key);
            if (val != "[MISS]") return val;
        }
        for (auto* replica : replicas[idx]) {
            string val = replica->retrieve(key);
            if (val != "[MISS]") return val;
        }
        return "[MISS:DB fallback required]";
    }
};

class APIService {
private:
    ShardManager& shardManager;

public:
    APIService(ShardManager& manager) : shardManager(manager) {}

    void insertAPI(const string& key, const string& value) {
        cout << "[API] Inserting key=" << key << ", value=" << value << endl;
        shardManager.insert(key, value);
        cout << "[200 OK]" << endl;
    }

    void retrieveAPI(const string& key) {
        cout << "[API] Retrieving key=" << key << endl;
        string val = shardManager.retrieve(key);
        cout << "[Result] " << val << endl;
    }
};

int main() {
    connectToZooKeeper();

    const int NUM_SHARDS = 2;
    const int REPLICAS_PER_SHARD = 2;
    const int CACHE_CAPACITY = 3;

    ShardManager manager;

    for (int i = 0; i < NUM_SHARDS; ++i) {
        CacheNode* primary = new CacheNode("P" + to_string(i), true, CACHE_CAPACITY);
        manager.registerPrimary(primary);

        vector<CacheNode*> replicaList;
        for (int r = 0; r < REPLICAS_PER_SHARD; ++r) {
            CacheNode* replica = new CacheNode("R" + to_string(i) + to_string(r), false, CACHE_CAPACITY);
            replicaList.push_back(replica);
        }
        manager.registerReplicas(i, replicaList);
    }

    APIService api(manager);

    api.insertAPI("user:1", "Yash");
    api.insertAPI("user:2", "Suraj");
    api.insertAPI("user:3", "Sudhanshu");
    api.insertAPI("user:4", "Dubey");

    api.retrieveAPI("user:1");
    api.retrieveAPI("user:2");
    api.retrieveAPI("user:3");
    api.retrieveAPI("user:4");
    api.retrieveAPI("user:5");

    size_t idx = manager.getShardIndex("user:1");
    manager.disablePrimary(idx);

    api.retrieveAPI("user:1");

    return 0;
}
