#include "resampled_pcm_player.h"

void Resample(tcb::span<const Frame<int16_t>> buf_in, tcb::span<Frame<int16_t>> buf_out);

Resampled_PCM_Player::Resampled_PCM_Player(
    std::shared_ptr<RingBuffer<Frame<int16_t>>> _buffer, 
    int _output_sample_rate)
: output_sample_rate(_output_sample_rate),
  input_sample_rate(_output_sample_rate),
  buffer(_buffer)
{
}


void Resampled_PCM_Player::ConsumeBuffer(tcb::span<const Frame<int16_t>> buf) {
    const bool is_resample = (input_sample_rate != output_sample_rate);
    if (!is_resample) {
        buffer->ConsumeBuffer(buf);
        return;
    }

    const int N = (int)buf.size();
    const float L = (float)(output_sample_rate) / (float)(input_sample_rate);
    const int M = (int)(L * float(N));
    resampling_buffer.resize(M);
    Resample(buf, resampling_buffer);
    buffer->ConsumeBuffer(resampling_buffer);
}

bool Resampled_PCM_Player::SetInputSampleRate(const int _input_sample_rate) {
    const bool rv = (input_sample_rate != _input_sample_rate);
    input_sample_rate = _input_sample_rate;
    return rv;
}


void Resample(tcb::span<const Frame<int16_t>> buf_in, tcb::span<Frame<int16_t>> buf_out) {
    const int N = (int)buf_out.size();
    const int M = (int)buf_in.size();

    const float step = (float)M / (float)N;
    float j = 0;
    for (int i = 0; i < N; i++) {
        const int j0 = (int)j;
        const int j1 = j0+1;
        const auto& f0 = buf_in[j0];
        const auto& f1 = (j1 < M) ? buf_in[j1] : f0;

        // Linear interpolation
        const float k = j - (float)j0;
        buf_out[i] = f0*(1.0f-k) + f1*k;
        j += step;
    }
}
