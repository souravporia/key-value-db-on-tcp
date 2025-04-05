/**
 * @file KVStore.h
 * @brief A simple key-value store with thread-safe operations and persistence.
 */
#ifndef KV_STORE_H
#define KV_STORE_H

#include "ankerl/unordered_dense.h"
#include <vector>
#include <shared_mutex>
#include <atomic>
#include <optional>
#include <string>
#include <mutex>
#include <fstream>
#include <iostream>

 /**
 * @class KVStore
 * @brief A thread-safe key-value store with optional persistence.
 */
class KVStore {
private:
    ankerl::unordered_dense::map<std::string, std::string> data;
    std::shared_mutex mutex;
    std::string filename = "kvstore.dat";
    /**
     * @brief Constructs a KVStore and loads data from disk if available.
     */
    void loadFromDisk() {
        std::ifstream inFile(filename, std::ios::binary);
        if (!inFile) return;

        std::unique_lock lock(mutex);
        data.clear();
        
        size_t size;
        while (inFile.read(reinterpret_cast<char*>(&size), size > 0)) {
            std::string key(size, '\0');
            inFile.read(&key[0], size);
            
            inFile.read(reinterpret_cast<char*>(&size), size);
            std::string value(size, '\0');
            inFile.read(&value[0], size);
            
            data[key] = value;
        }
    }

public:
    KVStore() {
        loadFromDisk();
    }

    /**
     * @brief Stores a key-value pair.
     * @param key The key to store.
     * @param value The value associated with the key.
     */
    void set(const std::string& key, const std::string& value) {
        std::unique_lock lock(mutex);
        data[key] = value;
    }

    /**
     * @brief Retrieves a value by key.
     * @param key The key to look up.
     * @return The value if found, otherwise std::nullopt.
     */
    std::optional<std::string> get(const std::string& key) {
        std::shared_lock lock(mutex);
        auto it = data.find(key);
        if (it != data.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Deletes a key from the store.
     * @param key The key to delete.
     * @return True if the key was deleted, false otherwise.
     */
    bool del(const std::string& key) {
        std::unique_lock lock(mutex);
        return data.erase(key) > 0;
    }

    /**
     * @brief Saves the current key-value store to disk.
     * @throws std::runtime_error if file operations fail.
     */
    void persistToDisk() {
        std::ofstream outFile(filename, std::ios::binary | std::ios::trunc);
        if (!outFile) {
            throw std::runtime_error("Failed to open file for writing");
        }

        std::shared_lock lock(mutex);
        for (const auto& [key, value] : data) {
            size_t size = key.size();
            outFile.write(reinterpret_cast<const char*>(&size), sizeof(size));
            outFile.write(key.data(), size);
            
            size = value.size();
            outFile.write(reinterpret_cast<const char*>(&size), sizeof(size));
            outFile.write(value.data(), size);
        }
    }
};

#endif // KV_STORE_H