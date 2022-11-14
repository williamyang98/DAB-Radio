#include "quantized_oscillator.h"
#include <cmath>

QuantizedOscillator::QuantizedOscillator(
    const size_t _Fres, const size_t _Fsample) 
: Fresolution(_Fres), Fsample(_Fsample),
    table_size(_Fsample/_Fres)
{
    table.resize(table_size);

    const float Ts = 1.0f/static_cast<float>(Fsample);
    const float TWO_PI = 2.0f * 3.14159265f;
    const float step = TWO_PI * static_cast<float>(Fresolution) * Ts;
    float dx = 0.0f;
    for (int i = 0; i < table_size; i++) {
        table[i] = std::complex<float>(
            static_cast<float>(std::cos(dx)), 
            static_cast<float>(std::sin(dx)));
        dx += step;
    }
}