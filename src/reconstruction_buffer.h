#pragma once

// reconstruct a block of size M from blocks of size N
template <typename T>
class ReconstructionBuffer 
{
private:
    T* buf;
    const int capacity;
    int length;
public:
    ReconstructionBuffer(const int _N)
    : capacity(_N), length(0) {
        buf = new T[capacity];
    }
    ~ReconstructionBuffer() {
        delete [] buf;
    }
    // Read the data from a source buffer and append it to this buffer
    int ConsumeBuffer(T* src, const int N) {
        const int N_required = capacity-length;
        const int nb_read = (N_required >= N) ? N : N_required;
        for (int i = 0; i < nb_read; i++) {
            buf[length++] = src[i];
        }
        return nb_read;
    }
    inline T& At(int i) { return buf[i]; }
    inline T* GetData() const { return buf; }
    inline void Reset() { length = 0; };
    inline void SetLength(int N) { length = N; }
    inline int Length() const { return length; }
    inline int Capacity() const { return capacity; }
    inline bool IsEmpty() const { return length == 0; }
    inline bool IsFull() const { return length == capacity; }
};