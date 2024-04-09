#include "./basic_fic_runner.h"
#include <stdint.h>
#include <memory>
#include <fmt/format.h>
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_updater.h"
#include "dab/fic/fic_decoder.h"
#include "dab/fic/fig_processor.h"
#include "dab/radio_fig_handler.h"
#include "utility/span.h"
#include "viterbi_config.h"
#include "./basic_radio_logging.h"
#define LOG_MESSAGE(...) BASIC_RADIO_LOG_MESSAGE(fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) BASIC_RADIO_LOG_ERROR(fmt::format(__VA_ARGS__))

BasicFICRunner::BasicFICRunner(const DAB_Parameters& _params) 
: m_params(_params)
{
    m_dab_db_updater = std::make_unique<DAB_Database_Updater>();
    m_fic_decoder = std::make_unique<FIC_Decoder>(m_params.nb_fib_cif_bits, m_params.nb_fibs_per_cif);
    m_fig_processor = std::make_unique<FIG_Processor>();
    m_fig_handler = std::make_unique<Radio_FIG_Handler>();

    m_fig_handler->SetUpdater(m_dab_db_updater.get());
    m_fig_handler->SetMiscInfo(&m_misc_info);
    m_fig_processor->SetHandler(m_fig_handler.get());
    m_fic_decoder->OnFIB().Attach([this](tcb::span<const uint8_t> buf) {
        m_fig_processor->ProcessFIB(buf);
    });
}

BasicFICRunner::~BasicFICRunner() = default;

void BasicFICRunner::Process(tcb::span<const viterbi_bit_t> fic_bits_buf) {
    BASIC_RADIO_SET_THREAD_NAME("FIC");

    const int nb_fic_bits = (int)fic_bits_buf.size(); 
    if (nb_fic_bits != m_params.nb_fic_bits) {
        LOG_ERROR("Got incorrect number of bits in fic {]/{}", nb_fic_bits, m_params.nb_fic_bits);
        return;
    }


    for (int i = 0; i < m_params.nb_cifs; i++) {
        const int N = m_params.nb_fib_cif_bits;
        const auto fib_cif_buf = fic_bits_buf.subspan(i*N, N);
        m_fic_decoder->DecodeFIBGroup(fib_cif_buf, i);
    }
}