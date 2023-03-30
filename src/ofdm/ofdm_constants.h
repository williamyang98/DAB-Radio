#pragma once

// NOTE: Fixed rate sample rate for OFDM
constexpr float Fs = 2.048e6f;
constexpr float Ts = 1.0f/Fs;
constexpr float Fmax_wrap = 1.5e3f; // NOTE: Avoid loss of precision when computing sin or cos at high frequencies