#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <vector>
#include "utility/span.h"
#include "viterbi_config.h"
#include "./app_io_buffers.h"

static void convert_viterbi_bytes_to_bits(tcb::span<const uint8_t> bytes, tcb::span<viterbi_bit_t> bits) {
    constexpr size_t BITS_PER_BYTE = 8;
    assert(bytes.size()*BITS_PER_BYTE == bits.size());
    const size_t total_bytes = bytes.size();
    size_t curr_bit = 0;
    size_t curr_byte = 0;
    for (; curr_byte < total_bytes; curr_byte++, curr_bit+=BITS_PER_BYTE) {
        const uint8_t v = bytes[curr_byte];
        for (size_t i = 0; i < BITS_PER_BYTE; i++) {
            const uint8_t byte = (v >> i) & 0b1;
            const viterbi_bit_t bit = byte ? SOFT_DECISION_VITERBI_HIGH : SOFT_DECISION_VITERBI_LOW;
            bits[curr_bit+i] = bit;
        }
    }
}

static void convert_viterbi_bits_to_bytes(tcb::span<const viterbi_bit_t> bits, tcb::span<uint8_t> bytes) {
    constexpr size_t BITS_PER_BYTE = 8;
    assert(bytes.size()*BITS_PER_BYTE == bits.size());
    const size_t total_bytes = bytes.size();
    constexpr viterbi_bit_t MID_POINT = (SOFT_DECISION_VITERBI_HIGH+SOFT_DECISION_VITERBI_LOW)/2;
    size_t curr_bit = 0;
    size_t curr_byte = 0;
    for (; curr_byte < total_bytes; curr_byte++, curr_bit+=BITS_PER_BYTE) {
        uint8_t v = 0;
        for (size_t i = 0; i < BITS_PER_BYTE; i++) {
            const viterbi_bit_t bit = bits[curr_bit+i];
            const uint8_t byte = (bit >= MID_POINT) ? 0b1 : 0b0;
            v |= (byte << i);
        }
        bytes[curr_byte] = v; 
    }
}

class Convert_Viterbi_Bits_to_Bytes: public InputBuffer<uint8_t>, public OutputBuffer<uint8_t>
{
private:
    std::shared_ptr<InputBuffer<viterbi_bit_t>> m_input = nullptr;
    std::shared_ptr<OutputBuffer<viterbi_bit_t>> m_output = nullptr;
    std::vector<viterbi_bit_t> m_bits_buffer;
public:
    Convert_Viterbi_Bits_to_Bytes() {}
    ~Convert_Viterbi_Bits_to_Bytes() override = default;
    void reserve_bits(size_t length) {
        m_bits_buffer.reserve(length);
    }
    void set_input_stream(std::shared_ptr<InputBuffer<viterbi_bit_t>> input) { 
        m_input = input; 
    }
    void set_output_stream(std::shared_ptr<OutputBuffer<viterbi_bit_t>> output) { 
        m_output = output; 
    }
    size_t read(tcb::span<uint8_t> bytes_buffer) override {
        constexpr size_t BITS_PER_BYTE = 8;
        if (m_input == nullptr) return 0;
        m_bits_buffer.resize(bytes_buffer.size()*BITS_PER_BYTE);
        const size_t length = m_input->read(m_bits_buffer);
        assert(length % BITS_PER_BYTE == 0);
        const size_t total_bits = length - (length % BITS_PER_BYTE);
        const size_t total_bytes = total_bits / BITS_PER_BYTE;
        convert_viterbi_bits_to_bytes(
            tcb::span(m_bits_buffer).first(total_bits),
            bytes_buffer.first(total_bytes)
        );
        return total_bytes;
    }
    size_t write(tcb::span<const uint8_t> bytes_buffer) override {
        constexpr size_t BITS_PER_BYTE = 8;
        if (m_output == nullptr) return 0;
        m_bits_buffer.resize(bytes_buffer.size()*BITS_PER_BYTE);
        convert_viterbi_bytes_to_bits(bytes_buffer, m_bits_buffer);
        const size_t total_bits = m_output->write(m_bits_buffer);
        assert(total_bits % BITS_PER_BYTE == 0);
        const size_t total_bytes = total_bits / 8;
        return total_bytes;
    };
};

class Convert_Viterbi_Bytes_to_Bits: public InputBuffer<viterbi_bit_t>, public OutputBuffer<viterbi_bit_t>
{
private:
    std::shared_ptr<InputBuffer<uint8_t>> m_input = nullptr;
    std::shared_ptr<OutputBuffer<uint8_t>> m_output = nullptr;
    std::vector<uint8_t> m_bytes_buffer;
public:
    Convert_Viterbi_Bytes_to_Bits() {}
    ~Convert_Viterbi_Bytes_to_Bits() override = default;
    void reserve_bytes(size_t length) {
        m_bytes_buffer.reserve(length);
    }
    void set_input_stream(std::shared_ptr<InputBuffer<uint8_t>> input) { 
        m_input = input; 
    }
    void set_output_stream(std::shared_ptr<OutputBuffer<uint8_t>> output) {
        m_output = output;
    }
    size_t read(tcb::span<viterbi_bit_t> bits_buffer) override {
        constexpr size_t BITS_PER_BYTE = 8;
        if (m_input == nullptr) return 0;
        assert(bits_buffer.size() % BITS_PER_BYTE == 0);
        const size_t max_bits = bits_buffer.size() - (bits_buffer.size() % BITS_PER_BYTE);
        bits_buffer = bits_buffer.first(max_bits);
        m_bytes_buffer.resize(bits_buffer.size()/BITS_PER_BYTE);
        const size_t total_bytes = m_input->read(m_bytes_buffer);
        const size_t total_bits = total_bytes * BITS_PER_BYTE;
        convert_viterbi_bytes_to_bits(
            tcb::span(m_bytes_buffer).first(total_bytes),
            bits_buffer.first(total_bits)
        );
        return total_bits;
    }
    size_t write(tcb::span<const viterbi_bit_t> bits_buffer) override {
        constexpr size_t BITS_PER_BYTE = 8;
        if (m_output == nullptr) return 0;
        assert(bits_buffer.size() % BITS_PER_BYTE == 0);
        const size_t max_bits = bits_buffer.size() - (bits_buffer.size() % BITS_PER_BYTE);
        bits_buffer = bits_buffer.first(max_bits);
        m_bytes_buffer.resize(bits_buffer.size()/BITS_PER_BYTE);
        convert_viterbi_bits_to_bytes(bits_buffer, m_bytes_buffer);
        const size_t total_bytes = m_output->write(m_bytes_buffer);
        const size_t total_bits = total_bytes * BITS_PER_BYTE;
        return total_bits;
    };
};

