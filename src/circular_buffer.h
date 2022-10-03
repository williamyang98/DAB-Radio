#pragma once

template <typename T>
class CircularBuffer
{
private:
    T* buf;
    const int capacity;
    int length;
    int index;
public:
    CircularBuffer(const int _N)
    : capacity(_N) {
        length = 0;
        index = 0;
        buf = new T[capacity];
    }
    ~CircularBuffer() {
        delete [] buf;
    }
    // Read the data from a source buffer and append it to this buffer
    // We can forcefully read all the data
    inline int ConsumeBuffer(T* src, const int N, const bool read_all=false) {
        int nb_read;
        if (read_all) {
            nb_read = N;
        } else {
            const int N_remain = capacity-length;
            nb_read = (N > N_remain) ? N_remain : N;
        }

        for (int i = 0; i < nb_read; i++) {
            buf[index++] = src[i];
            index = index % capacity;
        }
        length += nb_read;
        if (length > capacity) {
            length = capacity;
        }
        return nb_read;
    }
    // index the circular buffer with wrap-around
    inline T& At(int i) { 
        return buf[i % capacity]; 
    }
    inline void Reset() {
        length = 0;
        index = 0;
    }
    inline void SetLength(int N) { length = N; }
    inline int Length() const { return length; }
    inline int Capacity() const { return capacity; }
    inline int GetIndex() const { return index; }
    inline bool IsEmpty() const { return length == 0; }
    inline bool IsFull() const { return length == capacity; }
};