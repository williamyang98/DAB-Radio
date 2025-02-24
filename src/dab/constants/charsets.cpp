#include <array>
#include <string>
#include <string_view>
#include <stddef.h>
#include <stdint.h>
#include <uchar.h>
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
    "\0", "Ę",  "Į",  "Ų", "Ă", "Ė", "Ď", "Ș", "Ț", "Ċ", "\0", "\0", "Ġ", "Ĺ", "Ż", "Ń",
    "ą",  "ę",  "į",  "ų", "ă", "ė", "ď", "ș", "ț", "ċ", "Ň",  "Ě",  "ġ", "ĺ", "ż", "\0",
    " ",  "!",  "\"", "#", "ł", "%", "&", "'", "(", ")", "*",  "+",  ",", "-", ".", "/" ,
    "0",  "1",  "2",  "3", "4", "5", "6", "7", "8", "9", ":",  ";",  "<", "=", ">", "?",
    "@",  "A",  "B",  "C", "D", "E", "F", "G", "H", "I", "J",  "K",  "L", "M", "N", "O",
    "P",  "Q",  "R",  "S", "T", "U", "V", "W", "X", "Y", "Z",  "[",  "Ů", "]", "Ł", "_",
    "Ą",  "a",  "b",  "c", "d", "e", "f", "g", "h", "i", "j",  "k",  "l", "m", "n", "o",
    "p",  "q",  "r",  "s", "t", "u", "v", "w", "x", "y", "z",  "«",  "ů", "»", "Ľ", "Ħ",
    "á",  "à",  "é",  "è", "í", "ì", "ó", "ò", "ú", "ù", "Ñ",  "Ç",  "Ş", "ß", "¡", "Ÿ",
    "â",  "ä",  "ê",  "ë", "î", "ï", "ô", "ö", "û", "ü", "ñ",  "ç",  "ş", "ğ", "ı", "ÿ",
    "Ķ",  "Ņ",  "©",  "Ģ", "Ğ", "ě", "ň", "ő", "Ő", "€", "£",  "$",  "Ā", "Ē", "Ī", "Ū",
    "ķ",  "ņ",  "Ļ",  "ģ", "ļ", "İ", "ń", "ű", "Ű", "¿", "ľ",  "°",  "ā", "ē", "ī", "ū",
    "Á",  "À",  "É",  "È", "Í", "Ì", "Ó", "Ò", "Ú", "Ù", "Ř",  "Č",  "Š", "Ž", "Ð", "Ŀ",
    "Â",  "Ä",  "Ê",  "Ë", "Î", "Ï", "Ô", "Ö", "Û", "Ü", "ř",  "č",  "š", "ž", "đ", "ŀ",
    "Ã",  "Å",  "Æ",  "Œ", "ŷ", "Ý", "Õ", "Ø", "Þ", "Ŋ", "Ŕ",  "Ć",  "Ś", "Ź", "Ť", "ð",
    "ã",  "å",  "æ",  "œ", "ŵ", "ý", "õ", "ø", "þ", "ŋ", "ŕ",  "ć",  "ś", "ź", "ť", "ħ"
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

// static std::string convert_utf16_to_utf8(tcb::span<const uint8_t> utf16_string) {
//     // TODO: Convert utf16 big endian to utf8
// }

static std::string convert_to_utf8(tcb::span<const uint8_t> buf) {
    return std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
}

// Table 1: Charset values
std::string convert_charset_to_utf8(tcb::span<const uint8_t> buf, uint8_t charset) {
    // NOTE: This value is bit shifted in FIG_Processor::ProcessFIG_Type_1
    switch (charset) {
    case 0b0000: return convert_ebu_latin_to_utf8(buf);
    // case 0b0110: return convert_utf16_to_utf8(buf);
    case 0b1111: return convert_to_utf8(buf);
    default:
        auto string = convert_to_utf8(buf);
        LOG_ERROR("unknown charset={}, buf={}", charset, string);
        return string;
    }
}
