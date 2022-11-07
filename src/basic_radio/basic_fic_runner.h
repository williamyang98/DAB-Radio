#pragma once

#include "basic_threaded_channel.h"
#include "dab/constants/dab_parameters.h"
#include "dab/dab_misc_info.h"
#include "../viterbi_config.h"

class DAB_Database;
class DAB_Database_Updater;
class FIC_Decoder;
class FIG_Processor;
class Radio_FIG_Handler;

class BasicFICRunner: public BasicThreadedChannel
{
private:
    const DAB_Parameters params;
    DAB_Misc_Info* misc_info;
    DAB_Database* dab_db;
    DAB_Database_Updater* dab_db_updater;
    FIC_Decoder* fic_decoder;
    FIG_Processor* fig_processor;
    Radio_FIG_Handler* fig_handler;

    const viterbi_bit_t* fic_bits_buf = NULL;
    int nb_fic_bits = 0;
public:
    BasicFICRunner(const DAB_Parameters _params);
    ~BasicFICRunner();
    void SetBuffer(const viterbi_bit_t* _buf, const int _N);
    auto GetLiveDatabase(void) { return dab_db; }
    auto GetDatabaseUpdater(void) { return dab_db_updater; }
    auto GetMiscInfo(void) { return misc_info; }
protected:
    virtual void BeforeRun();
    virtual void Run();
};