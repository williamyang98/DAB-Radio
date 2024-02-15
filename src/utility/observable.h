#pragma once

#include <vector>
#include <functional>


template <typename ... T>
class Observable 
{
private:
    using Observer = std::function<void(T...)>;
    std::vector<Observer> m_observers;
public:
    void Attach(const Observer& observer) {
        m_observers.push_back(observer);
    }
    void Notify(T ... args) {
        for (const auto& o: m_observers) {
            o(std::forward<T>(args)...);
        }
    }
};