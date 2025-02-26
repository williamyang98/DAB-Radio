#include <array>
#include <string>
#include <string_view>
#include <stddef.h>
#include <stdint.h>
#include <uchar.h>
#include <optional>
#include "utility/span.h"
#include <fmt/format.h>
#include "../dab_logging.h"
#define TAG "charset"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

// DOC: ETSI EN 101 756
// Annex C: Complete EBU Latin based repertoire
const static auto EBU_LATIN_CHARACTERS = std::array<std::string_view, 256>{
    "\0", "Ę",  "Į",  "Ų", "Ă", "Ė", "Ď", "Ș", "Ț", "Ċ", "",  "",  "Ġ", "Ĺ", "Ż", "Ń",
    "ą",  "ę",  "į",  "ų", "ă", "ė", "ď", "ș", "ț", "ċ", "Ň", "Ě", "ġ", "ĺ", "ż", "",
    " ",  "!",  "\"", "#", "ł", "%", "&", "'", "(", ")", "*", "+", ",", "-", ".", "/" ,
    "0",  "1",  "2",  "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", "=", ">", "?",
    "@",  "A",  "B",  "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
    "P",  "Q",  "R",  "S", "T", "U", "V", "W", "X", "Y", "Z", "[", "Ů", "]", "Ł", "_",
    "Ą",  "a",  "b",  "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o",
    "p",  "q",  "r",  "s", "t", "u", "v", "w", "x", "y", "z", "«", "ů", "»", "Ľ", "Ħ",
    "á",  "à",  "é",  "è", "í", "ì", "ó", "ò", "ú", "ù", "Ñ", "Ç", "Ş", "ß", "¡", "Ÿ",
    "â",  "ä",  "ê",  "ë", "î", "ï", "ô", "ö", "û", "ü", "ñ", "ç", "ş", "ğ", "ı", "ÿ",
    "Ķ",  "Ņ",  "©",  "Ģ", "Ğ", "ě", "ň", "ő", "Ő", "€", "£", "$", "Ā", "Ē", "Ī", "Ū",
    "ķ",  "ņ",  "Ļ",  "ģ", "ļ", "İ", "ń", "ű", "Ű", "¿", "ľ", "°", "ā", "ē", "ī", "ū",
    "Á",  "À",  "É",  "È", "Í", "Ì", "Ó", "Ò", "Ú", "Ù", "Ř", "Č", "Š", "Ž", "Ð", "Ŀ",
    "Â",  "Ä",  "Ê",  "Ë", "Î", "Ï", "Ô", "Ö", "Û", "Ü", "ř", "č", "š", "ž", "đ", "ŀ",
    "Ã",  "Å",  "Æ",  "Œ", "ŷ", "Ý", "Õ", "Ø", "Þ", "Ŋ", "Ŕ", "Ć", "Ś", "Ź", "Ť", "ð",
    "ã",  "å",  "æ",  "œ", "ŵ", "ý", "õ", "ø", "þ", "ŋ", "ŕ", "ć", "ś", "ź", "ť", "ħ"
};

static std::string convert_ebu_latin_to_utf8(tcb::span<const uint8_t> ebu_latin_string) {
    size_t total_size = 0;
    for (auto& x: ebu_latin_string) {
        auto& Y = EBU_LATIN_CHARACTERS[x];
        total_size += Y.size();
    }
    std::string utf8_string(total_size, '\0');
    size_t i = 0;
    for (auto& x: ebu_latin_string) {
        auto& Y = EBU_LATIN_CHARACTERS[x];
        for (auto y: Y) {
            utf8_string[i] = y;
            i++;
        }
    }
    return utf8_string;
}

// ISO 8859-1: https://en.wikipedia.org/wiki/ISO/IEC_8859-1
const static auto LATIN_ALPHABET_1_CHARACTERS = std::array<std::string_view, 256> {
     "",  "",  "",   "",  "",  "",  "",  "",  "",  "",  "",  "",  "",   "",  "",  "",
     "",  "",  "",   "",  "",  "",  "",  "",  "",  "",  "",  "",  "",   "",  "",  "",
     " ", "!", "\"", "#", "$", "%", "&", "'", "(", ")", "*", "+", ",",  "-", ".", "/",
     "0", "1", "2",  "3", "4", "5", "6", "7", "8", "9", ":", ";", "<",  "=", ">", "?",
     "@", "A", "B",  "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",  "M", "N", "O",
     "P", "Q", "R",  "S", "T", "U", "V", "W", "X", "Y", "Z", "[", "\\", "]", "^", "_",
     "`", "a", "b",  "c", "d", "e", "f", "g", "h", "i", "j", "k", "l",  "m", "n", "o",
     "p", "q", "r",  "s", "t", "u", "v", "w", "x", "y", "z", "{", "|",  "}", "~", "",
     "",  "",  "",   "",  "",  "",  "",  "",  "",  "",  "",  "",  "",   "",  "",  "",
     "",  "",  "",   "",  "",  "",  "",  "",  "",  "",  "",  "",  "",   "",  "",  "",
     "\u00A0", "¡",  "¢", "£", "¤", "¥", "¦", "§", "¨", "©", "ª", "«",  "¬", "\u00AD", "®", "¯",
     "°", "±", "²",  "³", "´", "µ", "¶", "·", "¸", "¹", "º", "»", "¼",  "½", "¾", "¿",
     "À", "Á", "Â",  "Ã", "Ä", "Å", "Æ", "Ç", "È", "É", "Ê", "Ë", "Ì",  "Í", "Î", "Ï",
     "Ð", "Ñ", "Ò",  "Ó", "Ô", "Õ", "Ö", "×", "Ø", "Ù", "Ú", "Û", "Ü",  "Ý", "Þ", "ß",
     "à", "á", "â",  "ã", "ä", "å", "æ", "ç", "è", "é", "ê", "ë", "ì",  "í", "î", "ï",
     "ð", "ñ", "ò",  "ó", "ô", "õ", "ö", "÷", "ø", "ù", "ú", "û", "ü",  "ý", "þ", "ÿ",
};

static std::string convert_latin_alphabet_1_to_utf8(tcb::span<const uint8_t> latin_string) {
    size_t total_size = 0;
    for (auto& x: latin_string) {
        auto& Y = LATIN_ALPHABET_1_CHARACTERS[x];
        total_size += Y.size();
    }
    std::string utf8_string(total_size, '\0');
    size_t i = 0;
    for (auto& x: latin_string) {
        auto& Y = LATIN_ALPHABET_1_CHARACTERS[x];
        for (auto y: Y) {
            utf8_string[i] = y;
            i++;
        }
    }
    return utf8_string;
}

// DAB UTF-16 is limited to the basic multilingual plane (BMP) which is 16bits for each codepoint
// It is also stored as big endian
static std::string convert_utf16_to_utf8(tcb::span<const uint8_t> utf16_string) {
    // https://en.wikipedia.org/wiki/Plane_(Unicode)#Basic_Multilingual_Plane
    // UTF16 requires the entire basic multilingual plane (BMP)
    //      Full range:         U+0000 - U+FFFF
    // There is an unallocated range located at
    //                          U+2FE0 - U+2FEF
    // The surrogate range isn't actually rendered, they are there to represent language planes above the BMP
    // Surrogate range is:
    //      High surrogates     U+D800 - U+DB7F
    //      High private use    U+DB80 - U+DBFF
    //      Low surrogates      U+DC00 - U+DFFF

    size_t total_utf16_bytes = utf16_string.size();
    if (total_utf16_bytes % 2 != 0) total_utf16_bytes--; // round to 16bits

    // represent utf16 2byte codepoints with utf8 continuation bytes
    std::string utf8_string;
    utf8_string.reserve(total_utf16_bytes);

    std::optional<uint16_t> high_surrogate = std::nullopt;
    for (size_t i = 0; i < total_utf16_bytes; i+=2) {
        const uint16_t c = uint16_t(utf16_string[i]) << 8 | uint16_t(utf16_string[i+1]); // big endian

        // https://en.wikipedia.org/wiki/Universal_Character_Set_characters#Surrogates
        // A pair of high and low surrogates addresses U+010000-U+100000 according to the equation
        // C = 0x10000 + (H-0xD800)*0x0400 + (L-0xDC00)
        if (high_surrogate != std::nullopt) {
            if (c >= 0xDC00 && c <= 0xDFFF) {
                const uint32_t H = uint32_t(high_surrogate.value());
                const uint32_t L = uint32_t(c);
                const uint32_t C = 0x10000 + (H-0xD800)*0x0400 + (L-0xDC00);
                // 1111_0xxx, 10xx_xxxx, 10xx_xxxx, 10xx_xxxx
                utf8_string.push_back(0b1111'0000 | uint8_t((C & 0b0001'1100'0000'0000'0000'0000) >> 18));
                utf8_string.push_back(0b1000'0000 | uint8_t((C & 0b0000'0011'1111'0000'0000'0000) >> 12));
                utf8_string.push_back(0b1000'0000 | uint8_t((C & 0b0000'0000'0000'1111'1100'0000) >> 6));
                utf8_string.push_back(0b1000'0000 | uint8_t((C & 0b0000'0000'0000'0000'0011'1111) >> 0));
                high_surrogate = std::nullopt;
                continue;
            } else if (c >= 0xD800 && c <= 0xDBFF) {
                LOG_ERROR(
                    "high surrogate received twice in a row, first={:02x}, second={:02x}",
                    high_surrogate.value(), c
                );
                // override original first high surrogate assuming the previous one was a fluke
                high_surrogate = c;
                continue;
            } else {
                LOG_ERROR(
                    "surrogate pair missing low surrogate, high_surrogate={:02x}, bad_low_surrogate={:02x}",
                    high_surrogate.value(), c
                );
                // isolated surrogates should be ignored and codepoint processed as normal
                high_surrogate = std::nullopt;
                // @fallthrough
            }
        }

        if (c <= 0x007F) {
            // 0xxx_xxxx
            utf8_string.push_back(uint8_t(c & 0x007F));
        } else if (c <= 0x07FF) {
            // 110x_xxxx, 10xx_xxxx
            utf8_string.push_back(0b1100'0000 | uint8_t((c & 0b0000'0111'1100'0000) >> 6));
            utf8_string.push_back(0b1000'0000 | uint8_t((c & 0b0000'0000'0011'1111) >> 0));
        } else if (c >= 0x2FE0 && c <= 0x2FEF) {
            // ignore gap in BMP
        } else if (c >= 0xD800 && c <= 0xDBFF) {
            high_surrogate = c;
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            LOG_ERROR("got low surrogate first instead of high surrogate {:02x}", c);
        } else {
            // 1110_xxxx, 10xx_xxxx, 10xx_xxxx
            utf8_string.push_back(0b1110'0000 | uint8_t((c & 0b1111'0000'0000'0000) >> 12));
            utf8_string.push_back(0b1000'0000 | uint8_t((c & 0b0000'1111'1100'0000) >> 6));
            utf8_string.push_back(0b1000'0000 | uint8_t((c & 0b0000'0000'0011'1111) >> 0));
        }
    }
    return utf8_string;
}

static std::string convert_to_utf8(tcb::span<const uint8_t> buf) {
    return std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
}

std::string convert_charset_to_utf8(tcb::span<const uint8_t> buf, uint8_t charset) {
    // DOC: ETSI EN 101 756
    // Table 1: Charset sets for FIG type 1 dasta field and dynamic labels
    // Table 19: Character set indicators for MOT ContentName
    switch (charset) {
    case 0b0000: return convert_ebu_latin_to_utf8(buf);
    case 0b0100: return convert_latin_alphabet_1_to_utf8(buf);
    case 0b0110: return convert_utf16_to_utf8(buf);
    case 0b1111: return convert_to_utf8(buf);
    default:
        auto string = convert_to_utf8(buf);
        LOG_ERROR("unknown charset={}, buf={}", charset, string);
        return string;
    }
}
