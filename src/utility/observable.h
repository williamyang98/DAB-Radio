#pragma once

#include <vector>
#include <functional>


template <typename ... T>
class Observable 
{
private:
    using Observer = std::function<void(T...)>;
    std::vector<Observer> observers;
public:
    void Attach(const Observer& observer) {
        observers.push_back(observer);
    }
    void Notify(T ... args) {
        for (const auto& o: observers) {
            o(std::forward<T>(args)...);
        }
    }
};