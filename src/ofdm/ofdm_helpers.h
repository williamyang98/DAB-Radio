#pragma once

#include <complex>
#include <memory>
#include <vector>
#include "./dab_mapper_ref.h"
#include "./dab_ofdm_params_ref.h"
#include "./dab_prs_ref.h"
#include "./ofdm_demodulator.h"
#include "./ofdm_params.h"

static std::unique_ptr<OFDM_Demod> Create_OFDM_Demodulator(const int transmission_mode, const int total_threads=0) {
    const OFDM_Params ofdm_params = get_DAB_OFDM_params(transmission_mode);
    auto ofdm_prs_ref = std::vector<std::complex<float>>(ofdm_params.nb_fft);
    get_DAB_PRS_reference(transmission_mode, ofdm_prs_ref);
    auto ofdm_mapper_ref = std::vector<int>(ofdm_params.nb_data_carriers);
    get_DAB_mapper_ref(ofdm_mapper_ref, ofdm_params.nb_fft);
    auto ofdm_demod = std::make_unique<OFDM_Demod>(ofdm_params, ofdm_prs_ref, ofdm_mapper_ref, total_threads);
    return ofdm_demod;
}