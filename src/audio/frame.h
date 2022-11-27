#pragma once

constexpr int TOTAL_AUDIO_CHANNELS = 2;

// Stereo audio sample of type T
template <typename T>
struct Frame {
    T channels[TOTAL_AUDIO_CHANNELS];

    Frame<T> operator+(const Frame<T>& other) const {
        Frame<T> res;
        for (int i = 0; i < TOTAL_AUDIO_CHANNELS; i++) {
            res.channels[i] = channels[i] + other.channels[i];
        }
        return res;
    }

    Frame<T> operator-(const Frame<T>& other) const {
        Frame<T> res;
        for (int i = 0; i < TOTAL_AUDIO_CHANNELS; i++) {
            res.channels[i] = channels[i] - other.channels[i];
        }
        return res;
    }

    Frame<T>& operator+=(const Frame<T>& other) {
        for (int i = 0; i < TOTAL_AUDIO_CHANNELS; i++) {
            channels[i] += other.channels[i];
        }
        return *this;
    }

    Frame<T>& operator-=(const Frame<T>& other) {
        for (int i = 0; i < TOTAL_AUDIO_CHANNELS; i++) {
            channels[i] -= other.channels[i];
        }
        return *this;
    }

    template <typename U>
    Frame<T> operator*(const U v) const {
        Frame<T> res;
        for (int i = 0; i < TOTAL_AUDIO_CHANNELS; i++) {
            res.channels[i] = (T)(channels[i]*v);
        }
        return res;
    }

    template <typename U>
    Frame<T> operator/(const U v) const {
        Frame<T> res;
        for (int i = 0; i < TOTAL_AUDIO_CHANNELS; i++) {
            res.channels[i] = (T)(channels[i]/v);
        }
        return res;
    }

    template <typename U>
    operator Frame<U>() const {
        Frame<U> res;
        for (int i = 0; i < TOTAL_AUDIO_CHANNELS; i++) {
            res.channels[i] = static_cast<U>(channels[i]);
        }
        return res;
    }
};