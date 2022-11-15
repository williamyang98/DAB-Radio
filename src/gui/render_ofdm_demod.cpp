#include "render_ofdm_demod.h"
#include "modules/ofdm/ofdm_demodulator.h"

#include "imgui.h"
#include "implot.h"

void RenderSourceBuffer(tcb::span<const std::complex<float>> buf_raw)
{
    const size_t block_size = buf_raw.size();
    static double x = 0.0f;
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

void RenderOFDMDemodulator_Plots(OFDM_Demod& demod);
void RenderOFDMDemodulator_Controls(OFDM_Demod& demod);
void RenderOFDMDemodulator_State(OFDM_Demod& demod);

void RenderOFDMDemodulator(OFDM_Demod& demod) {
    RenderOFDMDemodulator_State(demod);
    RenderOFDMDemodulator_Controls(demod);
    RenderOFDMDemodulator_Plots(demod);
}

void RenderOFDMDemodulator_Plots(OFDM_Demod& demod) {
    const auto params = demod.GetOFDMParams();
    auto& cfg = demod.GetConfig();

    if (ImGui::Begin("DQPSK data")) {
        const int total_symbols = (int)params.nb_frame_symbols;
        static int symbol_index = 0;

        ImGui::SliderInt("DQPSK Symbol Index", &symbol_index, 0, total_symbols-2);

        static double dqsk_decision_boundaries[3] = {-3.1415/2, 0, 3.1415/2};
        auto phase_buf = demod.GetFrameDataPhases();

        if (ImPlot::BeginPlot("DQPSK data")) {
            const int total_carriers = (int)params.nb_data_carriers;
            const int buffer_offset = symbol_index*total_carriers;

            ImPlot::SetupAxisLimits(ImAxis_Y1, -4, +4, ImPlotCond_Once);
            ImPlot::SetupAxis(ImAxis_Y2, NULL, ImPlotAxisFlags_Opposite);
            ImPlot::SetupAxisLimits(ImAxis_Y2, -1, 4, ImPlotCond_Once);
            static double y_axis_ticks[4] = {0,1,2,3};
            ImPlot::SetupAxisTicks(ImAxis_Y2, y_axis_ticks, 4);
            {
                auto* buf = &phase_buf[buffer_offset];
                ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
                ImPlot::PlotScatter("Raw", buf, total_carriers);
                for (int i = 0; i < 3; i++) {
                    ImPlot::DragLineY(i, &dqsk_decision_boundaries[i], ImVec4(1,0,0,1), 1.0f, ImPlotDragToolFlags_NoInputs);
                }
            }

            ImPlot::EndPlot();
        }
    }
    ImGui::End();

    if (ImGui::Begin("Constellation")) {
        static int maximum_points = 3000;
        constexpr int slider_res = 1000;
        if (ImGui::SliderInt("Total points", &maximum_points, 0, 20000)) {
            maximum_points = (maximum_points / slider_res) * slider_res;
        }

        if (ImPlot::BeginPlot("Constellation", ImVec2(-1,0), ImPlotFlags_Equal)) {
            static float A = 4e6;
            ImPlot::SetupAxisLimits(ImAxis_X1, -A, A, ImPlotCond_Once);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -A, A, ImPlotCond_Once);

            auto points = demod.GetFrameDataVec();
            const size_t nb_points = points.size();
            auto* buf = reinterpret_cast<float*>(points.data());

            const float marker_size = 1.5f;
            ImPlot::SetNextMarkerStyle(0, marker_size);
            ImPlot::PlotScatter("IQ", &buf[0], &buf[1], std::min((int)nb_points, maximum_points), 0, 0, 2*sizeof(float));
            ImPlot::EndPlot();
        }
    }
    ImGui::End();

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
        static bool show_markers = true;
        if (ImPlot::BeginPlot("Coarse frequency response")) {
            auto buf = demod.GetCoarseFrequencyResponse();
            ImPlot::SetupAxisLimits(ImAxis_Y1, 180, 260, ImPlotCond_Once);
            ImPlot::PlotLine("Impulse response", buf.data(), (int)buf.size());

            // Plot useful markers for coarse freq sync using freq correlation
            const int coarse_freq_offset = (int)demod.GetCoarseFrequencyOffset();
            const int max_coarse_freq_offset = cfg.sync.max_coarse_freq_correction;
            const int freq_fft_bin = (int)params.freq_carrier_spacing;
            const int peak_offset_x = -coarse_freq_offset / freq_fft_bin;
            const int max_offset_x = max_coarse_freq_offset / freq_fft_bin;

            const auto target_colour = ImVec4(0,0.8,0,1);
            const auto actual_colour = ImVec4(1,0,0,1);
            const auto limits_colour = ImVec4(0,0,0.8,1);

            int marker_id = 0;
            const int target_peak_x = (int)params.nb_fft/2;
            const int actual_peak_x = target_peak_x + peak_offset_x;
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

    // TODO:
    // {
    //     ImGui::Begin("Null symbol spectrum");
    //     if (ImPlot::BeginPlot("Null symbol")) {
    //         auto buf = demod.GetNullSymbolMagnitude();
    //         const int N = params.nb_fft;
    //         ImPlot::SetupAxisLimits(ImAxis_Y1, 20, 90, ImPlotCond_Once);
    //         ImPlot::PlotLine("Null symbol", buf, N);
    //         ImPlot::EndPlot();
    //     }
    //     ImGui::End();
    // }

    // TODO:
    // {
    //     ImGui::Begin("Data symbol spectrum");
    //     if (ImPlot::BeginPlot("Data symbol spectrum")) {
    //         auto buf = demod.GetFrameMagnitudeAverage();
    //         const int N = params.nb_fft;
    //         ImPlot::SetupAxisLimits(ImAxis_Y1, 20, 90, ImPlotCond_Once);
    //         ImPlot::PlotLine("Data symbol", buf, N);
    //         ImPlot::EndPlot();
    //     }
    //     ImGui::End();
    // }
}

void RenderOFDMDemodulator_Controls(OFDM_Demod& demod) {
    auto& cfg = demod.GetConfig();
    auto params = demod.GetOFDMParams();

    if (ImGui::Begin("Controls")) {
        if (ImGui::Button("Reset")) {
            demod.Reset();
        }

        ImGui::Checkbox("Update data symbol mag", &cfg.data_sym_mag.is_update);
        ImGui::SameLine();
        ImGui::Checkbox("Update tii symbol mag", &cfg.is_update_tii_sym_mag);
        ImGui::SameLine();
        ImGui::Checkbox("Coarse frequency correction", &cfg.sync.is_coarse_freq_correction);

        ImGui::SliderFloat("Fine frequency beta", &cfg.sync.fine_freq_update_beta, 0.0f, 1.0f, "%.2f");
        if (ImGui::SliderInt("Max coarse frequency (Hz)", &cfg.sync.max_coarse_freq_correction, 0, 100000)) {
            const int N = cfg.sync.max_coarse_freq_correction / (int)params.freq_carrier_spacing;
            cfg.sync.max_coarse_freq_correction = N * (int)params.freq_carrier_spacing;
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

        ImGui::SliderFloat("Data sym mag update beta", &cfg.data_sym_mag.update_beta, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("L1 signal update beta", &cfg.signal_l1.update_beta, 0.0f, 1.0f, "%.2f");
    }
    ImGui::End();
}

void RenderOFDMDemodulator_State(OFDM_Demod& demod) {
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
        ImGui::Text("Fine freq: %.2f Hz", demod.GetFineFrequencyOffset());
        ImGui::Text("Coarse freq: %.2f Hz", demod.GetCoarseFrequencyOffset());
        ImGui::Text("Net freq: %.2f Hz", demod.GetNetFrequencyOffset());
        ImGui::Text("Signal level: %.2f", demod.GetSignalAverage());
        ImGui::Text("Frames read: %d", demod.GetTotalFramesRead());
        ImGui::Text("Frames desynced: %d", demod.GetTotalFramesDesync());
    }
    ImGui::End();

    #undef ENUM_TO_STRING
}