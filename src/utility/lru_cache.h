#pragma once

#include <stddef.h>
#include <list>
#include <unordered_map>
#include <utility>

template <typename K, typename T>
class LRU_Cache 
{
private:
    typedef std::list<std::pair<const K,T>> list_t;
    list_t m_lru_list;
    std::unordered_map<K, typename list_t::iterator> m_cache;
    size_t m_max_size;
public:
    explicit LRU_Cache(size_t max_size=10) {
       m_max_size = max_size; 
    }

    size_t get_max_size(void) const {
        return m_max_size;
    }

    void set_max_size(const size_t max_size) {
        m_max_size = max_size;
        remove_lru();
    }

    T* find(const K& key) {
        auto res = m_cache.find(key);
        if (res == m_cache.end()) {
            return nullptr;
        }

        auto it = res->second;
        promote(it);
        return &it->second;
    }

    T& insert(K key, T&& val) {
        auto res = m_cache.find(key);
        if (res == m_cache.end()) {
            remove_lru();
            m_lru_list.push_front({key, val});
            res = m_cache.insert({key, m_lru_list.begin()}).first; 
        }
        auto it = res->second;
        promote(it);
        return it->second;
    }

    template <typename ... U>
    T& emplace(K key, U&& ... args) {
        auto res = m_cache.find(key);
        if (res == m_cache.end()) {
            remove_lru();
            // std::pair has the following constructor signature
            m_lru_list.emplace_front(
                std::piecewise_construct, 
                std::forward_as_tuple(key), 
                std::forward_as_tuple(std::forward<U>(args)...));
            res = m_cache.insert({key, m_lru_list.begin()}).first; 
        }
        auto it = res->second;
        promote(it);
        return it->second;
    }

    auto begin() {
        return m_lru_list.begin();
    }

    auto end() {
        return m_lru_list.end();
    }
private:
    // cull list elements past max size
    void remove_lru(void) {
        if (m_lru_list.size() <= m_max_size) {
            return;
        }

        auto _start = m_lru_list.begin();
        auto _end = m_lru_list.end();
        std::advance(_start, m_max_size);
        for (auto it = _start; it != _end; it++) {
            auto key = it->first;
            m_cache.erase(key);
        }
        m_lru_list.erase(_start, _end);
    }

    // move list element to the front
    void promote(typename list_t::iterator& it) {
        m_lru_list.splice(m_lru_list.begin(), m_lru_list, it);
    }
};