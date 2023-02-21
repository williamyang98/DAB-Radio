#pragma once

template <typename T>
inline 
T abs(T x) {
    return (x > 0) ? x : -x;
}

template <typename T>
inline 
T min(T x0, T x1) {
    return (x0 > x1) ? x1 : x0;
}

template <typename T>
inline 
T max(T x0, T x1) {
    return (x0 > x1) ? x0 : x1;
}

template <typename T>
inline
T clamp(const T x, const T min, const T max) {
    T y = x;
    y = (y > min) ? y : min;
    y = (y > max) ? max : y;
    return y;
}
