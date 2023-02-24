#pragma once

// User configurable constants for decoder
template <typename error_t>
struct ViterbiDecoder_Config 
{
    error_t soft_decision_max_error;            // max total error for R output symbols against reference
    error_t initial_start_error;
    error_t initial_non_start_error;
    error_t renormalisation_threshold;          // threshold to normalise all errors to 0
};