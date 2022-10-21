#include "render_ofdm_demod.h"
#include "ofdm/ofdm_demodulator.h"
#include "ofdm/ofdm_symbol_mapper.h"

#include "imgui.h"
#include "implot.h"

void RenderSourceBuffer(std::complex<float>* buf_raw, const int block_size)
{
    static double x = 0.0f;
    ImGui::Begin("Sampling buffer");
    if (ImPlot::BeginPlot("Block")) {
        auto buf = reinterpret_cast<float*>(buf_raw);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -128, 128, ImPlotCond_Once);
        ImPlot::PlotLine("Real", &buf[0], block_size, 1.0f, 0, 0, 0, 2*sizeof(float));
        ImPlot::PlotLine("Imag", &buf[1], block_size, 1.0f, 0, 0, 0, 2*sizeof(float));
        ImPlot::EndPlot();
    }
    ImGui::End();
}

void RenderOFDMDemodulator(OFDM_Demodulator* demod, OFDM_Symbol_Mapper* mapper)
{
    const auto params = demod->GetOFDMParams();
    {
        ImGui::Begin("DQPSK data");
        const int total_symbols = params.nb_frame_symbols;
        static int symbol_index = 0;

        ImGui::SliderInt("DQPSK Symbol Index", &symbol_index, 0, total_symbols-2);

        static double dqsk_decision_boundaries[3] = {-3.1415/2, 0, 3.1415/2};
        auto phase_buf = demod->GetFrameDataPhases();
        auto mapper_buf = demod->GetFrameDataPhasesPred();

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

            {
                auto buf = &mapper_buf[buffer_offset];
                ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 3.0f, ImVec4(1.0f, 0.4f, 0.0f, 1.0f), 2.0f);
                ImPlot::PlotScatter("Predicted", buf, total_carriers);
            }

            ImPlot::EndPlot();
        }
        ImGui::End();
    }

    {
        ImGui::Begin("Impulse response");
        if (ImPlot::BeginPlot("Impulse response")) {
            auto buf = demod->GetImpulseResponse();
            const int N = params.nb_fft;
            ImPlot::SetupAxisLimits(ImAxis_Y1, 60, 150, ImPlotCond_Once);
            ImPlot::PlotLine("Impulse response", buf, N);
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

    {
        ImGui::Begin("Null symbol spectrum");
        if (ImPlot::BeginPlot("Null symbol")) {
            auto buf = demod->GetNullSymbolMagnitude();
            const int N = params.nb_fft;
            ImPlot::SetupAxisLimits(ImAxis_Y1, 20, 90, ImPlotCond_Once);
            ImPlot::PlotLine("Null symbol", buf, N);
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

    {
        ImGui::Begin("Data symbol spectrum");
        if (ImPlot::BeginPlot("Data symbol spectrum")) {
            auto buf = demod->GetFrameMagnitudeAverage();
            const int N = params.nb_fft;
            ImPlot::SetupAxisLimits(ImAxis_Y1, 20, 90, ImPlotCond_Once);
            ImPlot::PlotLine("Data symbol", buf, N);
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

    {
        ImGui::Begin("Controls/Stats");

        ImGui::Checkbox("Force fine freq", &(demod->GetIsUpdateFineFrequency()));
        switch (demod->GetState()) {
        case OFDM_Demodulator::State::WAITING_NULL:
            ImGui::Text("State: Waiting null");
            break;
        case OFDM_Demodulator::State::READING_OFDM_FRAME:
            ImGui::Text("State: Reading data symbol");
            break;
        case OFDM_Demodulator::State::READING_NULL_SYMBOL:
            ImGui::Text("State: Reading null symbol");
            break;
        default:
            ImGui::Text("State: Unknown");
            break;
        }
        ImGui::Text("Fine freq: %.2f Hz", demod->GetFineFrequencyOffset());
        ImGui::Text("Signal level: %.2f", demod->GetSignalAverage());
        ImGui::Text("Symbols read: %d/%d", 
            demod->GetCurrentOFDMSymbol(),
            params.nb_frame_symbols);
        ImGui::Text("Frames read: %d", demod->GetTotalFramesRead());
        ImGui::Text("Frames desynced: %d", demod->GetTotalFramesDesync());

        ImGui::End();
    }
}