#pragma once

#include <list>
#include <unordered_map>

template <typename K, typename T>
class LRU_Cache 
{
private:
    typedef std::list<std::pair<const K,T>> list_t;
    list_t lru_list;
    std::unordered_map<K, typename list_t::iterator> cache;
    size_t max_size;
public:
    explicit LRU_Cache(size_t _max_size=10) {
       max_size = _max_size; 
    }

    size_t get_max_size(void) const {
        return max_size;
    }

    void set_max_size(const size_t _max_size) {
        max_size = _max_size;
        remove_lru();
    }

    T* find(const K& key) {
        auto res = cache.find(key);
        if (res == cache.end()) {
            return NULL;
        }

        auto it = res->second;
        promote(it);
        return &it->second;
    }

    T& insert(K key, T&& val) {
        auto res = cache.find(key);
        if (res == cache.end()) {
            remove_lru();
            lru_list.push_front({key, val});
            res = cache.insert({key, lru_list.begin()}).first; 
        }
        auto it = res->second;
        promote(it);
        return it->second;
    }

    template <typename ... U>
    T& emplace(K key, U&& ... args) {
        auto res = cache.find(key);
        if (res == cache.end()) {
            remove_lru();
            // std::pair has the following constructor signature
            lru_list.emplace_front(
                std::piecewise_construct, 
                std::forward_as_tuple(key), 
                std::forward_as_tuple(std::forward<U>(args)...));
            res = cache.insert({key, lru_list.begin()}).first; 
        }
        auto it = res->second;
        promote(it);
        return it->second;
    }

    auto begin() {
        return lru_list.begin();
    }

    auto end() {
        return lru_list.end();
    }
private:
    // cull list elements past max size
    void remove_lru(void) {
        if (lru_list.size() <= max_size) {
            return;
        }

        auto _start = lru_list.begin();
        auto _end = lru_list.end();
        std::advance(_start, max_size);
        for (auto it = _start; it != _end; it++) {
            auto key = it->first;
            cache.erase(key);
        }
        lru_list.erase(_start, _end);
    }

    // move list element to the front
    void promote(typename list_t::iterator& it) {
        lru_list.splice(lru_list.begin(), lru_list, it);
    }
};