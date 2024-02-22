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

-- Documentation

This library provides several interfaces to load, demux and decode MPEG video
and audio data. A high-level API combines the demuxer, video & audio decoders
in an easy to use wrapper.

Lower-level APIs for accessing the demuxer, video decoder and audio decoder, 
as well as providing different data sources are also available.

Interfaces are written in an object oriented style, meaning you create object 
instances via various different constructor functions (plm_*create()),
do some work on them and later dispose them via plm_*destroy().

plm_buffer_* .. the data source used by all interfaces
plm_audio_* ... the MPEG1 Audio Layer II ("mp2") decoder

Audio data is decoded into a struct with either one single float array with the
samples for the left and right channel interleaved, or if the 
PLM_AUDIO_SEPARATE_CHANNELS is defined *before* including this library, into
two separate float arrays - one for each channel.

When using your own plm_buffer_t instance, you can fill this buffer using 
plm_buffer_write(). You can either monitor plm_buffer_get_remaining() and push 
data when appropriate, or install a callback on the buffer with 
plm_buffer_set_load_callback() that gets called whenever the buffer needs more 
data.

A buffer created with plm_buffer_create_with_capacity() is treated as a ring
buffer, meaning that data that has already been read, will be discarded. In
contrast, a buffer created with plm_buffer_create_for_appending() will keep all
data written to it in memory. This enables seeking in the already loaded data.

This library uses malloc(), realloc() and free() to manage memory. Typically 
all allocation happens up-front when creating the interface. However, the
default buffer size may be too small for certain inputs. In these cases plmpeg
will realloc() the buffer with a larger size whenever needed.

You can also define PLM_MALLOC, PLM_REALLOC and PLM_FREE to provide your own
memory management functions.

See below for detailed the API documentation.

*/

#pragma once

#include <stdint.h>
#include <stdio.h>

// -----------------------------------------------------------------------------
// Public Data Types
typedef struct plm_buffer_t plm_buffer_t;
typedef struct plm_audio_t plm_audio_t;

#define PLM_AUDIO_SAMPLES_PER_FRAME 1152

typedef struct {
    double time;
    unsigned int count;
    #ifdef PLM_AUDIO_SEPARATE_CHANNELS
        float left[PLM_AUDIO_SAMPLES_PER_FRAME];
        float right[PLM_AUDIO_SAMPLES_PER_FRAME];
    #else
        float interleaved[PLM_AUDIO_SAMPLES_PER_FRAME * 2];
    #endif
} plm_samples_t;


// -----------------------------------------------------------------------------
// plm_buffer public API
// Provides the data source for all other plm_* interfaces

// Create an empty buffer with an initial capacity. The buffer will grow
// as needed. Data that has already been read, will be discarded.
plm_buffer_t *plm_buffer_create_with_capacity(size_t capacity);

// Destroy a buffer instance and free all data
void plm_buffer_destroy(plm_buffer_t *self);

// Copy data into the buffer. If the data to be written is larger than the 
// available space, the buffer will realloc() with a larger capacity. 
// Returns the number of bytes written. This will always be the same as the
// passed in length, except when the buffer was created _with_memory() for
// which _write() is forbidden.
size_t plm_buffer_write(plm_buffer_t *self, const uint8_t *bytes, size_t length);

// Rewind the buffer back to the beginning. When loading from a file handle,
// this also seeks to the beginning of the file.
void plm_buffer_rewind(plm_buffer_t *self);

// Get the total size. For files, this returns the file size. For all other 
// types it returns the number of bytes currently in the buffer.
size_t plm_buffer_get_size(const plm_buffer_t *self);

// Get the number of remaining (yet unread) bytes in the buffer. This can be
// useful to throttle writing.
size_t plm_buffer_get_remaining(const plm_buffer_t *self);

// Get the read head of the buffer in bytes
size_t plm_buffer_get_read_head_bytes(const plm_buffer_t *self);

// -----------------------------------------------------------------------------
// plm_audio public API
// Decode MPEG-1 Audio Layer II ("mp2") data into raw samples

// Create an audio decoder with a plm_buffer as source.
plm_audio_t *plm_audio_create_with_buffer(plm_buffer_t *buffer);

// Destroy an audio decoder and free all data.
void plm_audio_destroy(plm_audio_t *self);

// Get whether a frame header was found and we can accurately report on metadata
bool plm_audio_has_header(const plm_audio_t *self);

// Get the samplerate in samples per second.
int plm_audio_get_samplerate(const plm_audio_t *self);

// Get the bitrate in kb/s
int plm_audio_get_bitrate(const plm_audio_t *self);

// Get number of channels
int plm_audio_get_channels(const plm_audio_t *self);

// Get the current internal time in seconds.
double plm_audio_get_time(const plm_audio_t *self);

// Set the current internal time in seconds. This is only useful when you
// manipulate the underlying video buffer and want to enforce a correct
// timestamps.
void plm_audio_set_time(plm_audio_t *self, double time);

// Rewind the internal buffer. See plm_buffer_rewind().
void plm_audio_rewind(plm_audio_t *self);

// Decode header and return number of bytes for entire data frame
int plm_audio_decode_header(plm_audio_t *self);

// Decode and return one "frame" of audio and advance the internal time by 
// (PLM_AUDIO_SAMPLES_PER_FRAME/samplerate) seconds. The returned samples_t 
// is valid until the next call of plm_audio_decode() or until the audio
// decoder is destroyed.
plm_samples_t *plm_audio_decode(plm_audio_t *self, int data_frame_size);
