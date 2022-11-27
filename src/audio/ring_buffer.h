#pragma once

#include <stdint.h>
#include <mutex>
#include <condition_variable>
#include <vector>
#include "utility/span.h"

template <typename T>
class RingBuffer
{
public:
    struct scoped_buffer_t {
        std::unique_lock<std::mutex> lock;
        tcb::span<const T> buf;
        scoped_buffer_t(std::mutex& _mutex, tcb::span<const T> _buf) 
        : lock(_mutex), buf(_buf) {}
    };
private:
    std::vector<T> blocks_buf;
    // fill write block
    int curr_wr_block_index;

    int curr_rd_block;
    int curr_wr_block;
    int nb_blocks;
    int block_size;
    int nb_max_blocks;

    std::mutex mutex_rw;
    std::condition_variable cv_nb_total_blocks;
public:
    RingBuffer(const int _block_size, const int _nb_max_blocks) {
        block_size = _block_size;
        nb_max_blocks = _nb_max_blocks;

        curr_wr_block_index = 0;
        curr_rd_block = 0;
        curr_wr_block = 0;
        nb_blocks = 0;

        blocks_buf.resize(block_size*nb_max_blocks);
    }

    void SetMaxBlocks(const int _nb_max_blocks) {
        auto lock = std::scoped_lock(mutex_rw);
        if (nb_max_blocks == _nb_max_blocks) {
            return;
        }
        nb_max_blocks = _nb_max_blocks;

        blocks_buf.resize(block_size*nb_max_blocks);
        curr_wr_block_index = 0;
        curr_rd_block = 0;
        curr_wr_block = 0;
        nb_blocks = 0;
    }

    void SetBlockSize(const int _block_size) {
        auto lock = std::scoped_lock(mutex_rw);
        if (block_size == _block_size) {
            return;
        }
        block_size = _block_size;

        blocks_buf.resize(block_size*nb_max_blocks);
        curr_wr_block_index = 0;
        curr_rd_block = 0;
        curr_wr_block = 0;
        nb_blocks = 0;

    }

    int GetTotalBlocks() const { return nb_blocks; }

    void ConsumeBuffer(tcb::span<const T> buf, const bool is_blocking=true) {
        auto lock = std::unique_lock(mutex_rw);
        const int N = (int)buf.size();

        int curr_byte = 0;
        while (curr_byte < N) {
            if (is_blocking && (nb_blocks >= nb_max_blocks)) {
                const auto max_delay = std::chrono::duration<float>(1.0f);
                cv_nb_total_blocks.wait_for(lock, max_delay, [this]() {
                    return (nb_blocks < nb_max_blocks);
                });
            }

            const int nb_remain = (N-curr_byte);

            tcb::span<const T> rd_buf = { 
                &buf[curr_byte], 
                (size_t)nb_remain 
            };        

            const int nb_required = block_size-curr_wr_block_index;
            tcb::span<T> wr_buf = { 
                &blocks_buf[curr_wr_block*block_size + curr_wr_block_index], 
                (size_t)nb_required
            };

            const int nb_copy = (nb_required > nb_remain) ? nb_remain : nb_required;
            std::copy_n(rd_buf.begin(), nb_copy, wr_buf.begin());
            curr_wr_block_index += nb_copy;
            curr_byte += nb_copy;

            if (curr_wr_block_index >= block_size) {
                curr_wr_block_index = 0;
                curr_wr_block = (curr_wr_block+1)%nb_max_blocks;
                nb_blocks++;
                if (nb_blocks > nb_max_blocks) {
                    nb_blocks = nb_max_blocks;
                }
            }
        }
    }

    scoped_buffer_t PopBlock() {
        auto scoped_buf = scoped_buffer_t(mutex_rw, {});
        if (nb_blocks == 0) {
            return scoped_buf;
        }

        tcb::span<const T> rd_buf = {  
            &blocks_buf[curr_rd_block*block_size],
            (size_t)block_size
        };
        curr_rd_block = (curr_rd_block+1)%nb_max_blocks;
        nb_blocks--;
        cv_nb_total_blocks.notify_one();

        scoped_buf.buf = rd_buf; 
        return scoped_buf;
    }

    void Reset() {
        auto lock = std::scoped_lock(mutex_rw);
        curr_wr_block_index = 0;
        curr_rd_block = 0;
        curr_wr_block = 0;
        nb_blocks = 0;
        cv_nb_total_blocks.notify_one();
    }

    size_t GetTotalBlockBytes() {
        return block_size * sizeof(T);
    }
};