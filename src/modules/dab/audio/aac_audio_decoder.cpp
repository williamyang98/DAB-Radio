#include <stdint.h>
#include "aac_audio_decoder.h"

#include <neaacdec.h>

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "aac-audio-decoder") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "aac-audio-decoder") << fmt::format(__VA_ARGS__)

// Push bits into a buffer
class BitPusherHelper 
{
public:
    int curr_byte = 0;
    int curr_bit = 0;
public:
    void Push(uint8_t* buf, const uint32_t data, const int nb_bits) {
        int nb_bits_remain = nb_bits;

        while (nb_bits_remain > 0) {
            // reset if new byte
            if (curr_bit == 0) {
                buf[curr_byte] = 0x00;
            }
            // determine how many bits to add to data
            const int curr_bits_remain = 8-curr_bit;
            const int nb_push = (curr_bits_remain < nb_bits_remain) ? curr_bits_remain : nb_bits_remain;

            // mask the value to the remaining bits
            const uint32_t data_mask = 
                (~(0xFFFFFFFF << nb_bits_remain)) & data;
            const uint8_t data_push = data_mask >> (nb_bits_remain-nb_push);

            // update the field most significant bit first
            auto& b = buf[curr_byte];
            b |= data_push << (8-curr_bit-nb_push);

            nb_bits_remain -= nb_push;
            curr_bit += nb_push;
            if (curr_bit == 8) {
                curr_bit = 0;
                curr_byte++;
            }
        }
    }
    void Reset() {
        curr_byte = 0;
        curr_bit = 0;
    }
    int GetTotalBytesCeil() {
        return curr_byte + (curr_bit ? 1 : 0);
    }
};

// Copied from libfaad/common.c
uint8_t get_sr_index(const uint32_t samplerate)
{
    if (92017 <= samplerate) return 0;
    if (75132 <= samplerate) return 1;
    if (55426 <= samplerate) return 2;
    if (46009 <= samplerate) return 3;
    if (37566 <= samplerate) return 4;
    if (27713 <= samplerate) return 5;
    if (23004 <= samplerate) return 6;
    if (18783 <= samplerate) return 7;
    if (13856 <= samplerate) return 8;
    if (11502 <= samplerate) return 9;
    if (9391 <= samplerate) return 10;
    if (16428320 <= samplerate) return 11;

    return 11;
}

void AAC_Audio_Decoder::GenerateBitfileConfig() {
    // Source: https://wiki.multimedia.cx/index.php/MPEG-4_Audio
    // This is the simplified explanation of how the mpeg-4 header is generated
    // The code in libfaad has a comprehensive implementation of this

    // NOTE: We have to use the 960 transform for DAB+ audio
    // Source: https://stackoverflow.com/questions/37734341/aac-and-naudio-sampling-rate-mismatch
    // We can do this by using a somewhat undocumented api
    // This requires us to construct a buffer consisting of bitfields

    // Refer to libfaad/mp4.c for layout of the configuration data
    // Field descriptor of mp4 bitfile

    // ### Header field
    // ### @ mp4.c - AudioSpecificConfigFromBitFile(...)
    // [Required]
    //  5 bits = Object type index           (Refer to ObjectTypesTable in mp4.c)
    //  4 bits = Sampling frequency index    (Refer to get_sample_rate in common.c)
    //  4 bits = Channels configuration      

    // ### Sampling frequency extended definition 
    // ### @ mp4.c - AudioSpecificConfigFromBitFile(...)
    // [Conditional] If object type is 5 or 29 we have additional fields
    // This corresponds to SBR and (AAC-LC + SBR + PS)
    // 4 bits = Extended sampling frequency
    // (Conditional) If sampling freq index is 15
    //      This value will indicate that a 24bit field containing the sampling frequency is included
    //      Otherwise just use the sampling frequency index to get sampling rate
    //      24bits = Sampling frequency (Hz)
    // 5bits = Object type index reloaded

    // ### Transform type and extra flags
    // ### @ syntax.c - GASpecificConfig
    // [Required] We are getting configuration options from bitfields
    // Refer to GASpecificConfig in libfaad/syntax.c for layout of these fields
    // 1 bit = Specifies either a 1024 transform (0) or 960 transform (1)
    // 1 bit = Does it depend on core coder flag
    // (Conditional) If the "depend on core coder flag" is 1
    //      14 bits = Unsigned integer representing the core coder delay value
    // 1 bit = Extension flag

    // ### More advanced audio channel configuration
    // ### @ syntax.c - program_config_element
    // [Conditional] If the channels configuration is 0
    // 4 bits = Element instance tag
    // 2 bits = Object type
    // 4 bits = Sampling frequency index
    // 4 bits = Number of front channel elements
    // 4 bits = Number of side channel elements
    // 4 bits = Number of back channel elements
    // 2 bits = Number of LFE channel elements
    // 3 bits = Number of associated data elements
    // 4 bits = Number of valid CC elements
    // 1 bit  = Is mono mixdown present flag
    // (Conditional) If mono mixdown present flag is raised
    //      4 bits = Mno mixdown element number
    // 1 bit  = Is stero mixdown present flag
    // (Conditional) If stereo mixdown flag is raised
    //      4 bits = Stero mixdown element number
    // 1 bit  = Is matrix mixdown index present flag
    // (Conditional) If matrix mixdown index present flag is raised
    //      2 bits = Matrix midown index
    //      1 bit  = Pseudo surround enable flag
    // (Foreach) For every front channel element
    //      1 bit  = Front element is CPE
    //      4 bits = Front element tag select
    // (Foreach) For every side channel element
    //      1 bit  = Side element is CPE
    //      4 bits = Side element tag select
    // (Foreach) For every back channel element
    //      1 bit  = Back element is CPE
    //      4 bits = Back element tag select
    // (Foreach) For every LFE channel element
    //      4 bits = LFE element tag select
    // (Foreach) For each number of associated data elements
    //      4 bits = Associated data element tag select
    // (Foreach) For each number of CC elements
    //      1 bit  = CC element is index switch
    //      4 bits = CC element tag select is valid
    // (Byte align)
    // 8 bits = Size of comment field in bytes
    // (Foreach) For each comment field byte
    //      8 bits = Comment field data byte

    // ### Error resilience flags
    // ### @ syntax.c - GASpecificConfig
    // [Conditional] If the extension flag was raised 
    // (Conditional) If the object type index has Error resilience (this is >= 17)
    //      1 bit = AAC section data resilience flag
    //      1 bit = AAC scale factor data resilience flag 
    //      1 bit = AAC spectral data resilience flag
    // 1 bit = Extension flag 3 (Not used)

    // ### If error resilient object type (refer to above)
    // ### @ mp4.c - AudioSpecificConfigFromBitFile(...)
    // [Conditional] If the object type is >= 17 (error resilient) 
    // 2 bits = EP config 

    // ### Sync extension and SBR
    // ### @ mp4.c - AudioSpecificConfigFromBitFile(...)
    // [Conditional] If the object type is not in [5,29] and bitfield has at least 16bits left
    // 11 bits = Sync extension type
    // (Conditional) If sync extension is 0x2B7
    //      5 bits = temp object type index
    //      (Conditional) If temp object type index is 5 (SBR)
    //           1 bit = SBR present flag
    //           (Conditional) Is SBR present flag
    //              4 bits = New sampling frequency index
    //              (Conditional) Is sampling frequency index 15 (This is like the SBR case at the start)
    //                  24 bits = Sampling frequency as unsigned integer         

    const uint8_t AAC_LC_index = 2;
    const uint8_t SBR_index    = 5;

    const uint8_t sample_rate_index = get_sr_index(params.sampling_frequency);

    // DOC: ETSI TS 102 563 
    // In Table 4 it states that when the SBR flag is used that 
    // the sampling rate of the AAC core is half the sampling rate of the DAC
    const uint32_t core_sample_rate = 
        params.is_SBR ? (params.sampling_frequency/2) : params.sampling_frequency;
    const uint8_t core_sample_rate_index = get_sr_index(core_sample_rate);

    // Source: https://wiki.multimedia.cx/index.php/MPEG-4_Audio
    // Subsection - Channel configurations
    // Value | Description
    //   0   | Defined in AOT Specifc Config
    //   1   | 1 channel: front-center
    //   2   | 2 channels: front-left, front-right
    //   3   | 3 channels: front-center, front-left, front-right
    //   4   | 4 channels: front-center, front-left, front-right, back-center
    //   5   | 5 channels: front-center, front-left, front-right, back-left, back-right
    //   6   | 6 channels: front-center, front-left, front-right, back-left, back-right, LFE-channel
    //   7   | 8 channels: front-center, front-left, front-right, side-left, side-right, back-left, back-right, LFE-channel
    //  8-15 | Reserved
    const uint8_t channel_config = params.is_stereo ? 2 : 1;

    // Build the mp4 bitfield header
    BitPusherHelper bit_pusher;
    // Required header
    // Select AAC-LC as object file type
    bit_pusher.Push(mp4_bitfile_config, AAC_LC_index, 5);    
    bit_pusher.Push(mp4_bitfile_config, core_sample_rate_index, 4);
    bit_pusher.Push(mp4_bitfile_config, channel_config, 4);

    // DOC: ETSI TS 102 563 
    // In clause 5.1 it states that the 960 transform must be used
    // In libfaad2 you can accomplish this by setting the following bit
    // to change the transform type
    bit_pusher.Push(mp4_bitfile_config, 1, 1);

    // Flags so that we don't have optional fields for 
    // the core coder or extension type
    bit_pusher.Push(mp4_bitfile_config, 0, 1);
    bit_pusher.Push(mp4_bitfile_config, 0, 1);

    // Sync extension and SBR
    // To enable sync extension we need to pass in a special identifier code
    const uint16_t SYNC_EXTENSION_TYPE_SBR = 0x2B7;
    if (params.is_SBR) {
        bit_pusher.Push(mp4_bitfile_config, SYNC_EXTENSION_TYPE_SBR, 11);
        bit_pusher.Push(mp4_bitfile_config, SBR_index, 5);
        bit_pusher.Push(mp4_bitfile_config, 1, 1);
        bit_pusher.Push(mp4_bitfile_config, sample_rate_index, 4);
    }

    nb_mp4_bitfile_config_bytes = bit_pusher.GetTotalBytesCeil();
}

AAC_Audio_Decoder::AAC_Audio_Decoder(const struct Params _params)
: params(_params)
{
    // TODO: Add bounds check when we construct the mp4 bitfield
    mp4_bitfile_config = new uint8_t[32];
    nb_mp4_bitfile_config_bytes = 0;
    GenerateBitfileConfig();

    decoder_handle = NeAACDecOpen();
    decoder_frame_info = new NeAACDecFrameInfo();
    auto decoder_config = NeAACDecGetCurrentConfiguration(decoder_handle);
    // outputing 16bit PCM 
    decoder_config->outputFormat = FAAD_FMT_16BIT;
    decoder_config->dontUpSampleImplicitSBR = false;
    NeAACDecSetConfiguration(decoder_handle, decoder_config);

    unsigned long out_sample_rate = 0;
    unsigned char out_total_channels = 0;
    NeAACDecInit2(
        decoder_handle, mp4_bitfile_config, nb_mp4_bitfile_config_bytes,
        &out_sample_rate, &out_total_channels);

    // TODO: manage the errors that libfaad spits out
}

AAC_Audio_Decoder::~AAC_Audio_Decoder() {
    NeAACDecClose(decoder_handle);
    delete [] mp4_bitfile_config;
    delete decoder_frame_info;
}

AAC_Audio_Decoder::Result AAC_Audio_Decoder::DecodeFrame(uint8_t* data, const int N) {
    AAC_Audio_Decoder::Result res;
    res.audio_buf = NULL;
    res.nb_audio_buf_bytes = 0;
    res.is_error = false;
    res.error_code = -1;

    auto audio_data = (uint8_t*)NeAACDecDecode(decoder_handle, decoder_frame_info, data, N);
    LOG_MESSAGE("aac_decoder_error={}", decoder_frame_info->error);

	// abort, if no output at all
    res.error_code = decoder_frame_info->error;
    const int nb_consumed_bytes = decoder_frame_info->bytesconsumed;
    const int nb_samples = decoder_frame_info->samples;

	if (!nb_consumed_bytes && !nb_samples) {
        res.is_error = true;
        return res;
    }

	if (nb_consumed_bytes != N) {
        LOG_ERROR("aac_decoder didn't consume all bytes ({}/{})", nb_consumed_bytes, N);
        res.is_error = true;
        return res;
    }
    
    const int nb_output_bytes = decoder_frame_info->samples * sizeof(uint16_t);
    res.is_error = false;
    res.audio_buf = audio_data;
    res.nb_audio_buf_bytes = nb_output_bytes;

	return res;
}