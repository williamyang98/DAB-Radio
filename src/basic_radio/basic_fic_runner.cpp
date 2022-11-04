#include "basic_fic_runner.h"

#include "dab/dab_misc_info.h"
#include "dab/database/dab_database.h"
#include "dab/database/dab_database_updater.h"
#include "dab/fic/fic_decoder.h"
#include "dab/fic/fig_processor.h"
#include "dab/radio_fig_handler.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(##__VA_ARGS__)

BasicFICRunner::BasicFICRunner(const DAB_Parameters _params) 
: params(_params)
{
    misc_info = new DAB_Misc_Info();
    dab_db = new DAB_Database();
    dab_db_updater = new DAB_Database_Updater(dab_db);
    fic_decoder = new FIC_Decoder(params.nb_fib_cif_bits, params.nb_fibs_per_cif);
    fig_processor = new FIG_Processor();
    fig_handler = new Radio_FIG_Handler();

    fig_handler->SetUpdater(dab_db_updater);
    fig_handler->SetMiscInfo(misc_info);
    fig_processor->SetHandler(fig_handler);
    fic_decoder->OnFIB().Attach([this](const uint8_t* buf, const int N) {
        fig_processor->ProcessFIB(buf);
    });
}

BasicFICRunner::~BasicFICRunner() {
    Stop();
    Join();
    delete misc_info;
    delete dab_db;
    delete dab_db_updater;
    delete fic_decoder;
    delete fig_processor;
    delete fig_handler;
}

void BasicFICRunner::SetBuffer(const viterbi_bit_t* _buf, const int _N) {
    fic_bits_buf = _buf;
    nb_fic_bits = _N;
}

void BasicFICRunner::BeforeRun() {
    el::Helpers::setThreadName("FIC");
}

void BasicFICRunner::Run() {
    if (nb_fic_bits != params.nb_fic_bits) {
        LOG_ERROR("Got incorrect number of bits in fic {]/{}", nb_fic_bits, params.nb_fic_bits);
        return;
    }

    if (fic_bits_buf == NULL) {
        LOG_ERROR("Got NULL fic bits buffer");
        return;
    }

    for (int i = 0; i < params.nb_cifs; i++) {
        const auto* fib_cif_buf = &fic_bits_buf[params.nb_fib_cif_bits*i];
        fic_decoder->DecodeFIBGroup(fib_cif_buf, i);
    }
}