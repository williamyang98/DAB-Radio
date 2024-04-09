#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <cstring>
#include "utility/span.h"
#include "./ofdm_params.h"

// Purpose of this class if to provide the necessary alignment to each symbol
// so that FFTW3 can use SIMD to accelerate the FFT/IFFT
//
// Our OFDM frame has the following format:
//      Frame         => PRS | (Data Symbol) x N | (NULL Symbol)
//      PRS           => Cyclic prefix | FFT data                  
//      (Data Symbol) => Cyclic prefix | FFT data
//
// We add padding to get the required alignment
// 
// NOTE: Template this if for some reason we change the type of std::complex<T>
template <typename T>
class OFDM_Frame_Buffer 
{
private:
    tcb::span<uint8_t>& m_buf;
    const OFDM_Params m_params;
    const size_t m_align_size;
    // calculated alignment sizes
    const size_t m_prefix_size;
    const size_t m_data_symbol_size;
    const size_t m_null_symbol_size;
    const size_t m_aligned_data_prefix_padding;
    const size_t m_aligned_data_symbol_stride;
    const size_t m_total_aligned_bytes;
    // keep track of which symbols are filled
    size_t m_curr_symbol_index;
    size_t m_curr_symbol_samples; 
public:
    // NOTE: We take in a reference to a span since the buffer is allocated separately 
    //       The underlying buffer should be resized to GetTotalBufferBytes()
    OFDM_Frame_Buffer(const OFDM_Params& params, tcb::span<uint8_t>& buf, const size_t align_size)
    :   m_buf(buf), 
        m_params(params), 
        m_align_size(align_size),
        m_prefix_size(sizeof(T) * params.nb_cyclic_prefix),
        m_data_symbol_size(sizeof(T) * params.nb_symbol_period),
        m_null_symbol_size(sizeof(T) * params.nb_null_period),
        // Add left padding before prefix so start of FFT data is aligned
        m_aligned_data_prefix_padding(GetAligned(m_prefix_size, m_align_size)-m_prefix_size),
        // Add right padding after FFT data so consecutive symbols are aligned
        m_aligned_data_symbol_stride(GetAligned(m_aligned_data_prefix_padding + m_data_symbol_size, m_align_size)),
        // Size of padded and aligned byte buffer
        m_total_aligned_bytes(m_aligned_data_symbol_stride*(m_params.nb_frame_symbols+1) + (m_null_symbol_size-m_data_symbol_size))
    {
        Reset();
    }

    size_t GetTotalBufferBytes() const { 
        return m_total_aligned_bytes; 
    }

    size_t GetAlignment() const { 
        return m_align_size; 
    }

    void Reset() {
        m_curr_symbol_index = 0;
        m_curr_symbol_samples = 0;
    }

    bool IsFull() const { 
        return m_curr_symbol_index == (m_params.nb_frame_symbols+1);
    }

    size_t ConsumeBuffer(tcb::span<const T> src) {
        assert(!m_buf.empty());
        assert(m_buf.size() == GetTotalBufferBytes());
        size_t nb_read = 0;
        while (!src.empty() && !IsFull()) {
            const size_t N = Consume(src);
            nb_read += N;
            src = src.subspan(N);
        }
        return nb_read;
    }

    tcb::span<T> GetDataSymbol(const size_t index) {
        const size_t offset = index*m_aligned_data_symbol_stride + m_aligned_data_prefix_padding;
        auto* wr_buf = reinterpret_cast<T*>(&m_buf[offset]);
        return { wr_buf, m_params.nb_symbol_period };
    }

    tcb::span<T> GetNullSymbol() {
        const size_t offset = 
            m_params.nb_frame_symbols*m_aligned_data_symbol_stride + 
            m_aligned_data_prefix_padding;
        auto* wr_buf = reinterpret_cast<T*>(&m_buf[offset]);
        return { wr_buf, m_params.nb_null_period };
    };
private:
    inline 
    size_t Consume(tcb::span<const T> src) {
        auto sym_buf = 
            (m_curr_symbol_index < m_params.nb_frame_symbols) ? 
            GetDataSymbol(m_curr_symbol_index) : GetNullSymbol();

        const size_t nb_capacity = sym_buf.size();
        const size_t nb_required = nb_capacity-m_curr_symbol_samples;
        const size_t nb_read = (src.size() > nb_required) ? nb_required : src.size();

        auto wr_buf = sym_buf.subspan(m_curr_symbol_samples, nb_read);
        std::memcpy(wr_buf.begin(), src.begin(), nb_read*sizeof(T));

        m_curr_symbol_samples += nb_read;
        // branchless math to update number of samples in OFDM frame 
        m_curr_symbol_index += (m_curr_symbol_samples / nb_capacity);
        m_curr_symbol_samples = (m_curr_symbol_samples % nb_capacity);
        return nb_read;
    }

    inline static 
    size_t GetAligned(size_t x, size_t align) {
        return ((x+align-1)/align) * align;
    }
};