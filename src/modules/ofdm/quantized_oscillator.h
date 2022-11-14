#pragma once

#include <complex>
#include <vector>

// Precompute the local oscillator so that we dont have to compute cos(x) + 1j*sin(x) 
class QuantizedOscillator 
{
private:
    std::vector<std::complex<float>> table;
    const size_t Fsample;
    const size_t Fresolution;
    const size_t table_size;
public:
    QuantizedOscillator(const size_t _Fres=1, const size_t _Fsample=2048000);
    size_t GetFrequencyResolution() const { 
        return Fresolution; 
    }
    const auto& operator[](const size_t index) {
        return table[index];
    }
    size_t GetTableSize() const { return table_size; }
};