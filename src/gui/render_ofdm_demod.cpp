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

void RenderOFDMDemodulator(OFDM_Demod& demod)
{
    const auto params = demod.GetOFDMParams();
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
                    ImPlot::DragLineY(i, &dqsk_decision_boundaries[i], ImVec4(1,0,0,1), 1.0f);
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

    if (ImGui::Begin("Impulse response")) {
        if (ImPlot::BeginPlot("Impulse response")) {
            auto buf = demod.GetImpulseResponse();
            ImPlot::SetupAxisLimits(ImAxis_Y1, 60, 150, ImPlotCond_Once);
            ImPlot::PlotLine("Impulse response", buf.data(), (int)buf.size());
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

    if (ImGui::Begin("Controls/Stats")) {

        switch (demod.GetState()) {
        case OFDM_Demod::State::FINDING_NULL_POWER_DIP:
            ImGui::Text("State: Finding dip");
            break;
        case OFDM_Demod::State::CALCULATE_PRS_CORRELATION:
            ImGui::Text("State: Finding PRS correlation peak");
            break;
        case OFDM_Demod::State::READING_BUFFER:
            ImGui::Text("State: Reading buffer");
            break;
        default:
            ImGui::Text("State: Unknown");
            break;
        }
        ImGui::Text("Fine freq: %.2f Hz", demod.GetFineFrequencyOffset());
        ImGui::Text("Signal level: %.2f", demod.GetSignalAverage());
        ImGui::Text("Frames read: %d", demod.GetTotalFramesRead());
        ImGui::Text("Frames desynced: %d", demod.GetTotalFramesDesync());
    }
    ImGui::End();
}