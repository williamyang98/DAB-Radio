#pragma once

#include <stdint.h>

// We are using soft decision decoding in our Viterbi decoder
// viterbi_bit_t is the type we quantise to
// this is done since our viterbi decoder works with int16_t soft decision bits to use faster sse2 instructions
// OFDM_Demodulator -> float bits[] -> viterbi_bit bits[] -> ... -> ViterbiDecoder
// NOTE: int16_t is in the decoding stage since it stores the accumulated error metric which would overflow with int8
// NOTE: we are using a global typedef since using templates would blow out compile times
typedef int8_t viterbi_bit_t;
static constexpr viterbi_bit_t SOFT_DECISION_VITERBI_HIGH      = +127;
static constexpr viterbi_bit_t SOFT_DECISION_VITERBI_LOW       = -127;
static constexpr viterbi_bit_t SOFT_DECISION_VITERBI_PUNCTURED = 0;
