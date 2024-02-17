#pragma once

#include "utility/span.h"
#include "viterbi_config.h"

class Basic_MSC_Runner {
public:
    virtual ~Basic_MSC_Runner() {};
    virtual void Process(tcb::span<const viterbi_bit_t> msc_bits_buf) = 0;
};
