#include "basic_fic_runner.h"

#include "modules/dab/dab_misc_info.h"
#include "modules/dab/database/dab_database.h"
#include "modules/dab/database/dab_database_updater.h"
#include "modules/dab/fic/fic_decoder.h"
#include "modules/dab/fic/fig_processor.h"
#include "modules/dab/radio_fig_handler.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(__VA_ARGS__)

BasicFICRunner::BasicFICRunner(const DAB_Parameters _params) 
: params(_params)
{
    dab_db = std::make_unique<DAB_Database>();
    dab_db_updater = std::make_unique<DAB_Database_Updater>(dab_db.get());
    fic_decoder = std::make_unique<FIC_Decoder>(params.nb_fib_cif_bits, params.nb_fibs_per_cif);
    fig_processor = std::make_unique<FIG_Processor>();
    fig_handler = std::make_unique<Radio_FIG_Handler>();

    fig_handler->SetUpdater(dab_db_updater.get());
    fig_handler->SetMiscInfo(&misc_info);
    fig_processor->SetHandler(fig_handler.get());
    fic_decoder->OnFIB().Attach([this](tcb::span<const uint8_t> buf) {
        fig_processor->ProcessFIB(buf);
    });
}

BasicFICRunner::~BasicFICRunner() {
    Stop();
    Join();
}

void BasicFICRunner::SetBuffer(tcb::span<const viterbi_bit_t> _buf) {
    fic_bits_buf = _buf;
}

void BasicFICRunner::BeforeRun() {
    el::Helpers::setThreadName("FIC");
}

void BasicFICRunner::Run() {
    if (fic_bits_buf.empty()) {
        LOG_ERROR("Got NULL fic bits buffer");
        return;
    }

    const int nb_fic_bits = (int)fic_bits_buf.size(); 
    if (nb_fic_bits != params.nb_fic_bits) {
        LOG_ERROR("Got incorrect number of bits in fic {]/{}", nb_fic_bits, params.nb_fic_bits);
        return;
    }


    for (int i = 0; i < params.nb_cifs; i++) {
        const int N = params.nb_fib_cif_bits;
        const auto fib_cif_buf = fic_bits_buf.subspan(i*N, N);
        fic_decoder->DecodeFIBGroup(fib_cif_buf, i);
    }
}