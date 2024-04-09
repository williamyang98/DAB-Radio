/*
PL_MPEG - MPEG1 Video decoder, MP2 Audio decoder, MPEG-PS demuxer

Dominic Szablewski - https://phoboslab.org


-- LICENSE: The MIT License(MIT)

Copyright(c) 2019 Dominic Szablewski

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "./mp2_audio_decoder.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef PLM_MALLOC
    #define PLM_MALLOC(sz) malloc(sz)
    #define PLM_FREE(p) free(p)
    #define PLM_REALLOC(p, sz) realloc(p, sz)
#endif

#define PLM_UNUSED(expr) (void)(expr)

// -----------------------------------------------------------------------------
struct plm_buffer_t {
    size_t bit_index;
    size_t capacity;
    size_t length;
    size_t total_size;
    uint8_t *bytes;
};

static void plm_buffer_discard_read_bytes(plm_buffer_t *self);
static bool plm_buffer_has_bits(const plm_buffer_t *self, size_t count);
static int plm_buffer_read(plm_buffer_t *self, int count);
static void plm_buffer_align(plm_buffer_t *self);
static void plm_buffer_skip(plm_buffer_t *self, size_t count);
static int plm_buffer_skip_bytes(plm_buffer_t *self, uint8_t v);

plm_buffer_t *plm_buffer_create_with_capacity(size_t capacity) {
    assert(capacity != 0);
    plm_buffer_t *self = reinterpret_cast<plm_buffer_t *>(PLM_MALLOC(sizeof(plm_buffer_t)));
    memset(self, 0, sizeof(plm_buffer_t));
    self->capacity = capacity;
    self->bytes = reinterpret_cast<uint8_t *>(PLM_MALLOC(capacity));
    return self;
}

void plm_buffer_destroy(plm_buffer_t *self) {
    PLM_FREE(self->bytes);
    PLM_FREE(self);
}

size_t plm_buffer_get_size(const plm_buffer_t *self) {
    return self->length;
}

size_t plm_buffer_get_remaining(const plm_buffer_t *self) {
    return self->length - plm_buffer_get_read_head_bytes(self);
}

size_t plm_buffer_write(plm_buffer_t *self, const uint8_t *bytes, size_t length) {
    // This should be a ring buffer, but instead it just shifts all unread 
    // data to the beginning of the buffer and appends new data at the end. 
    // Seems to be good enough.
    plm_buffer_discard_read_bytes(self);
    self->total_size = 0;

    // Do we have to resize to fit the new data?
    size_t bytes_available = self->capacity - self->length;
    if (bytes_available < length) {
        size_t new_size = self->capacity;
        do {
            new_size *= 2;
        } while (new_size - self->length < length);
        self->bytes = reinterpret_cast<uint8_t *>(PLM_REALLOC(self->bytes, new_size));
        self->capacity = new_size;
    }

    memcpy(self->bytes + self->length, bytes, length);
    self->length += length;
    return length;
}


void plm_buffer_rewind(plm_buffer_t *self) {
    self->bit_index = 0;
    self->length = 0;
    self->total_size = 0;
}

size_t plm_buffer_get_read_head_bytes(const plm_buffer_t *self) {
    return self->bit_index >> 3;
}

bool plm_buffer_has_bits(const plm_buffer_t *self, size_t count) {
    if (((self->length << 3) - self->bit_index) >= count) {
        return true;
    }
    return false;
}

void plm_buffer_discard_read_bytes(plm_buffer_t *self) {
    size_t byte_pos = plm_buffer_get_read_head_bytes(self);
    if (byte_pos == self->length) {
        self->bit_index = 0;
        self->length = 0;
    }
    else if (byte_pos > 0) {
        memmove(self->bytes, self->bytes + byte_pos, self->length - byte_pos);
        self->bit_index -= byte_pos << 3;
        self->length -= byte_pos;
    }
}

int plm_buffer_read(plm_buffer_t *self, int count) {
    if (!plm_buffer_has_bits(self, count)) {
        return 0;
    }

    int value = 0;
    while (count) {
        int current_byte = self->bytes[plm_buffer_get_read_head_bytes(self)];

        int remaining = 8 - (self->bit_index & 7); // NOLINT: Remaining bits in byte
        int read = remaining < count ? remaining : count; // Bits in self run
        int shift = remaining - read;
        int mask = (0xff >> (8 - read)); // NOLINT

        value = (value << read) | ((current_byte & (mask << shift)) >> shift);

        self->bit_index += read;
        count -= read;
    }

    return value;
}

void plm_buffer_align(plm_buffer_t *self) {
    self->bit_index = ((self->bit_index + 7) >> 3) << 3; // Align to next byte
}

void plm_buffer_skip(plm_buffer_t *self, size_t count) {
    if (plm_buffer_has_bits(self, count)) {
        self->bit_index += count;
    }
}

int plm_buffer_skip_bytes(plm_buffer_t *self, uint8_t v) {
    plm_buffer_align(self);
    int skipped = 0;
    while (plm_buffer_has_bits(self, 8) && self->bytes[plm_buffer_get_read_head_bytes(self)] == v) {
        self->bit_index += 8;
        skipped++;
    }
    return skipped;
}

int plm_buffer_next_start_code(plm_buffer_t *self) {
    plm_buffer_align(self);

    while (plm_buffer_has_bits(self, (5 << 3))) {
        size_t byte_index = (self->bit_index) >> 3;
        if (
            self->bytes[byte_index] == 0x00 &&
            self->bytes[byte_index + 1] == 0x00 &&
            self->bytes[byte_index + 2] == 0x01
        ) {
            self->bit_index = (byte_index + 4) << 3;
            return self->bytes[byte_index + 3];
        }
        self->bit_index += 8;
    }
    return -1;
}

// -----------------------------------------------------------------------------
// plm_audio implementation

// Based on kjmp2 by Martin J. Fiedler
// http://keyj.emphy.de/kjmp2/

static const int PLM_AUDIO_FRAME_SYNC = 0x7ff;

enum class PLM_AUDIO_MPEG: int {
    MPEG_2_5 = 0x0,
    MPEG_2 = 0x2,
    MPEG_1 = 0x3,
};

enum class PLM_AUDIO_LAYER: int {
    III = 0x1,
    II = 0x2,
    I = 0x3,
};

enum class PLM_AUDIO_MODE: int {
    STEREO = 0x0,
    JOINT_STEREO = 0x1,
    DUAL_CHANNEL = 0x2,
    MONO = 0x3,
};

static const unsigned short PLM_AUDIO_SAMPLE_RATE[] = {
    44100, 48000, 32000, 0, // MPEG-1
    22050, 24000, 16000, 0  // MPEG-2
};

static const short PLM_AUDIO_BIT_RATE[] = {
    32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, // MPEG-1
     8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160  // MPEG-2
};

static const int PLM_AUDIO_SCALEFACTOR_BASE[] = {
    0x02000000, 0x01965FEA, 0x01428A30
};

static const float PLM_AUDIO_SYNTHESIS_WINDOW[] = {
         0.0,     -0.5,     -0.5,     -0.5,     -0.5,     -0.5,
        -0.5,     -1.0,     -1.0,     -1.0,     -1.0,     -1.5,
        -1.5,     -2.0,     -2.0,     -2.5,     -2.5,     -3.0,
        -3.5,     -3.5,     -4.0,     -4.5,     -5.0,     -5.5,
        -6.5,     -7.0,     -8.0,     -8.5,     -9.5,    -10.5,
       -12.0,    -13.0,    -14.5,    -15.5,    -17.5,    -19.0,
       -20.5,    -22.5,    -24.5,    -26.5,    -29.0,    -31.5,
       -34.0,    -36.5,    -39.5,    -42.5,    -45.5,    -48.5,
       -52.0,    -55.5,    -58.5,    -62.5,    -66.0,    -69.5,
       -73.5,    -77.0,    -80.5,    -84.5,    -88.0,    -91.5,
       -95.0,    -98.0,   -101.0,   -104.0,    106.5,    109.0,
       111.0,    112.5,    113.5,    114.0,    114.0,    113.5,
       112.0,    110.5,    107.5,    104.0,    100.0,     94.5,
        88.5,     81.5,     73.0,     63.5,     53.0,     41.5,
        28.5,     14.5,     -1.0,    -18.0,    -36.0,    -55.5,
       -76.5,    -98.5,   -122.0,   -147.0,   -173.5,   -200.5,
      -229.5,   -259.5,   -290.5,   -322.5,   -355.5,   -389.5,
      -424.0,   -459.5,   -495.5,   -532.0,   -568.5,   -605.0,
      -641.5,   -678.0,   -714.0,   -749.0,   -783.5,   -817.0,
      -849.0,   -879.5,   -908.5,   -935.0,   -959.5,   -981.0,
     -1000.5,  -1016.0,  -1028.5,  -1037.5,  -1042.5,  -1043.5,
     -1040.0,  -1031.5,   1018.5,   1000.0,    976.0,    946.5,
       911.0,    869.5,    822.0,    767.5,    707.0,    640.0,
       565.5,    485.0,    397.0,    302.5,    201.0,     92.5,
       -22.5,   -144.0,   -272.5,   -407.0,   -547.5,   -694.0,
      -846.0,  -1003.0,  -1165.0,  -1331.5,  -1502.0,  -1675.5,
     -1852.5,  -2031.5,  -2212.5,  -2394.0,  -2576.5,  -2758.5,
     -2939.5,  -3118.5,  -3294.5,  -3467.5,  -3635.5,  -3798.5,
     -3955.0,  -4104.5,  -4245.5,  -4377.5,  -4499.0,  -4609.5,
     -4708.0,  -4792.5,  -4863.5,  -4919.0,  -4958.0,  -4979.5,
     -4983.0,  -4967.5,  -4931.5,  -4875.0,  -4796.0,  -4694.5,
     -4569.5,  -4420.0,  -4246.0,  -4046.0,  -3820.0,  -3567.0,
      3287.0,   2979.5,   2644.0,   2280.5,   1888.0,   1467.5,
      1018.5,    541.0,     35.0,   -499.0,  -1061.0,  -1650.0,
     -2266.5,  -2909.0,  -3577.0,  -4270.0,  -4987.5,  -5727.5,
     -6490.0,  -7274.0,  -8077.5,  -8899.5,  -9739.0, -10594.5,
    -11464.5, -12347.0, -13241.0, -14144.5, -15056.0, -15973.5,
    -16895.5, -17820.0, -18744.5, -19668.0, -20588.0, -21503.0,
    -22410.5, -23308.5, -24195.0, -25068.5, -25926.5, -26767.0,
    -27589.0, -28389.0, -29166.5, -29919.0, -30644.5, -31342.0,
    -32009.5, -32645.0, -33247.0, -33814.5, -34346.0, -34839.5,
    -35295.0, -35710.0, -36084.5, -36417.5, -36707.5, -36954.0,
    -37156.5, -37315.0, -37428.0, -37496.0,  37519.0,  37496.0,
     37428.0,  37315.0,  37156.5,  36954.0,  36707.5,  36417.5,
     36084.5,  35710.0,  35295.0,  34839.5,  34346.0,  33814.5,
     33247.0,  32645.0,  32009.5,  31342.0,  30644.5,  29919.0,
     29166.5,  28389.0,  27589.0,  26767.0,  25926.5,  25068.5,
     24195.0,  23308.5,  22410.5,  21503.0,  20588.0,  19668.0,
     18744.5,  17820.0,  16895.5,  15973.5,  15056.0,  14144.5,
     13241.0,  12347.0,  11464.5,  10594.5,   9739.0,   8899.5,
      8077.5,   7274.0,   6490.0,   5727.5,   4987.5,   4270.0,
      3577.0,   2909.0,   2266.5,   1650.0,   1061.0,    499.0,
       -35.0,   -541.0,  -1018.5,  -1467.5,  -1888.0,  -2280.5,
     -2644.0,  -2979.5,   3287.0,   3567.0,   3820.0,   4046.0,
      4246.0,   4420.0,   4569.5,   4694.5,   4796.0,   4875.0,
      4931.5,   4967.5,   4983.0,   4979.5,   4958.0,   4919.0,
      4863.5,   4792.5,   4708.0,   4609.5,   4499.0,   4377.5,
      4245.5,   4104.5,   3955.0,   3798.5,   3635.5,   3467.5,
      3294.5,   3118.5,   2939.5,   2758.5,   2576.5,   2394.0,
      2212.5,   2031.5,   1852.5,   1675.5,   1502.0,   1331.5,
      1165.0,   1003.0,    846.0,    694.0,    547.5,    407.0,
       272.5,    144.0,     22.5,    -92.5,   -201.0,   -302.5,
      -397.0,   -485.0,   -565.5,   -640.0,   -707.0,   -767.5,
      -822.0,   -869.5,   -911.0,   -946.5,   -976.0,  -1000.0,
      1018.5,   1031.5,   1040.0,   1043.5,   1042.5,   1037.5,
      1028.5,   1016.0,   1000.5,    981.0,    959.5,    935.0,
       908.5,    879.5,    849.0,    817.0,    783.5,    749.0,
       714.0,    678.0,    641.5,    605.0,    568.5,    532.0,
       495.5,    459.5,    424.0,    389.5,    355.5,    322.5,
       290.5,    259.5,    229.5,    200.5,    173.5,    147.0,
       122.0,     98.5,     76.5,     55.5,     36.0,     18.0,
         1.0,    -14.5,    -28.5,    -41.5,    -53.0,    -63.5,
       -73.0,    -81.5,    -88.5,    -94.5,   -100.0,   -104.0,
      -107.5,   -110.5,   -112.0,   -113.5,   -114.0,   -114.0,
      -113.5,   -112.5,   -111.0,   -109.0,    106.5,    104.0,
       101.0,     98.0,     95.0,     91.5,     88.0,     84.5,
        80.5,     77.0,     73.5,     69.5,     66.0,     62.5,
        58.5,     55.5,     52.0,     48.5,     45.5,     42.5,
        39.5,     36.5,     34.0,     31.5,     29.0,     26.5,
        24.5,     22.5,     20.5,     19.0,     17.5,     15.5,
        14.5,     13.0,     12.0,     10.5,      9.5,      8.5,
         8.0,      7.0,      6.5,      5.5,      5.0,      4.5,
         4.0,      3.5,      3.5,      3.0,      2.5,      2.5,
         2.0,      2.0,      1.5,      1.5,      1.0,      1.0,
         1.0,      1.0,      0.5,      0.5,      0.5,      0.5,
         0.5,      0.5
};

// Quantizer lookup, step 1: bitrate classes
static const uint8_t PLM_AUDIO_QUANT_LUT_STEP_1[2][16] = {
    // 32, 48, 56, 64, 80, 96,112,128,160,192,224,256,320,384 <- bitrate
    { 0,  0,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2 }, // mono
    // 16, 24, 28, 32, 40, 48, 56, 64, 80, 96,112,128,160,192 <- bitrate / chan
    { 0,  0,  0,  0,  0,  0,  1,  1,  1,  2,  2,  2,  2,  2 } // stereo
};

// Quantizer lookup, step 2: bitrate class, sample rate -> B2 table idx, sblimit
#define PLM_AUDIO_QUANT_TAB_A (27 | 64)   // Table 3-B.2a: high-rate, sblimit = 27
#define PLM_AUDIO_QUANT_TAB_B (30 | 64)   // Table 3-B.2b: high-rate, sblimit = 30
#define PLM_AUDIO_QUANT_TAB_C 8           // Table 3-B.2c:  low-rate, sblimit =  8
#define PLM_AUDIO_QUANT_TAB_D 12          // Table 3-B.2d:  low-rate, sblimit = 12

static const uint8_t QUANT_LUT_STEP_2[3][3] = {
    //44.1 kHz,              48 kHz,                32 kHz
    { PLM_AUDIO_QUANT_TAB_C, PLM_AUDIO_QUANT_TAB_C, PLM_AUDIO_QUANT_TAB_D }, // 32 - 48 kbit/sec/ch
    { PLM_AUDIO_QUANT_TAB_A, PLM_AUDIO_QUANT_TAB_A, PLM_AUDIO_QUANT_TAB_A }, // 56 - 80 kbit/sec/ch
    { PLM_AUDIO_QUANT_TAB_B, PLM_AUDIO_QUANT_TAB_A, PLM_AUDIO_QUANT_TAB_B }  // 96+     kbit/sec/ch
};

// Quantizer lookup, step 3: B2 table, subband -> nbal, row index
// (upper 4 bits: nbal, lower 4 bits: row index)
static const uint8_t PLM_AUDIO_QUANT_LUT_STEP_3[3][32] = {
    // Low-rate table (3-B.2c and 3-B.2d)
    {
        0x44,0x44,
        0x34,0x34,0x34,0x34,0x34,0x34,0x34,0x34,0x34,0x34
    },
    // High-rate table (3-B.2a and 3-B.2b)
    {
        0x43,0x43,0x43,
        0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,
        0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,
        0x20,0x20,0x20,0x20,0x20,0x20,0x20
    },
    // MPEG-2 LSR table (B.2 in ISO 13818-3)
    {
        0x45,0x45,0x45,0x45,
        0x34,0x34,0x34,0x34,0x34,0x34,0x34,
        0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,
        0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24
    }
};

// Quantizer lookup, step 4: table row, allocation[] value -> quant table index
static const uint8_t PLM_AUDIO_QUANT_LUT_STEP_4[6][16] = {
    { 0, 1, 2, 17 },
    { 0, 1, 2,  3, 4, 5, 6, 17 },
    { 0, 1, 2,  3, 4, 5, 6,  7,  8,  9, 10, 11, 12, 13, 14, 17 },
    { 0, 1, 3,  5, 6, 7, 8,  9, 10, 11, 12, 13, 14, 15, 16, 17 },
    { 0, 1, 2,  4, 5, 6, 7,  8,  9, 10, 11, 12, 13, 14, 15, 17 },
    { 0, 1, 2,  3, 4, 5, 6,  7,  8,  9, 10, 11, 12, 13, 14, 15 }
};

typedef struct plm_quantizer_spec_t {
    unsigned short levels;
    unsigned char group;
    unsigned char bits;
} plm_quantizer_spec_t;

static const plm_quantizer_spec_t PLM_AUDIO_QUANT_TAB[] = {
    {     3, 1,  5 },  //  1
    {     5, 1,  7 },  //  2
    {     7, 0,  3 },  //  3
    {     9, 1, 10 },  //  4
    {    15, 0,  4 },  //  5
    {    31, 0,  5 },  //  6
    {    63, 0,  6 },  //  7
    {   127, 0,  7 },  //  8
    {   255, 0,  8 },  //  9
    {   511, 0,  9 },  // 10
    {  1023, 0, 10 },  // 11
    {  2047, 0, 11 },  // 12
    {  4095, 0, 12 },  // 13
    {  8191, 0, 13 },  // 14
    { 16383, 0, 14 },  // 15
    { 32767, 0, 15 },  // 16
    { 65535, 0, 16 }   // 17
};

struct plm_audio_t {
    double time;
    int samples_decoded;
    int samplerate_index;
    int bitrate_index;
    PLM_AUDIO_MPEG version;
    PLM_AUDIO_LAYER layer;
    PLM_AUDIO_MODE mode;
    int bound;
    int v_pos;
    bool has_header;

    plm_buffer_t *buffer;

    const plm_quantizer_spec_t *allocation[2][32];
    uint8_t scale_factor_info[2][32];
    int scale_factor[2][32][3];
    int sample[2][32][3];

    plm_samples_t samples;
    float D[1024];
    float V[2][1024];
    float U[32];
};

static bool plm_audio_find_frame_sync(plm_audio_t *self);
static void plm_audio_decode_frame(plm_audio_t *self);
static const plm_quantizer_spec_t *plm_audio_read_allocation(plm_audio_t *self, int sb, int tab3);
static void plm_audio_read_samples(plm_audio_t *self, int ch, int sb, int part); 
static void plm_audio_idct36(int s[32][3], int ss, float *d, int dp);

plm_audio_t *plm_audio_create_with_buffer(plm_buffer_t *buffer) {
    plm_audio_t *self = reinterpret_cast<plm_audio_t *>(PLM_MALLOC(sizeof(plm_audio_t)));
    memset(self, 0, sizeof(plm_audio_t));

    self->samples.count = PLM_AUDIO_SAMPLES_PER_FRAME;
    self->buffer = buffer;
    self->samplerate_index = 3; // Indicates 0
    self->has_header = false;

    memcpy(self->D, PLM_AUDIO_SYNTHESIS_WINDOW, 512 * sizeof(float));
    memcpy(self->D + 512, PLM_AUDIO_SYNTHESIS_WINDOW, 512 * sizeof(float));

    return self;
}

void plm_audio_destroy(plm_audio_t *self) {
    PLM_FREE(self);
}

bool plm_audio_has_header(const plm_audio_t *self) {
    return self->has_header;
}

int plm_audio_get_samplerate(const plm_audio_t *self) {
    return plm_audio_has_header(self) ? PLM_AUDIO_SAMPLE_RATE[self->samplerate_index] : 0;
}

int plm_audio_get_bitrate(const plm_audio_t *self) {
    return plm_audio_has_header(self) ? PLM_AUDIO_BIT_RATE[self->bitrate_index] : 0;
}

int plm_audio_get_channels(const plm_audio_t *self) {
    if (!plm_audio_has_header(self)) return 0; 
    switch (self->mode) {
    case PLM_AUDIO_MODE::MONO: return 1;
    case PLM_AUDIO_MODE::STEREO: return 2;
    case PLM_AUDIO_MODE::JOINT_STEREO: return 2;
    case PLM_AUDIO_MODE::DUAL_CHANNEL: return 2;
    default: return 0;
    }
}

double plm_audio_get_time(const plm_audio_t *self) {
    return self->time;
}

void plm_audio_set_time(plm_audio_t *self, double time) {
    self->samples_decoded = int(time * double(PLM_AUDIO_SAMPLE_RATE[self->samplerate_index]));
    self->time = time;
}

void plm_audio_rewind(plm_audio_t *self) {
    plm_buffer_rewind(self->buffer);
    self->time = 0;
    self->samples_decoded = 0;
}

plm_samples_t *plm_audio_decode(plm_audio_t *self, int data_frame_size) {
    if (data_frame_size == 0) {
        return nullptr;
    }
    if (!plm_buffer_has_bits(self->buffer, data_frame_size << 3)) {
        return nullptr;
    }
 
    plm_audio_decode_frame(self);
    self->samples.time = self->time;
    self->samples_decoded += PLM_AUDIO_SAMPLES_PER_FRAME;
    self->time = (double)self->samples_decoded / (double)PLM_AUDIO_SAMPLE_RATE[self->samplerate_index];
    return &self->samples;
}

bool plm_audio_find_frame_sync(plm_audio_t *self) {
    size_t i;
    for (i = plm_buffer_get_read_head_bytes(self->buffer); i < self->buffer->length-1; i++) {
        if (
            self->buffer->bytes[i] == 0xFF &&
            (self->buffer->bytes[i+1] & 0xFE) == 0xFC
        ) {
            self->buffer->bit_index = ((i+1) << 3) + 3;
            return true;
        }
    }
    self->buffer->bit_index = (i + 1) << 3;
    return false;
}

int plm_audio_decode_header(plm_audio_t *self) {
    if (!plm_buffer_has_bits(self->buffer, 48)) {
        return 0;
    }

    plm_buffer_skip_bytes(self->buffer, 0x00);
    int sync = plm_buffer_read(self->buffer, 11);


    // Attempt to resync if no syncword was found. This sucks balls. The MP2 
    // stream contains a syncword just before every frame (11 bits set to 1).
    // However, this syncword is not guaranteed to not occur elsewhere in the
    // stream. So, if we have to resync, we also have to check if the header 
    // (samplerate, bitrate) differs from the one we had before. This all
    // may still lead to garbage data being decoded :/
    // NOTE: In callee we can guarantee that frames are always synced
    // if (sync != PLM_AUDIO_FRAME_SYNC && !plm_audio_find_frame_sync(self)) {
    //     return 0;
    // }
    if (sync != PLM_AUDIO_FRAME_SYNC) {
        return 0;
    }

    self->version = static_cast<PLM_AUDIO_MPEG>(plm_buffer_read(self->buffer, 2));
    self->layer = static_cast<PLM_AUDIO_LAYER>(plm_buffer_read(self->buffer, 2));
    int hasCRC = !plm_buffer_read(self->buffer, 1);

    if (self->version != PLM_AUDIO_MPEG::MPEG_1 || self->layer != PLM_AUDIO_LAYER::II) {
        return 0;
    }

    int bitrate_index = plm_buffer_read(self->buffer, 4) - 1;
    if (bitrate_index > 13) {
        return 0;
    }

    int samplerate_index = plm_buffer_read(self->buffer, 2);
    if (samplerate_index == 3) {
        return 0;
    }

    int padding = plm_buffer_read(self->buffer, 1);
    plm_buffer_skip(self->buffer, 1); // f_private
    PLM_AUDIO_MODE mode = static_cast<PLM_AUDIO_MODE>(plm_buffer_read(self->buffer, 2));

    // If we already have a header, make sure the samplerate, bitrate and mode
    // are still the same, otherwise we might have missed sync.
    if (
        self->has_header && (
            self->bitrate_index != bitrate_index ||
            self->samplerate_index != samplerate_index ||
            self->mode != mode
        )
    ) {
        return 0;
    }

    self->bitrate_index = bitrate_index;
    self->samplerate_index = samplerate_index;
    self->mode = mode;
    self->has_header = true;

    // Parse the mode_extension, set up the stereo bound
    if (mode == PLM_AUDIO_MODE::JOINT_STEREO) {
        self->bound = (plm_buffer_read(self->buffer, 2) + 1) << 2;
    }
    else {
        plm_buffer_skip(self->buffer, 2);
        self->bound = (mode == PLM_AUDIO_MODE::MONO) ? 0 : 32;
    }

    // Discard the last 4 bits of the header and the CRC value, if present
    plm_buffer_skip(self->buffer, 4); // copyright(1), original(1), emphasis(2)
    if (hasCRC) {
        plm_buffer_skip(self->buffer, 16);
    }

    // Compute frame size, check if we have enough data to decode the whole frame.
    int bitrate = PLM_AUDIO_BIT_RATE[self->bitrate_index]; // NOLINT
    int samplerate = PLM_AUDIO_SAMPLE_RATE[self->samplerate_index];
    int frame_size = (144000 * bitrate / samplerate) + padding;
    return frame_size - (hasCRC ? 6 : 4);
}

void plm_audio_decode_frame(plm_audio_t *self) {
    // Prepare the quantizer table lookups
    int tab3 = 0;
    int sblimit = 0;
    
    int tab1 = (self->mode == PLM_AUDIO_MODE::MONO) ? 0 : 1;
    int tab2 = PLM_AUDIO_QUANT_LUT_STEP_1[tab1][self->bitrate_index];
    tab3 = QUANT_LUT_STEP_2[tab2][self->samplerate_index];
    sblimit = tab3 & 63;
    tab3 >>= 6;

    if (self->bound > sblimit) {
        self->bound = sblimit;
    }

    // Read the allocation information
    for (int sb = 0; sb < self->bound; sb++) {
        self->allocation[0][sb] = plm_audio_read_allocation(self, sb, tab3);
        self->allocation[1][sb] = plm_audio_read_allocation(self, sb, tab3);
    }

    for (int sb = self->bound; sb < sblimit; sb++) {
        self->allocation[0][sb] =
            self->allocation[1][sb] =
            plm_audio_read_allocation(self, sb, tab3);
    }

    // Read scale factor selector information
    int channels = (self->mode == PLM_AUDIO_MODE::MONO) ? 1 : 2;
    for (int sb = 0; sb < sblimit; sb++) {
        for (int ch = 0; ch < channels; ch++) {
            if (self->allocation[ch][sb]) {
                self->scale_factor_info[ch][sb] = plm_buffer_read(self->buffer, 2);
            }
        }
        if (self->mode == PLM_AUDIO_MODE::MONO) {
            self->scale_factor_info[1][sb] = self->scale_factor_info[0][sb];
        }
    }

    // Read scale factors
    for (int sb = 0; sb < sblimit; sb++) {
        for (int ch = 0; ch < channels; ch++) {
            if (self->allocation[ch][sb]) {
                int *sf = self->scale_factor[ch][sb];
                switch (self->scale_factor_info[ch][sb]) {
                    case 0:
                        sf[0] = plm_buffer_read(self->buffer, 6);
                        sf[1] = plm_buffer_read(self->buffer, 6);
                        sf[2] = plm_buffer_read(self->buffer, 6);
                        break;
                    case 1:
                        sf[0] = 
                        sf[1] = plm_buffer_read(self->buffer, 6);
                        sf[2] = plm_buffer_read(self->buffer, 6);
                        break;
                    case 2:
                        sf[0] = 
                        sf[1] = 
                        sf[2] = plm_buffer_read(self->buffer, 6);
                        break;
                    case 3:
                        sf[0] = plm_buffer_read(self->buffer, 6);
                        sf[1] = 
                        sf[2] = plm_buffer_read(self->buffer, 6);
                        break;
                }
            }
        }
        if (self->mode == PLM_AUDIO_MODE::MONO) {
            self->scale_factor[1][sb][0] = self->scale_factor[0][sb][0];
            self->scale_factor[1][sb][1] = self->scale_factor[0][sb][1];
            self->scale_factor[1][sb][2] = self->scale_factor[0][sb][2];
        }
    }

    // Coefficient input and reconstruction
    int out_pos = 0;
    for (int part = 0; part < 3; part++) {
        for (int granule = 0; granule < 4; granule++) {

            // Read the samples
            for (int sb = 0; sb < self->bound; sb++) {
                plm_audio_read_samples(self, 0, sb, part);
                plm_audio_read_samples(self, 1, sb, part);
            }
            for (int sb = self->bound; sb < sblimit; sb++) {
                plm_audio_read_samples(self, 0, sb, part);
                self->sample[1][sb][0] = self->sample[0][sb][0];
                self->sample[1][sb][1] = self->sample[0][sb][1];
                self->sample[1][sb][2] = self->sample[0][sb][2];
            }
            for (int sb = sblimit; sb < 32; sb++) {
                self->sample[0][sb][0] = 0;
                self->sample[0][sb][1] = 0;
                self->sample[0][sb][2] = 0;
                self->sample[1][sb][0] = 0;
                self->sample[1][sb][1] = 0;
                self->sample[1][sb][2] = 0;
            }

            // Synthesis loop
            for (int p = 0; p < 3; p++) {
                // Shifting step
                self->v_pos = (self->v_pos - 64) & 1023;

                for (int ch = 0; ch < 2; ch++) {
                    plm_audio_idct36(self->sample[ch], p, self->V[ch], self->v_pos);

                    // Build U, windowing, calculate output
                    memset(self->U, 0, sizeof(self->U));

                    int d_index = 512 - (self->v_pos >> 1);
                    int v_index = (self->v_pos % 128) >> 1;
                    while (v_index < 1024) {
                        for (int i = 0; i < 32; ++i) {
                            self->U[i] += self->D[d_index++] * self->V[ch][v_index++];
                        }

                        v_index += 128 - 32;
                        d_index += 64 - 32;
                    }

                    d_index -= (512 - 32);
                    v_index = (128 - 32 + 1024) - v_index;
                    while (v_index < 1024) {
                        for (int i = 0; i < 32; ++i) {
                            self->U[i] += self->D[d_index++] * self->V[ch][v_index++];
                        }

                        v_index += 128 - 32;
                        d_index += 64 - 32;
                    }

                    // Output samples
                    #ifdef PLM_AUDIO_SEPARATE_CHANNELS
                        float *out_channel = ch == 0
                            ? self->samples.left
                            : self->samples.right;
                        for (int j = 0; j < 32; j++) {
                            out_channel[out_pos + j] = self->U[j] / 2147418112.0f;
                        }
                    #else
                        for (int j = 0; j < 32; j++) {
                            self->samples.interleaved[((out_pos + j) << 1) + ch] = 
                                self->U[j] / 2147418112.0f;
                        }
                    #endif
                } // End of synthesis channel loop
                out_pos += 32;
            } // End of synthesis sub-block loop

        } // Decoding of the granule finished
    }

    plm_buffer_align(self->buffer);
}

const plm_quantizer_spec_t *plm_audio_read_allocation(plm_audio_t *self, int sb, int tab3) {
    int tab4 = PLM_AUDIO_QUANT_LUT_STEP_3[tab3][sb];
    int qtab = PLM_AUDIO_QUANT_LUT_STEP_4[tab4 & 15][plm_buffer_read(self->buffer, tab4 >> 4)];
    return qtab ? (&PLM_AUDIO_QUANT_TAB[qtab - 1]) : 0;
}

void plm_audio_read_samples(plm_audio_t *self, int ch, int sb, int part) {
    const plm_quantizer_spec_t *q = self->allocation[ch][sb];
    int sf = self->scale_factor[ch][sb][part];
    int *sample = self->sample[ch][sb];
    int val = 0;

    if (!q) {
        // No bits allocated for this subband
        sample[0] = sample[1] = sample[2] = 0;
        return;
    }

    // Resolve scalefactor
    if (sf == 63) {
        sf = 0;
    }
    else {
        int shift = (sf / 3) | 0;
        sf = (PLM_AUDIO_SCALEFACTOR_BASE[sf % 3] + ((1 << shift) >> 1)) >> shift;
    }

    // Decode samples
    int adj = q->levels;
    if (q->group) {
        // Decode grouped samples
        val = plm_buffer_read(self->buffer, q->bits);
        sample[0] = val % adj;
        val /= adj;
        sample[1] = val % adj;
        sample[2] = val / adj;
    }
    else {
        // Decode direct samples
        sample[0] = plm_buffer_read(self->buffer, q->bits);
        sample[1] = plm_buffer_read(self->buffer, q->bits);
        sample[2] = plm_buffer_read(self->buffer, q->bits);
    }

    // Postmultiply samples
    int scale = 65536 / (adj + 1);
    adj = ((adj + 1) >> 1) - 1;

    val = (adj - sample[0]) * scale;
    sample[0] = (val * (sf >> 12) + ((val * (sf & 4095) + 2048) >> 12)) >> 12;

    val = (adj - sample[1]) * scale;
    sample[1] = (val * (sf >> 12) + ((val * (sf & 4095) + 2048) >> 12)) >> 12;

    val = (adj - sample[2]) * scale;
    sample[2] = (val * (sf >> 12) + ((val * (sf & 4095) + 2048) >> 12)) >> 12;
}

void plm_audio_idct36(int s[32][3], int ss, float *d, int dp) {
    float t01, t02, t03, t04, t05, t06, t07, t08, t09, t10, t11, t12,
        t13, t14, t15, t16, t17, t18, t19, t20, t21, t22, t23, t24,
        t25, t26, t27, t28, t29, t30, t31, t32, t33;

    t01 = (float)(s[0][ss] + s[31][ss]); t02 = (float)(s[0][ss] - s[31][ss]) * 0.500602998235f;
    t03 = (float)(s[1][ss] + s[30][ss]); t04 = (float)(s[1][ss] - s[30][ss]) * 0.505470959898f;
    t05 = (float)(s[2][ss] + s[29][ss]); t06 = (float)(s[2][ss] - s[29][ss]) * 0.515447309923f;
    t07 = (float)(s[3][ss] + s[28][ss]); t08 = (float)(s[3][ss] - s[28][ss]) * 0.53104259109f;
    t09 = (float)(s[4][ss] + s[27][ss]); t10 = (float)(s[4][ss] - s[27][ss]) * 0.553103896034f;
    t11 = (float)(s[5][ss] + s[26][ss]); t12 = (float)(s[5][ss] - s[26][ss]) * 0.582934968206f;
    t13 = (float)(s[6][ss] + s[25][ss]); t14 = (float)(s[6][ss] - s[25][ss]) * 0.622504123036f;
    t15 = (float)(s[7][ss] + s[24][ss]); t16 = (float)(s[7][ss] - s[24][ss]) * 0.674808341455f;
    t17 = (float)(s[8][ss] + s[23][ss]); t18 = (float)(s[8][ss] - s[23][ss]) * 0.744536271002f;
    t19 = (float)(s[9][ss] + s[22][ss]); t20 = (float)(s[9][ss] - s[22][ss]) * 0.839349645416f;
    t21 = (float)(s[10][ss] + s[21][ss]); t22 = (float)(s[10][ss] - s[21][ss]) * 0.972568237862f;
    t23 = (float)(s[11][ss] + s[20][ss]); t24 = (float)(s[11][ss] - s[20][ss]) * 1.16943993343f;
    t25 = (float)(s[12][ss] + s[19][ss]); t26 = (float)(s[12][ss] - s[19][ss]) * 1.48416461631f;
    t27 = (float)(s[13][ss] + s[18][ss]); t28 = (float)(s[13][ss] - s[18][ss]) * 2.05778100995f;
    t29 = (float)(s[14][ss] + s[17][ss]); t30 = (float)(s[14][ss] - s[17][ss]) * 3.40760841847f;
    t31 = (float)(s[15][ss] + s[16][ss]); t32 = (float)(s[15][ss] - s[16][ss]) * 10.1900081235f;

    t33 = t01 + t31; t31 = (t01 - t31) * 0.502419286188f;
    t01 = t03 + t29; t29 = (t03 - t29) * 0.52249861494f;
    t03 = t05 + t27; t27 = (t05 - t27) * 0.566944034816f;
    t05 = t07 + t25; t25 = (t07 - t25) * 0.64682178336f;
    t07 = t09 + t23; t23 = (t09 - t23) * 0.788154623451f;
    t09 = t11 + t21; t21 = (t11 - t21) * 1.06067768599f;
    t11 = t13 + t19; t19 = (t13 - t19) * 1.72244709824f;
    t13 = t15 + t17; t17 = (t15 - t17) * 5.10114861869f;
    t15 = t33 + t13; t13 = (t33 - t13) * 0.509795579104f;
    t33 = t01 + t11; t01 = (t01 - t11) * 0.601344886935f;
    t11 = t03 + t09; t09 = (t03 - t09) * 0.899976223136f;
    t03 = t05 + t07; t07 = (t05 - t07) * 2.56291544774f;
    t05 = t15 + t03; t15 = (t15 - t03) * 0.541196100146f;
    t03 = t33 + t11; t11 = (t33 - t11) * 1.30656296488f;
    t33 = t05 + t03; t05 = (t05 - t03) * 0.707106781187f;
    t03 = t15 + t11; t15 = (t15 - t11) * 0.707106781187f;
    t03 += t15;
    t11 = t13 + t07; t13 = (t13 - t07) * 0.541196100146f;
    t07 = t01 + t09; t09 = (t01 - t09) * 1.30656296488f;
    t01 = t11 + t07; t07 = (t11 - t07) * 0.707106781187f;
    t11 = t13 + t09; t13 = (t13 - t09) * 0.707106781187f;
    t11 += t13; t01 += t11;
    t11 += t07; t07 += t13;
    t09 = t31 + t17; t31 = (t31 - t17) * 0.509795579104f;
    t17 = t29 + t19; t29 = (t29 - t19) * 0.601344886935f;
    t19 = t27 + t21; t21 = (t27 - t21) * 0.899976223136f;
    t27 = t25 + t23; t23 = (t25 - t23) * 2.56291544774f;
    t25 = t09 + t27; t09 = (t09 - t27) * 0.541196100146f;
    t27 = t17 + t19; t19 = (t17 - t19) * 1.30656296488f;
    t17 = t25 + t27; t27 = (t25 - t27) * 0.707106781187f;
    t25 = t09 + t19; t19 = (t09 - t19) * 0.707106781187f;
    t25 += t19;
    t09 = t31 + t23; t31 = (t31 - t23) * 0.541196100146f;
    t23 = t29 + t21; t21 = (t29 - t21) * 1.30656296488f;
    t29 = t09 + t23; t23 = (t09 - t23) * 0.707106781187f;
    t09 = t31 + t21; t31 = (t31 - t21) * 0.707106781187f;
    t09 += t31;    t29 += t09;    t09 += t23;    t23 += t31;
    t17 += t29;    t29 += t25;    t25 += t09;    t09 += t27;
    t27 += t23;    t23 += t19; t19 += t31;
    t21 = t02 + t32; t02 = (t02 - t32) * 0.502419286188f;
    t32 = t04 + t30; t04 = (t04 - t30) * 0.52249861494f;
    t30 = t06 + t28; t28 = (t06 - t28) * 0.566944034816f;
    t06 = t08 + t26; t08 = (t08 - t26) * 0.64682178336f;
    t26 = t10 + t24; t10 = (t10 - t24) * 0.788154623451f;
    t24 = t12 + t22; t22 = (t12 - t22) * 1.06067768599f;
    t12 = t14 + t20; t20 = (t14 - t20) * 1.72244709824f;
    t14 = t16 + t18; t16 = (t16 - t18) * 5.10114861869f;
    t18 = t21 + t14; t14 = (t21 - t14) * 0.509795579104f;
    t21 = t32 + t12; t32 = (t32 - t12) * 0.601344886935f;
    t12 = t30 + t24; t24 = (t30 - t24) * 0.899976223136f;
    t30 = t06 + t26; t26 = (t06 - t26) * 2.56291544774f;
    t06 = t18 + t30; t18 = (t18 - t30) * 0.541196100146f;
    t30 = t21 + t12; t12 = (t21 - t12) * 1.30656296488f;
    t21 = t06 + t30; t30 = (t06 - t30) * 0.707106781187f;
    t06 = t18 + t12; t12 = (t18 - t12) * 0.707106781187f;
    t06 += t12;
    t18 = t14 + t26; t26 = (t14 - t26) * 0.541196100146f;
    t14 = t32 + t24; t24 = (t32 - t24) * 1.30656296488f;
    t32 = t18 + t14; t14 = (t18 - t14) * 0.707106781187f;
    t18 = t26 + t24; t24 = (t26 - t24) * 0.707106781187f;
    t18 += t24; t32 += t18;
    t18 += t14; t26 = t14 + t24;
    t14 = t02 + t16; t02 = (t02 - t16) * 0.509795579104f;
    t16 = t04 + t20; t04 = (t04 - t20) * 0.601344886935f;
    t20 = t28 + t22; t22 = (t28 - t22) * 0.899976223136f;
    t28 = t08 + t10; t10 = (t08 - t10) * 2.56291544774f;
    t08 = t14 + t28; t14 = (t14 - t28) * 0.541196100146f;
    t28 = t16 + t20; t20 = (t16 - t20) * 1.30656296488f;
    t16 = t08 + t28; t28 = (t08 - t28) * 0.707106781187f;
    t08 = t14 + t20; t20 = (t14 - t20) * 0.707106781187f;
    t08 += t20;
    t14 = t02 + t10; t02 = (t02 - t10) * 0.541196100146f;
    t10 = t04 + t22; t22 = (t04 - t22) * 1.30656296488f;
    t04 = t14 + t10; t10 = (t14 - t10) * 0.707106781187f;
    t14 = t02 + t22; t02 = (t02 - t22) * 0.707106781187f;
    t14 += t02;    t04 += t14;    t14 += t10;    t10 += t02;
    t16 += t04;    t04 += t08;    t08 += t14;    t14 += t28;
    t28 += t10;    t10 += t20;    t20 += t02;    t21 += t16;
    t16 += t32;    t32 += t04;    t04 += t06;    t06 += t08;
    t08 += t18;    t18 += t14;    t14 += t30;    t30 += t28;
    t28 += t26;    t26 += t10;    t10 += t12;    t12 += t20;
    t20 += t24;    t24 += t02;

    d[dp + 48] = -t33;
    d[dp + 49] = d[dp + 47] = -t21;
    d[dp + 50] = d[dp + 46] = -t17;
    d[dp + 51] = d[dp + 45] = -t16;
    d[dp + 52] = d[dp + 44] = -t01;
    d[dp + 53] = d[dp + 43] = -t32;
    d[dp + 54] = d[dp + 42] = -t29;
    d[dp + 55] = d[dp + 41] = -t04;
    d[dp + 56] = d[dp + 40] = -t03;
    d[dp + 57] = d[dp + 39] = -t06;
    d[dp + 58] = d[dp + 38] = -t25;
    d[dp + 59] = d[dp + 37] = -t08;
    d[dp + 60] = d[dp + 36] = -t11;
    d[dp + 61] = d[dp + 35] = -t18;
    d[dp + 62] = d[dp + 34] = -t09;
    d[dp + 63] = d[dp + 33] = -t14;
    d[dp + 32] = -t05;
    d[dp + 0] = t05; d[dp + 31] = -t30;
    d[dp + 1] = t30; d[dp + 30] = -t27;
    d[dp + 2] = t27; d[dp + 29] = -t28;
    d[dp + 3] = t28; d[dp + 28] = -t07;
    d[dp + 4] = t07; d[dp + 27] = -t26;
    d[dp + 5] = t26; d[dp + 26] = -t23;
    d[dp + 6] = t23; d[dp + 25] = -t10;
    d[dp + 7] = t10; d[dp + 24] = -t15;
    d[dp + 8] = t15; d[dp + 23] = -t12;
    d[dp + 9] = t12; d[dp + 22] = -t19;
    d[dp + 10] = t19; d[dp + 21] = -t20;
    d[dp + 11] = t20; d[dp + 20] = -t13;
    d[dp + 12] = t13; d[dp + 19] = -t24;
    d[dp + 13] = t24; d[dp + 18] = -t31;
    d[dp + 14] = t31; d[dp + 17] = -t02;
    d[dp + 15] = t02; d[dp + 16] = 0.0;
}
