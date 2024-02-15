#pragma once

#include <memory>

#include "dab/constants/dab_parameters.h"
#include "dab/dab_misc_info.h"
#include "utility/span.h"
#include "viterbi_config.h"

class DAB_Database_Updater;
class FIC_Decoder;
class FIG_Processor;
class Radio_FIG_Handler;

class BasicFICRunner
{
private:
    const DAB_Parameters m_params;
    DAB_Misc_Info m_misc_info;
    std::unique_ptr<DAB_Database_Updater> m_dab_db_updater;
    std::unique_ptr<FIC_Decoder> m_fic_decoder;
    std::unique_ptr<FIG_Processor> m_fig_processor;
    std::unique_ptr<Radio_FIG_Handler> m_fig_handler;
public:
    explicit BasicFICRunner(const DAB_Parameters& _params);
    ~BasicFICRunner();
    void Process(tcb::span<const viterbi_bit_t> fic_bits_buf);
    auto& GetDatabaseUpdater(void) { return *(m_dab_db_updater.get()); }
    const auto& GetMiscInfo(void) { return m_misc_info; }
};