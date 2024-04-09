#include "./aac_audio_decoder.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <fmt/format.h>
#include <neaacdec.h>
#include "utility/span.h"
#include "../dab_logging.h"
#define TAG "aac-audio-decoder"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

// Push bits into a buffer
class BitPusherHelper 
{
public:
    int curr_byte = 0;
    int curr_bit = 0;
public:
    void Push(std::vector<uint8_t>& buf, const uint32_t data, const int nb_bits) {
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
            const uint32_t data_mask = (~(uint32_t(0xFFFFFFFF) << nb_bits_remain)) & data;
            const uint8_t data_push = data_mask >> (nb_bits_remain-nb_push);

            // update the field most significant bit first
            auto& b = buf[curr_byte];
            b |= data_push << (8-curr_bit-nb_push);

            nb_bits_remain -= nb_push;
            curr_bit += nb_push;
            if (curr_bit == 8) {
                curr_bit = 0;
                curr_byte++;
                const int new_length = curr_byte+1;
                buf.resize(size_t(new_length));
            }
        }
    }
    void Reset() {
        curr_byte = 0;
        curr_bit = 0;
    }
    int GetTotalBytesCeil() const {
        return curr_byte + (curr_bit ? 1 : 0);
    }
};

// Copied from libfaad/common.c
static uint8_t get_index_from_sample_rate(const uint32_t samplerate) {
    // Does rounding to nearest sample rate
    if (samplerate >= 92017) return 0; 
    if (samplerate >= 75132) return 1;
    if (samplerate >= 55426) return 2;
    if (samplerate >= 46009) return 3;
    if (samplerate >= 37566) return 4;
    if (samplerate >= 27713) return 5;
    if (samplerate >= 23004) return 6;
    if (samplerate >= 18783) return 7;
    if (samplerate >= 13856) return 8;
    if (samplerate >= 11502) return 9;
    if (samplerate >=  9391) return 10;
    return 11;
}

static uint32_t get_sample_rate_from_index(const size_t index) {
    static const uint32_t sample_rates[12] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000
    };
    return sample_rates[index];
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

    const uint8_t sample_rate_index = get_index_from_sample_rate(m_params.sampling_frequency);

    // DOC: ETSI TS 102 563 
    // In Table 4 it states that when the SBR flag is used that 
    // the sampling rate of the AAC core is half the sampling rate of the DAC
    const uint32_t core_sample_rate = m_params.is_SBR ? (m_params.sampling_frequency/2) : m_params.sampling_frequency;
    const uint8_t core_sample_rate_index = get_index_from_sample_rate(core_sample_rate);

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
    const uint8_t channel_config = m_params.is_stereo ? 2 : 1;

    // Build the mp4 bitfield header
    BitPusherHelper bit_pusher;
    // Required header
    // Select AAC-LC as object file type
    bit_pusher.Push(m_mp4_bitfile_config, AAC_LC_index, 5);    
    bit_pusher.Push(m_mp4_bitfile_config, core_sample_rate_index, 4);
    bit_pusher.Push(m_mp4_bitfile_config, channel_config, 4);

    // DOC: ETSI TS 102 563 
    // In clause 5.1 it states that the 960 transform must be used
    // In libfaad2 you can accomplish this by setting the following bit
    // to change the transform type
    bit_pusher.Push(m_mp4_bitfile_config, 1, 1);

    // Flags so that we don't have optional fields for 
    // the core coder or extension type
    bit_pusher.Push(m_mp4_bitfile_config, 0, 1);
    bit_pusher.Push(m_mp4_bitfile_config, 0, 1);

    // Sync extension and SBR
    // To enable sync extension we need to pass in a special identifier code
    const uint16_t SYNC_EXTENSION_TYPE_SBR = 0x2B7;
    if (m_params.is_SBR) {
        bit_pusher.Push(m_mp4_bitfile_config, SYNC_EXTENSION_TYPE_SBR, 11);
        bit_pusher.Push(m_mp4_bitfile_config, SBR_index, 5);
        bit_pusher.Push(m_mp4_bitfile_config, 1, 1);
        bit_pusher.Push(m_mp4_bitfile_config, sample_rate_index, 4);
    }
    m_mp4_bitfile_config.resize(size_t(bit_pusher.GetTotalBytesCeil()));
}

void AAC_Audio_Decoder::GenerateMPEG4Header() {
    const uint8_t AAC_LC_index = 2;
    const uint8_t channel_config  = m_params.is_stereo ? 2 : 1;

    // DOC: ETSI TS 102 563 
    // In Table 4 it states that when the SBR flag is used that 
    // the sampling rate of the AAC core is half the sampling rate of the DAC
    const uint32_t core_sample_rate = m_params.is_SBR ? (m_params.sampling_frequency/2) : m_params.sampling_frequency;
    const uint8_t core_sample_rate_index = get_index_from_sample_rate(core_sample_rate);
 
    // Source: https://wiki.multimedia.cx/index.php/ADTS
    BitPusherHelper bit_pusher;
    bit_pusher.Push(m_mpeg4_header, 0xFFF, 12); // Syncword all 1s
    bit_pusher.Push(m_mpeg4_header, 0, 1); // MPEG Version: 0 = MPEG4, 1 = MPEG2
    bit_pusher.Push(m_mpeg4_header, 0, 2); // Layer all 0s
    bit_pusher.Push(m_mpeg4_header, 1, 1); // Protection absence: 1 for no CRC
    bit_pusher.Push(m_mpeg4_header, AAC_LC_index-1, 2); // Profile
    bit_pusher.Push(m_mpeg4_header, core_sample_rate_index, 4); // Sampling frequency index
    bit_pusher.Push(m_mpeg4_header, 0, 1); // Private bit (unused in decoding)
    bit_pusher.Push(m_mpeg4_header, channel_config, 3); // Channel config
    bit_pusher.Push(m_mpeg4_header, 0, 1); // Originality
    bit_pusher.Push(m_mpeg4_header, 0, 1); // Home usage
    bit_pusher.Push(m_mpeg4_header, 0, 1); // Copyright
    bit_pusher.Push(m_mpeg4_header, 0, 1); // Copyright id start
    bit_pusher.Push(m_mpeg4_header, 0, 13); // Frame length including headers (placeholder)
    bit_pusher.Push(m_mpeg4_header, 0x7FF, 11); // Variable bitrate
    bit_pusher.Push(m_mpeg4_header, 1-1, 2); // Number of raw data blocks in frame - 1 (Single AAC frame raw data block is advised)
    const int total_size = bit_pusher.GetTotalBytesCeil();
    assert(total_size == 7);
    m_mpeg4_header.resize(size_t(total_size));
}

// Generate MPEG4 Header for ADTS format
tcb::span<const uint8_t> AAC_Audio_Decoder::GetMPEG4Header(uint16_t frame_length_bytes) {
    // Most of MPEG4 header is pregenerated, we only need to store the number of bytes in the associated frame
    // frame length is stored 30bits into the header
    uint16_t total_frame_bytes = uint16_t(m_mpeg4_header.size()) + frame_length_bytes;
    total_frame_bytes &= 0b1'1111'1111'1111;
    // introduce 24bit (3 bytes) offset
    m_mpeg4_header[3] = (m_mpeg4_header[3] & 0b1111'1100) | ((total_frame_bytes & 0b1'1000'0000'0000) >> 11); // 2bits
    m_mpeg4_header[4] = (m_mpeg4_header[4] & 0b0000'0000) | ((total_frame_bytes & 0b0'0111'1111'1000) >> 3);  // 8bits
    m_mpeg4_header[5] = (m_mpeg4_header[5] & 0b0001'1111) | ((total_frame_bytes & 0b0'0000'0000'0111) << 5);  // 3bits
    return m_mpeg4_header;
}

AAC_Audio_Decoder::AAC_Audio_Decoder(const struct Params _params)
: m_params(_params)
{
    m_mp4_bitfile_config.resize(32);
    m_mpeg4_header.resize(32);
    GenerateBitfileConfig();
    GenerateMPEG4Header();

    m_decoder_handle = NeAACDecOpen();
    m_decoder_frame_info = new NeAACDecFrameInfo();
    auto decoder_config = NeAACDecGetCurrentConfiguration(m_decoder_handle);
    // outputing 16bit PCM 
    decoder_config->outputFormat = FAAD_FMT_16BIT;
    decoder_config->dontUpSampleImplicitSBR = false;
    NeAACDecSetConfiguration(m_decoder_handle, decoder_config);

    unsigned long out_sample_rate = 0;
    unsigned char out_total_channels = 0;
    NeAACDecInit2(
        m_decoder_handle, m_mp4_bitfile_config.data(), (unsigned long)m_mp4_bitfile_config.size(),
        &out_sample_rate, &out_total_channels);

    // TODO: manage the errors that libfaad spits out
}

AAC_Audio_Decoder::~AAC_Audio_Decoder() {
    NeAACDecClose(m_decoder_handle);
    delete m_decoder_frame_info;
}

AAC_Audio_Decoder::Result AAC_Audio_Decoder::DecodeFrame(tcb::span<uint8_t> data) {
    const uint8_t* audio_data_buf = reinterpret_cast<const uint8_t*>(NeAACDecDecode(m_decoder_handle, m_decoder_frame_info, data.data(), int(data.size())));
    LOG_MESSAGE("aac_decoder_error={}", m_decoder_frame_info->error);

    // abort, if no output at all
    const int nb_consumed_bytes = m_decoder_frame_info->bytesconsumed;
    const int nb_samples = m_decoder_frame_info->samples;

    if (nb_consumed_bytes <= 0 || nb_samples <= 0 || nb_consumed_bytes != int(data.size())) {
        AAC_Audio_Decoder::Result res;
        res.audio_buf = {};
        res.is_error = true;
        res.error_code = m_decoder_frame_info->error;
        return res;
    }

    const int nb_output_bytes = nb_samples * sizeof(uint16_t);
    AAC_Audio_Decoder::Result res;
    res.audio_buf = tcb::span(audio_data_buf, size_t(nb_output_bytes));
    res.is_error = false;
    res.error_code = m_decoder_frame_info->error;
    return res;
}