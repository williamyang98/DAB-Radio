#pragma once

#include "utility/span.h"
#include "./ofdm_params.h"
#include <algorithm>
#include <assert.h>

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
    tcb::span<uint8_t>& buf;
    const OFDM_Params params;
    const size_t align_size;
    // calculated alignment sizes
    const size_t prefix_size;
    const size_t data_symbol_size;
    const size_t null_symbol_size;
    const size_t aligned_data_prefix_padding;
    const size_t aligned_data_symbol_stride;
    const size_t total_aligned_bytes;
    // keep track of which symbols are filled
    size_t curr_symbol_index;
    size_t curr_symbol_samples; 
public:
    // NOTE: We take in a reference to a span since the buffer is allocated separately 
    //       The underlying buffer should be resized to GetTotalBufferBytes()
    OFDM_Frame_Buffer(const OFDM_Params& _params, tcb::span<uint8_t>& _buf, const size_t _align_size)
    :   buf(_buf), 
        params(_params), 
        align_size(_align_size),
        prefix_size(sizeof(T) * _params.nb_cyclic_prefix),
        data_symbol_size(sizeof(T) * _params.nb_symbol_period),
        null_symbol_size(sizeof(T) * _params.nb_null_period),
        // Add left padding before prefix so start of FFT data is aligned
        aligned_data_prefix_padding(GetAligned(prefix_size, align_size)-prefix_size),
        // Add right padding after FFT data so consecutive symbols are aligned
        aligned_data_symbol_stride(GetAligned(aligned_data_prefix_padding + data_symbol_size, align_size)),
        // Size of padded and aligned byte buffer
        total_aligned_bytes(aligned_data_symbol_stride*(params.nb_frame_symbols+1) + (null_symbol_size-data_symbol_size))
    {
        Reset();
    }

    size_t GetTotalBufferBytes() const { 
        return total_aligned_bytes; 
    }

    size_t GetAlignment() const { 
        return align_size; 
    }

    void Reset() {
        curr_symbol_index = 0;
        curr_symbol_samples = 0;
    }

    bool IsFull() const { 
        return curr_symbol_index == (params.nb_frame_symbols+1);
    }

    size_t ConsumeBuffer(tcb::span<const T> src) {
        assert(!buf.empty());
        assert(buf.size() == GetTotalBufferBytes());
        size_t nb_read = 0;
        while (!src.empty() && !IsFull()) {
            const size_t N = Consume(src);
            nb_read += N;
            src = src.subspan(N);
        }
        return nb_read;
    }

    tcb::span<T> GetDataSymbol(const size_t index) {
        const size_t offset = index*aligned_data_symbol_stride + aligned_data_prefix_padding;
        auto* wr_buf = reinterpret_cast<T*>(&buf[offset]);
        return { wr_buf, params.nb_symbol_period };
    }

    tcb::span<T> GetNullSymbol() {
        const size_t offset = 
            params.nb_frame_symbols*aligned_data_symbol_stride + 
            aligned_data_prefix_padding;
        auto* wr_buf = reinterpret_cast<T*>(&buf[offset]);
        return { wr_buf, params.nb_null_period };
    };
private:
    inline 
    size_t Consume(tcb::span<const T> src) {
        auto sym_buf = 
            (curr_symbol_index < params.nb_frame_symbols) ? 
            GetDataSymbol(curr_symbol_index) : GetNullSymbol();

        const size_t nb_capacity = sym_buf.size();
        const size_t nb_required = nb_capacity-curr_symbol_samples;
        const size_t nb_read = (src.size() > nb_required) ? nb_required : src.size();

        auto wr_buf = sym_buf.subspan(curr_symbol_samples, nb_read);
        std::copy_n(src.begin(), nb_read, wr_buf.begin());

        curr_symbol_samples += nb_read;
        // branchless math to update number of samples in OFDM frame 
        curr_symbol_index += (curr_symbol_samples / nb_capacity);
        curr_symbol_samples = (curr_symbol_samples % nb_capacity);
        return nb_read;
    }

    inline static 
    size_t GetAligned(size_t x, size_t align) {
        return ((x+align-1)/align) * align;
    }
};