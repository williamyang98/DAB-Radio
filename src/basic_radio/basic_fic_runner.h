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
    const DAB_Parameters params;
    DAB_Misc_Info misc_info;
    std::unique_ptr<DAB_Database_Updater> dab_db_updater;
    std::unique_ptr<FIC_Decoder> fic_decoder;
    std::unique_ptr<FIG_Processor> fig_processor;
    std::unique_ptr<Radio_FIG_Handler> fig_handler;
public:
    explicit BasicFICRunner(const DAB_Parameters& _params);
    ~BasicFICRunner();
    void Process(tcb::span<const viterbi_bit_t> fic_bits_buf);
    auto& GetDatabaseUpdater(void) { return *(dab_db_updater.get()); }
    const auto& GetMiscInfo(void) { return misc_info; }
};