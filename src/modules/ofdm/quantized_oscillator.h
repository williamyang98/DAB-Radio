#pragma once

#include <complex>

// Precompute the local oscillator so that we dont have to compute cos(x) + 1j*sin(x) 
class QuantizedOscillator 
{
private:
    std::complex<float>* table;
    const int Fsample;
    const int Fresolution;
    const int table_size;
public:
    QuantizedOscillator(const int _Fres=1, const int _Fsample=2048000);
    ~QuantizedOscillator();
    inline int GetFrequencyResolution() const { 
        return Fresolution; 
    }
    inline const auto& At(const int index) {
        return table[index];
    }
    inline int GetTableSize() const { return table_size; }
};