#pragma once
#include <list>
#include <unordered_map>
#include <functional>
#include <utility>

template<typename Key, typename Val>
class LRUCache {
public:
    using value_deinit_callback = std::function<void(const Key&, Val*)>;

    // 构造函数：修复默认参数
    LRUCache(int capacity, value_deinit_callback func = nullptr)
        : capacity_(capacity), value_deinit_func_(std::move(func)) {}

    ~LRUCache() {
        for (auto& pairs : cache_list_) {
            if (value_deinit_func_) {
                value_deinit_func_(pairs.first, pairs.second);
            } else {
                delete pairs.second;  // 默认行为
            }
        }
        hasht_.clear();
        cache_list_.clear();
    }

    Val* Get(const Key& key) {
        auto iter = hasht_.find(key);
        if (iter == hasht_.end()) {
            return nullptr;
        }
        
        // 移动到链表尾部（最近使用）
        auto& list_iter = iter->second;
        auto* pairs = *list_iter;           // pairs 是 std::pair<Key, Val*>*
        cache_list_.erase(list_iter);
        list_iter = cache_list_.insert(cache_list_.end(), pairs);
        return pairs->second;               // 返回 Val*
    }

    void Put(const Key& key, Val* val) {
        if (!val) return;  // 防御
        
        auto iter = hasht_.find(key);
        if (iter != hasht_.end()) {
            // Key 已存在：更新值
            auto& list_iter = iter->second;
            auto* old_pairs = *list_iter;
            
            // 释放旧值
            if (value_deinit_func_) {
                value_deinit_func_(old_pairs->first, old_pairs->second);
            } else {
                delete old_pairs->second;
            }
            
            // 更新为新值
            old_pairs->second = val;
            
            // 移到尾部
            cache_list_.erase(list_iter);
            list_iter = cache_list_.insert(cache_list_.end(), old_pairs);
            
        } else {
            // Key 不存在：插入新条目
            if (cache_list_.size() >= static_cast<size_t>(capacity_)) {
                // 淘汰最久未使用（链表头）
                auto* front_pairs = cache_list_.front();
                auto& front_key = front_pairs->first;
                auto& front_val = front_pairs->second;
                
                // 从哈希表移除
                hasht_.erase(front_key);
                
                // 释放值
                if (value_deinit_func_) {
                    value_deinit_func_(front_key, front_val);
                } else {
                    delete front_val;
                }
                
                // 从链表移除
                delete front_pairs;  // 释放 pair 本身
                cache_list_.pop_front();
            }
            
            // 插入新条目
            auto* new_pairs = new std::pair<Key, Val*>(key, val);
            auto list_iter = cache_list_.insert(cache_list_.end(), new_pairs);
            hasht_.emplace(key, list_iter);
        }
    }

    // 额外：查看缓存大小
    size_t Size() const { return cache_list_.size(); }
    
    // 额外：清空缓存
    void Clear() {
        for (auto* pairs : cache_list_) {
            if (value_deinit_func_) {
                value_deinit_func_(pairs->first, pairs->second);
            } else {
                delete pairs->second;
            }
            delete pairs;
        }
        cache_list_.clear();
        hasht_.clear();
    }

private:
    using ListIter = typename std::list<std::pair<Key, Val*>*>::iterator;
    
    std::unordered_map<Key, ListIter> hasht_;
    std::list<std::pair<Key, Val*>*> cache_list_;  // 存储指针
    int capacity_;
    value_deinit_callback value_deinit_func_;
};