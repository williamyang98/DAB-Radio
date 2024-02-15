#include "./render_ofdm_demod.h"
#include "./render_profiler.h"
#include "ofdm/ofdm_demodulator.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <implot.h>
#include <cmath>
#include <vector>

static void CalculateMagnitude(tcb::span<const std::complex<float>> fft_buf, tcb::span<float> mag_buf, const float scale=20.0f);
static void RenderControls(OFDM_Demod& demod);
static void RenderState(OFDM_Demod& demod);
static void RenderPlots(OFDM_Demod& demod);
static void RenderMagnitudeSpectrum(OFDM_Demod& demod);
static void RenderDemodulatedSymbols(OFDM_Demod& demod);
static void RenderSynchronisation(OFDM_Demod& demod);

constexpr float Fs = 2.048e6f; // OFDM sampling frequency

void RenderOFDMDemodulator(OFDM_Demod& demod) {
    RenderState(demod);
    RenderControls(demod);
    RenderPlots(demod);
}

void RenderPlots(OFDM_Demod& demod) {
    RenderMagnitudeSpectrum(demod);
    RenderSynchronisation(demod);
    RenderDemodulatedSymbols(demod);
}

void RenderSourceBuffer(tcb::span<const std::complex<float>> buf_raw)
{
    const size_t block_size = buf_raw.size();
    if (ImGui::Begin("Sampling buffer")) {
        if (ImPlot::BeginPlot("Block")) {
            const auto* buf = reinterpret_cast<const float*>(buf_raw.data());
            ImPlot::SetupAxisLimits(ImAxis_Y1, -128, 128, ImPlotCond_Once);
            ImPlot::PlotLine("Real", &buf[0], (int)block_size, 1.0f, 0, 0, 0, 2*sizeof(float));
            ImPlot::PlotLine("Imag", &buf[1], (int)block_size, 1.0f, 0, 0, 0, 2*sizeof(float));
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}


void RenderControls(OFDM_Demod& demod) {
    auto& cfg = demod.GetConfig();

    if (ImGui::Begin("Controls")) {
        if (ImGui::Button("Reset")) {
            demod.Reset();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Coarse frequency correction", &cfg.sync.is_coarse_freq_correction);
        ImGui::SliderFloat("Fine frequency beta", &cfg.sync.fine_freq_update_beta, 0.0f, 1.0f, "%.2f");
        {
            float frequency_offset_Hz = cfg.sync.max_coarse_freq_correction_norm * Fs;
            if (ImGui::SliderFloat("Max coarse frequency (Hz)", &frequency_offset_Hz, 0.0f, Fs/2.0f)) {
                cfg.sync.max_coarse_freq_correction_norm = frequency_offset_Hz / Fs;
            }
        }
        ImGui::SliderFloat("Coarse freq slow beta", &cfg.sync.coarse_freq_slow_beta, 0.0f, 1.0f);
        ImGui::SliderFloat("Impulse peak threshold (dB)", &cfg.sync.impulse_peak_threshold_db, 0, 100.0f, "%.f");
        ImGui::SliderFloat("Impulse peak distance weight", &cfg.sync.impulse_peak_distance_probability, 0.0f, 1.0f, "%.3f");
        static float null_threshold[2] = {0,0};
        null_threshold[0] = cfg.null_l1_search.thresh_null_start;
        null_threshold[1] = cfg.null_l1_search.thresh_null_end;
        if (ImGui::SliderFloat2("Null detection threshold", null_threshold, 0.0f, 1.0f, "%.2f")) {
            null_threshold[0] = std::min(null_threshold[0], null_threshold[1]);
            null_threshold[1] = std::max(null_threshold[0], null_threshold[1]);
            cfg.null_l1_search.thresh_null_start = null_threshold[0];
            cfg.null_l1_search.thresh_null_end = null_threshold[1];
        }
        ImGui::SliderFloat("L1 signal update beta", &cfg.signal_l1.update_beta, 0.0f, 1.0f, "%.2f");
    }
    ImGui::End();
}

void RenderState(OFDM_Demod& demod) {
    #define ENUM_TO_STRING(NAME) \
    case OFDM_Demod::State::NAME: ImGui::Text("State: "#NAME); break;

    if (ImGui::Begin("Stats")) {
        switch (demod.GetState()) {
        ENUM_TO_STRING(FINDING_NULL_POWER_DIP);
        ENUM_TO_STRING(READING_NULL_AND_PRS);
        ENUM_TO_STRING(RUNNING_COARSE_FREQ_SYNC);
        ENUM_TO_STRING(RUNNING_FINE_TIME_SYNC);
        ENUM_TO_STRING(READING_SYMBOLS);
        default: 
        ImGui::Text("State: Unknown"); 
            break;
        }
        ImGui::Text("Fine freq: %.2f Hz", demod.GetFineFrequencyOffset() * Fs);
        ImGui::Text("Coarse freq: %.2f Hz", demod.GetCoarseFrequencyOffset() * Fs);
        ImGui::Text("Net freq: %.2f Hz", demod.GetNetFrequencyOffset() * Fs);
        ImGui::Text("Signal level: %.2f", demod.GetSignalAverage());
        ImGui::Text("Frames read: %d", demod.GetTotalFramesRead());
        ImGui::Text("Frames desynced: %d", demod.GetTotalFramesDesync());
    }
    ImGui::End();

    #undef ENUM_TO_STRING
}

void RenderDemodulatedSymbols(OFDM_Demod& demod) {
    const auto params = demod.GetOFDMParams();
    const int total_symbols = (int)params.nb_frame_symbols;
    const int total_dqpsk_symbols = total_symbols-1;
    static int symbol_index = 0;

    if (ImGui::Begin("Demodulated Symbols")) {
        ImGui::SliderInt("DQPSK Symbol Index", &symbol_index, 0, total_dqpsk_symbols-1);
        if (ImGui::BeginTabBar("OFDM symbol plots")) {
            // Raw constellation
            if (ImGui::BeginTabItem("Raw vectors")) {
                const size_t N = params.nb_data_carriers;
                auto syms_vec_data = demod.GetFrameDataVec();
                // vec[0:1] = [real, imag]
                auto sym_vec = syms_vec_data.subspan(symbol_index*N, N);
                const double A = 4e6;
                if (ImPlot::BeginPlot("IQ", ImVec2(-1,0), ImPlotFlags_Equal)) {
                    ImPlot::SetupAxisLimits(ImAxis_X1, -A, A, ImPlotCond_Once);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, -A, A, ImPlotCond_Once);

                    auto* buf = reinterpret_cast<const float*>(sym_vec.data());
                    const float marker_size = 2.0f;
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, marker_size);
                    ImPlot::PlotScatter("IQ", &buf[0], &buf[1], (int)N, 0, 0, 2*sizeof(buf[0]));
                    ImPlot::EndPlot();
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Bits")) {
                const size_t nb_data_carriers = params.nb_data_carriers;
                const size_t nb_sym_bits = nb_data_carriers*2;
                auto syms_bits_data = demod.GetFrameDataBits();
                // bits[0:N-1]  = Real component
                // bits[N:2N-1] = Imaginary component
                auto sym_bits = syms_bits_data.subspan(symbol_index*nb_sym_bits, nb_sym_bits);

                static const int NB_REFERENCE = 4;
                struct Point {
                    viterbi_bit_t I; 
                    viterbi_bit_t Q;
                };
                static const Point REFERENCE_CONSTELLATION[NB_REFERENCE] = {
                    { SOFT_DECISION_VITERBI_LOW , SOFT_DECISION_VITERBI_LOW  },
                    { SOFT_DECISION_VITERBI_LOW , SOFT_DECISION_VITERBI_HIGH },
                    { SOFT_DECISION_VITERBI_HIGH, SOFT_DECISION_VITERBI_LOW  },
                    { SOFT_DECISION_VITERBI_HIGH, SOFT_DECISION_VITERBI_HIGH },
                };

                if (ImPlot::BeginPlot("Viterbi bits constellation", ImVec2(-1,0), ImPlotFlags_Equal)) {
                    const auto A = (double)SOFT_DECISION_VITERBI_HIGH * 4.0f;
                    ImPlot::SetupAxisLimits(ImAxis_X1, -A, A, ImPlotCond_Once);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, -A, A, ImPlotCond_Once);

                    auto* buf = sym_bits.data();

                    const float marker_size = 2.0f;
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Plus, marker_size);
                    ImPlot::PlotScatter("IQ", &buf[0], &buf[nb_data_carriers], (int)nb_data_carriers, 0, 0, sizeof(buf[0]));

                    ImPlot::PlotScatter("Reference", &REFERENCE_CONSTELLATION[0].I, &REFERENCE_CONSTELLATION[0].Q, NB_REFERENCE, 0, 0, sizeof(Point));
                    ImPlot::EndPlot();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Phase error")) {
                const size_t nb_data_carriers = params.nb_data_carriers;
                const size_t nb_sym_bits = nb_data_carriers*2;
                auto syms_bits_data = demod.GetFrameDataBits();
                // bits[0:N-1]  = Real component
                // bits[N:2N-1] = Imaginary component
                auto sym_bits = syms_bits_data.subspan(symbol_index*nb_sym_bits, nb_sym_bits);
                auto real_bits = sym_bits.first(nb_data_carriers);
                auto imag_bits = sym_bits.last(nb_data_carriers);

                // We are interested in computing the error in the argument of the complex bit
                // For a noiseless signal we expected 
                //      abs(real) = abs(imag) = SOFT_DECISION_VITERBI_HIGH
                //      arg(real + imag*j) = PI/4 + k*PI/2, k is integer
                // We can approximate the phase error by taking
                //      error = abs(real) - abs(imag)

                static auto bits_error = std::vector<viterbi_bit_t>();
                bits_error.resize(nb_data_carriers);

                for (size_t i = 0; i < nb_data_carriers; i++) {
                    bits_error[i] = std::abs(real_bits[i]) - std::abs(imag_bits[i]);
                }

                if (ImPlot::BeginPlot("Phase error", ImVec2(-1,0))) {
                    const auto A = (double)SOFT_DECISION_VITERBI_HIGH;
                    ImPlot::SetupAxisLimits(ImAxis_X1, -A, A, ImPlotCond_Once);
                    ImPlot::SetupAxis(ImAxis_Y1, NULL, ImPlotAxisFlags_AutoFit);

                    const int x_range = 2*SOFT_DECISION_VITERBI_HIGH+1;
                    const int x_step = 4;
                    const int total_bins = x_range / x_step;

                    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.5f);
                    ImPlot::PlotHistogram("Phase error", bits_error.data(), (int)bits_error.size(), total_bins, 1.0, ImPlotRange(-A, A));
                    ImPlot::PopStyleVar();
                    ImPlot::EndPlot();
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }


    }
    ImGui::End();
}

void RenderSynchronisation(OFDM_Demod& demod) {
    const auto params = demod.GetOFDMParams();
    auto& cfg = demod.GetConfig();

    if (ImGui::Begin("Fine time synchronisation")) {
        if (ImPlot::BeginPlot("Fine time response")) {
            auto buf = demod.GetImpulseResponse();
            ImPlot::SetupAxisLimits(ImAxis_Y1, 60, 150, ImPlotCond_Once);
            ImPlot::PlotLine("Impulse response", buf.data(), (int)buf.size());
            // Plot useful markers for fine time sync using time correlation
            int marker_id = 0;
            const int target_peak_x = (int)params.nb_cyclic_prefix;
            const int actual_peak_x = target_peak_x + demod.GetFineTimeOffset();
            double marker_0 = target_peak_x;
            double marker_1 = actual_peak_x;
            ImPlot::DragLineX(marker_id++, &marker_0, ImVec4(0,1,0,1), 1.0f, ImPlotDragToolFlags_NoInputs);
            ImPlot::DragLineX(marker_id++, &marker_1, ImVec4(1,0,0,1), 1.0f, ImPlotDragToolFlags_NoInputs);
            ImPlot::EndPlot();
        }
    }
    ImGui::End();

    if (ImGui::Begin("Coarse frequency response")) {
        if (ImPlot::BeginPlot("Coarse frequency response")) {
            auto buf = demod.GetCoarseFrequencyResponse();
            ImPlot::SetupAxisLimits(ImAxis_Y1, 180, 260, ImPlotCond_Once);
            ImPlot::PlotLine("Impulse response", buf.data(), (int)buf.size());

            // Plot useful markers for coarse freq sync using freq correlation
            const float coarse_freq_offset = std::round(demod.GetCoarseFrequencyOffset() * Fs);
            const float max_coarse_freq_offset = cfg.sync.max_coarse_freq_correction_norm * Fs;
            const float freq_fft_bin = Fs / float(params.nb_fft);
            const float peak_offset_x = -coarse_freq_offset / freq_fft_bin;
            const float max_offset_x = max_coarse_freq_offset / freq_fft_bin;

            const auto target_colour = ImVec4(0,0.8,0,1);
            const auto actual_colour = ImVec4(1,0,0,1);
            const auto limits_colour = ImVec4(0,0,0.8,1);

            int marker_id = 0;
            const float target_peak_x = float(params.nb_fft)/2.0f;
            const float actual_peak_x = target_peak_x + peak_offset_x;
            double marker_0 = target_peak_x;
            double marker_1 = target_peak_x - max_offset_x;
            double marker_2 = target_peak_x + max_offset_x;
            double marker_3 = actual_peak_x;
            ImPlot::DragLineX(marker_id++, &marker_0, target_colour, 1.0f, ImPlotDragToolFlags_NoInputs);
            ImPlot::DragLineX(marker_id++, &marker_1, limits_colour, 1.0f, ImPlotDragToolFlags_NoInputs);
            ImPlot::DragLineX(marker_id++, &marker_2, limits_colour, 1.0f, ImPlotDragToolFlags_NoInputs);
            ImPlot::DragLineX(marker_id++, &marker_3, actual_colour, 1.0f, ImPlotDragToolFlags_NoInputs);
            ImPlot::EndPlot();
        }
    }
    ImGui::End();

    if (ImGui::Begin("Correlation time buffer")) {
        auto buf_raw = demod.GetCorrelationTimeBuffer();
        const size_t N = buf_raw.size();
        if (ImPlot::BeginPlot("NULL+PRS")) {
            const auto* buf = reinterpret_cast<const float*>(buf_raw.data());
            ImPlot::SetupAxisLimits(ImAxis_Y1, -128, 128, ImPlotCond_Once);
            ImPlot::PlotLine("Real", &buf[0], (int)N, 1.0f, 0, 0, 0, 2*sizeof(float));
            ImPlot::PlotLine("Imag", &buf[1], (int)N, 1.0f, 0, 0, 0, 2*sizeof(float));

            const auto target_colour = ImVec4(0,0.8,0,1);
            int marker_id = 0;
            const int target_x = (int)params.nb_null_period;
            double marker_0 = target_x;
            ImPlot::DragLineX(marker_id++, &marker_0, target_colour, 1.0f, ImPlotDragToolFlags_NoInputs);
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

void RenderMagnitudeSpectrum(OFDM_Demod& demod) {
    const auto params = demod.GetOFDMParams();

    // NOTE: We are calculating the magnitude spectrum in the GUI thread because
    //       the ofdm demodulation process doesn't need this
    // TODO: Move the magnitude calculation into the demodulator if we plan on doing 
    //       TII decoding (transmittor identifier information)
    if (ImGui::Begin("Null symbol spectrum")) {
        if (ImPlot::BeginPlot("Null symbol")) {
            const int N = (int)params.nb_fft;
            auto fft_buf = demod.GetFrameFFT();
            auto null_fft = fft_buf.subspan(N*params.nb_frame_symbols, N);

            static auto mag_buf = std::vector<float>();
            mag_buf.resize(N);

            CalculateMagnitude(null_fft, mag_buf);

            ImPlot::SetupAxisLimits(ImAxis_Y1, 20, 90, ImPlotCond_Once);
            ImPlot::PlotLine("Null symbol", mag_buf.data(), N);
            ImPlot::EndPlot();
        }
    }
    ImGui::End();

    // NOTE: We are calculating the magnitude spectrum in the GUI thread because
    //       the ofdm demodulation process doesn't need this
    if (ImGui::Begin("Data symbol spectrum")) {
        const int total_symbols = (int)params.nb_frame_symbols;

        static int symbol_index = 0;
        ImGui::SliderInt("Data Symbol Index", &symbol_index, 0, total_symbols-1);

        if (ImPlot::BeginPlot("Data symbol spectrum")) {
            const int N = (int)params.nb_fft;
            auto fft_buf = demod.GetFrameFFT();
            auto syms_fft_buf = fft_buf.first(N*params.nb_frame_symbols);

            static auto mag_buf = std::vector<float>();
            mag_buf.resize(N);

            auto sym_fft_buf = syms_fft_buf.subspan(symbol_index*N, N);
            CalculateMagnitude(sym_fft_buf, mag_buf);

            ImPlot::SetupAxisLimits(ImAxis_Y1, 20, 90, ImPlotCond_Once);
            ImPlot::PlotLine("Data symbol", mag_buf.data(), N);
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

void CalculateMagnitude(tcb::span<const std::complex<float>> fft_buf, tcb::span<float> mag_buf, const float scale) {
    const size_t N = fft_buf.size();
    const size_t M = N/2;
    // F/2 <= f < 0 
    for (size_t i = 0; i < M; i++) {
        mag_buf[i] = scale*std::log10(std::abs(fft_buf[i+M]));
    }
    // 0 <= f < F/2 
    for (size_t i = 0; i < M; i++) {
        mag_buf[i+M] = scale*std::log10(std::abs(fft_buf[i]));
    }
}
