#pragma once

#include <stdint.h>

class Trellis 
{
public:
    struct Transition 
    {
    public:
        struct {
            uint8_t* output = NULL;
            int next_state = 0;
        } input[2];
    public:
        void SetBuffers(const int L, uint8_t* buf) {
            input[0].output = &buf[0];
            input[1].output = &buf[L];
        }
    };
public:
    const int K;
    const int L;
    const int nb_states;
    Transition* transition_table;
    uint8_t* conv_codes;
    uint8_t* transition_buffers;
public:
    Trellis(const uint8_t* _conv_codes_octal, const int nb_codes, const int constraint_length);
    ~Trellis(); 
};

class ViterbiDecoder
{
public:
    struct DecodeResult {
        bool is_encoded_ended;
        bool is_puncture_ended;
        bool is_decoded_ended;
        int nb_encoded_bits;
        int nb_puncture_bits;
        int nb_decoded_bits;
    };
private:
    struct Path {
        uint32_t cost;
        int prev_state;
        uint8_t input;
    };
private:
    Trellis* trellis;
    const int traceback_length; 
    Path* paths;
    int curr_path_index;
    int curr_path_length;

    const int nb_states;
    const int L;
    const int K;

    uint8_t* depunctured_output_buf;
    uint8_t* curr_puncture_code;
public:
    ViterbiDecoder(Trellis* _trellis, const int _traceback_length);
    ~ViterbiDecoder();
    void Reset();
    DecodeResult Decode(
        const uint8_t* encoded_bits, const int nb_encoded_bits, 
        const uint8_t* puncture_code, const int nb_puncture_bits,
        uint8_t* decoded_bits, const int nb_decoded_bits,
        const int max_puncture_bits=INT32_MAX, const bool is_flush=false);
    inline int GetPathIndex(const int index, const int state);
    inline Path& GetPath(const int index, const int state);
    uint32_t GetPathError(const int state=0);
};