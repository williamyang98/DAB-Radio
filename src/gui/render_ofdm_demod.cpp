#include "render_ofdm_demod.h"
#include "ofdm/ofdm_demodulator.h"

#include "imgui.h"
#include "implot.h"

void RenderSourceBuffer(std::complex<float>* buf_raw, const int block_size)
{
    static double x = 0.0f;
    if (ImGui::Begin("Sampling buffer")) {
        if (ImPlot::BeginPlot("Block")) {
            auto buf = reinterpret_cast<float*>(buf_raw);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -128, 128, ImPlotCond_Once);
            ImPlot::PlotLine("Real", &buf[0], block_size, 1.0f, 0, 0, 0, 2*sizeof(float));
            ImPlot::PlotLine("Imag", &buf[1], block_size, 1.0f, 0, 0, 0, 2*sizeof(float));
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

void RenderOFDMDemodulator(OFDM_Demod* demod)
{
    const auto params = demod->GetOFDMParams();
    if (ImGui::Begin("DQPSK data")) {
        const int total_symbols = params.nb_frame_symbols;
        static int symbol_index = 0;

        ImGui::SliderInt("DQPSK Symbol Index", &symbol_index, 0, total_symbols-2);

        static double dqsk_decision_boundaries[3] = {-3.1415/2, 0, 3.1415/2};
        auto phase_buf = demod->GetFrameDataPhases();

        if (ImPlot::BeginPlot("DQPSK data")) {
            const int total_carriers = params.nb_data_carriers;
            const int buffer_offset = symbol_index*total_carriers;

            ImPlot::SetupAxisLimits(ImAxis_Y1, -4, +4, ImPlotCond_Once);
            ImPlot::SetupAxis(ImAxis_Y2, NULL, ImPlotAxisFlags_Opposite);
            ImPlot::SetupAxisLimits(ImAxis_Y2, -1, 4, ImPlotCond_Once);
            static double y_axis_ticks[4] = {0,1,2,3};
            ImPlot::SetupAxisTicks(ImAxis_Y2, y_axis_ticks, 4);
            {
                auto buf = &phase_buf[buffer_offset];
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
        if (ImPlot::BeginPlot("Constellation", ImVec2(-1,0), ImPlotFlags_Equal)) {
            const int total_carriers = params.nb_data_carriers;
            const int nb_symbols = params.nb_frame_symbols-1;
            const int nb_points = nb_symbols * total_carriers;
            auto* buf = reinterpret_cast<float*>(demod->GetFrameDataVec());

            const float marker_size = 1.5f;
            ImPlot::SetNextMarkerStyle(0, marker_size);
            ImPlot::PlotScatter("IQ", &buf[0], &buf[1], std::min(nb_points, 3000), 0, 0, 2*sizeof(float));
            ImPlot::EndPlot();
        }
    }
    ImGui::End();

    if (ImGui::Begin("Impulse response")) {
        if (ImPlot::BeginPlot("Impulse response")) {
            auto buf = demod->GetImpulseResponse();
            const int N = params.nb_fft;
            ImPlot::SetupAxisLimits(ImAxis_Y1, 60, 150, ImPlotCond_Once);
            ImPlot::PlotLine("Impulse response", buf, N);
            ImPlot::EndPlot();
        }
    }
    ImGui::End();

    // TODO:
    // {
    //     ImGui::Begin("Null symbol spectrum");
    //     if (ImPlot::BeginPlot("Null symbol")) {
    //         auto buf = demod->GetNullSymbolMagnitude();
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
    //         auto buf = demod->GetFrameMagnitudeAverage();
    //         const int N = params.nb_fft;
    //         ImPlot::SetupAxisLimits(ImAxis_Y1, 20, 90, ImPlotCond_Once);
    //         ImPlot::PlotLine("Data symbol", buf, N);
    //         ImPlot::EndPlot();
    //     }
    //     ImGui::End();
    // }

    if (ImGui::Begin("Controls/Stats")) {

        switch (demod->GetState()) {
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
        ImGui::Text("Fine freq: %.2f Hz", demod->GetFineFrequencyOffset());
        ImGui::Text("Signal level: %.2f", demod->GetSignalAverage());
        ImGui::Text("Frames read: %d", demod->GetTotalFramesRead());
        ImGui::Text("Frames desynced: %d", demod->GetTotalFramesDesync());
    }
    ImGui::End();
}