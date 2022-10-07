#include "viterbi_decoder.h"

uint8_t* GenerateCountTable() {
    const int N = 256;
    auto count_table = new uint8_t[N];
    for (int i = 0; i < N; i++) {
        uint8_t count = 0;
        for (int j = 0; j < 8; j++) {
            count += ((static_cast<uint8_t>(i) >> j) & 0b1);
        }
        count_table[i] = count;
    }
    return count_table;
};

const uint8_t* COUNT_TABLE = GenerateCountTable(); 

Trellis::Trellis(const uint8_t* _conv_codes_octal, const int nb_codes, const int constraint_length)
: K(constraint_length), L(nb_codes), nb_states(1 << (constraint_length-1)) {
    conv_codes = new uint8_t[L];
    for (int i = 0; i < L; i++) {
        conv_codes[i] = _conv_codes_octal[i];
    }

    transition_table = new Transition[nb_states];
    transition_buffers = new uint8_t[nb_states*L*2];
    for (int i = 0; i < nb_states; i++) {
        auto& transition = transition_table[i];
        transition.SetBuffers(L, &transition_buffers[i*L*2]);
    }

    // generate the trellis
    for (int curr_state = 0; curr_state < nb_states; curr_state++) {
        auto& t = transition_table[curr_state];
        for (uint8_t x = 0; x < 2; x++) {
            auto& transition_res = t.input[x];
            const uint8_t reg = (static_cast<uint8_t>(curr_state) << 1) | x;
            const uint8_t next_state = reg & 0b00111111;
            transition_res.next_state = next_state;
            for (int i = 0; i < L; i++) {
                const uint8_t output = COUNT_TABLE[reg & conv_codes[i]] % 2;
                transition_res.output[i]  = output;
            }
        }
    }
}

Trellis::~Trellis() {
    delete [] conv_codes;
    delete [] transition_table;
    delete [] transition_buffers;
}

ViterbiDecoder::ViterbiDecoder(Trellis* _trellis, const int _traceback_length)
: trellis(_trellis), traceback_length(_traceback_length),
  nb_states(_trellis->nb_states), L(_trellis->L), K(_trellis->K)
{
    paths = new Path[traceback_length*nb_states];
    depunctured_output_buf = new uint8_t[L];
    curr_puncture_code = new uint8_t[L];

    Reset();
}

ViterbiDecoder::~ViterbiDecoder() {
    delete [] depunctured_output_buf;
    delete [] curr_puncture_code;
    delete [] paths;
}

void ViterbiDecoder::Reset() {
    const int N = traceback_length*nb_states;
    for (int i = 0; i < N; i++) {
        auto& p = paths[i];
        p.cost = UINT32_MAX/2;
        p.prev_state = 0;
        p.input = 0;
    }
    curr_path_index = 0;
    curr_path_length = 0;

    auto& path = GetPath(0,0);
    path.cost = 0;
    path.prev_state = 0;
    path.input = 0;
}

ViterbiDecoder::DecodeResult ViterbiDecoder::Decode(
    const uint8_t* encoded_bits, const int nb_encoded_bits, 
    const uint8_t* puncture_code, const int nb_puncture_bits,
    uint8_t* decoded_bits, const int nb_decoded_bits, 
    const int max_puncture_bits, const bool is_flush)
{
    DecodeResult res;
    res.nb_encoded_bits = 0;
    res.nb_puncture_bits = 0;
    res.nb_decoded_bits = 0;
    res.is_encoded_ended = false;
    res.is_puncture_ended = false;
    res.is_decoded_ended = false;

    int curr_encoded_bit = 0;
    int curr_puncture_bit = 0;
    int curr_decoded_bit = 0;

    while (true) {
        // retrieve the depunctured code of length L using the puncture pattern provided
        bool is_end = false;
        int puncture_count = 0;
        for (int i = 0; i < L; i++) {
            const uint8_t p = puncture_code[curr_puncture_bit % nb_puncture_bits];
            depunctured_output_buf[i] = p ? encoded_bits[curr_encoded_bit] : 0;
            curr_puncture_code[i] = p;
            curr_encoded_bit += p;
            puncture_count += p;
            curr_puncture_bit++;

            res.is_encoded_ended = (curr_encoded_bit > nb_encoded_bits);
            res.is_puncture_ended = (curr_puncture_bit > max_puncture_bits);
            is_end = res.is_encoded_ended || res.is_puncture_ended;
            if (is_end) {
                break;
            }
        }

        // ended because we exceeded the encoded/decode/puncture buffer limits
        if (is_end) {
            break;
        }

        res.nb_encoded_bits = curr_encoded_bit;
        res.nb_puncture_bits = curr_puncture_bit;

        const int next_path_index = (curr_path_index+1) % traceback_length;

        // if we are going to overlap ourselves, push out the predicted bit
        if (curr_path_length == traceback_length) {
            // see if we can store the decoded bit
            curr_decoded_bit++;
            if (curr_decoded_bit > nb_decoded_bits) {
                res.is_decoded_ended = true;
                break;
            }

            res.nb_decoded_bits = curr_decoded_bit;

            // traceback
            int traceback_path_index = curr_path_index;
            uint32_t lowest_cost = UINT32_MAX;
            int curr_state = 0;

            for (int state = 0; state < nb_states; state++) {
                auto& path = GetPath(traceback_path_index, state);
                if (path.cost < lowest_cost) {
                    lowest_cost = path.cost;
                    curr_state = state;
                }
            }

            // go back by (traceback_length-1) times
            for (int i = 0; i < (traceback_length-1); i++) {
                auto& path = GetPath(traceback_path_index, curr_state);
                curr_state = path.prev_state;
                traceback_path_index = (traceback_path_index-1+traceback_length) % traceback_length;
            }
            auto& path = GetPath(traceback_path_index, curr_state);
            decoded_bits[curr_decoded_bit-1] = path.input;

        } else {
            curr_path_length++;
        }

        // reset next path metrics
        for (int state = 0; state < nb_states; state++) {
            auto& path = GetPath(next_path_index, state);
            path.cost = UINT32_MAX/2;
            path.input = 0;
            path.prev_state = 0;
        }

        // update path metrics for next step
        for (int curr_state = 0; curr_state < nb_states; curr_state++) {
            auto& transition = trellis->transition_table[curr_state];
            auto& curr_path = GetPath(curr_path_index, curr_state);
            for (uint8_t input = 0; input < 2; input++) {
                auto& transition_res = transition.input[input];
                auto& next_path = GetPath(next_path_index, transition_res.next_state);
                auto& pred_output_bits = transition_res.output;

                // calculate the distance error of the transition
                uint32_t dist_error = 0;
                for (int i = 0; i < L; i++) {
                    uint8_t r = transition_res.output[i] ^ depunctured_output_buf[i];
                    // ignore punctured sections in depunctured code
                    dist_error += (r * curr_puncture_code[i]);
                }

                // if the dist error is better, we update that path
                const uint32_t new_cost = curr_path.cost + dist_error;
                if (new_cost < next_path.cost) {
                    next_path.cost = new_cost;
                    next_path.input = input;
                    next_path.prev_state = curr_state;
                }
            }
        }
        curr_path_index = next_path_index;
    }

    // attempt to flush the decoded bits out to the buffer
    if (is_flush) {
        const int nb_decoded_left = nb_decoded_bits-curr_decoded_bit;
        int nb_decoded_flush_bits = 0;
        if (nb_decoded_left >= curr_path_length) {
            res.is_decoded_ended = false;
            nb_decoded_flush_bits = curr_path_length;
        } else {
            res.is_decoded_ended = true;
            nb_decoded_flush_bits = nb_decoded_left;
        }

        int curr_state = 0;
        int traceback_path_index = curr_path_index;

        int nb_decoded_flush_index = curr_decoded_bit + nb_decoded_flush_bits;
        const int i_min = curr_path_length - nb_decoded_flush_bits;

        // as we backtrack through the entire available paths
        // push into the decode buffer at correct index if there is room
        for (int i = 0; i < curr_path_length; i++) {
            auto& path = GetPath(traceback_path_index, curr_state);
            // place decoded bits into output buffer
            if (i >= i_min) {
                nb_decoded_flush_index--;
                decoded_bits[nb_decoded_flush_index] = path.input;
            }
            curr_state = path.prev_state;
            traceback_path_index = (traceback_path_index-1+traceback_length) % traceback_length;
        }

        res.nb_decoded_bits += nb_decoded_flush_bits;
        curr_path_length -= nb_decoded_flush_bits;
    }

    return res;
}

int ViterbiDecoder::GetPathIndex(const int index, const int state)
{
    return (index*nb_states + state);
}

ViterbiDecoder::Path& ViterbiDecoder::GetPath(const int index, const int state) {
    const int i = GetPathIndex(index, state);
    return paths[i];
}

uint32_t ViterbiDecoder::GetPathError(const int state) {
    auto& path = GetPath(curr_path_index, state);
    return path.cost;
}