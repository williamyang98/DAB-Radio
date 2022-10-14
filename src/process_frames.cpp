#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <io.h>
#include <fcntl.h>

#include "./getopt/getopt.h"

#include "fic_decoder.h"
#include "fig_processor.h"
#include "radio_fig_handler.h"
#include "dab_database.h"
#include "dab_database_updater.h"

#define PRINT_LOG 1

#if PRINT_LOG 
  #define LOG_MESSAGE(...) fprintf(stderr, ##__VA_ARGS__)
#else
  #define LOG_MESSAGE(...) (void)0
#endif

class App: public FIC_Decoder::Callback
{
private:
    FIC_Decoder* fic_decoder;
    FIG_Processor* fig_processor;
    Radio_FIG_Handler* fig_handler;
    DAB_Database* dab_db;
    DAB_Database_Updater* dab_db_updater;
public:
    App() {
        fic_decoder = new FIC_Decoder();
        fig_processor = new FIG_Processor();
        fig_handler = new Radio_FIG_Handler();
        dab_db = new DAB_Database();
        dab_db_updater = new DAB_Database_Updater(dab_db);

        fig_handler->SetUpdater(dab_db_updater);
        fig_processor->SetHandler(fig_handler);
        fic_decoder->SetCallback(this);
    }
    ~App() {
        delete fic_decoder;
        delete fig_processor;
        delete fig_handler;
        delete dab_db;
        delete dab_db_updater;
    }
    void ProcessFrame(uint8_t* buf, const int N) {
        const int nb_frame_length = 28800;
        const int nb_symbols = 75;
        const int nb_sym_length = nb_frame_length / nb_symbols;

        const int nb_fic_symbols = 3;

        const auto* fic_buf = &buf[0];
        const auto* msc_buf = &buf[nb_fic_symbols*nb_sym_length];

        const int nb_fic_length = nb_sym_length*nb_fic_symbols;
        const int nb_msc_length = nb_sym_length*(nb_symbols-nb_fic_symbols);

        // FIC: 3 symbols -> 12 FIBs -> 4 FIB groups
        // A FIB group contains FIGs (fast information group)
        const int nb_fic_groups = 4;
        const int nb_fic_group_length = nb_fic_length / nb_fic_groups;
        for (int i = 0; i < nb_fic_groups; i++) {
            const auto* fic_group_buf = &fic_buf[i*nb_fic_group_length];
            fic_decoder->DecodeFIBGroup(fic_group_buf, i);
        }
    }
    virtual void OnDecodeFIBGroup(const uint8_t* buf, const int N, const int cif_index) {
        fig_processor->ProcessFIG(buf, cif_index);
    }
    void FilterPending() {
        auto& v = dab_db_updater->GetUpdaters();
        std::vector<UpdaterBase*> pending;
        std::vector<UpdaterBase*> complete;
        for (auto& e: v) {
            if (!e->IsComplete()) {
                pending.push_back(e);
            } else {
                complete.push_back(e);
            }
        }
    }
};

void usage() {
    fprintf(stderr, 
        "process_frames, decoded DAB frame data\n\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) {
    char* rd_filename = NULL;

    int opt; 
    while ((opt = getopt(argc, argv, "i:h")) != -1) {
        switch (opt) {
        case 'i':
            rd_filename = optarg;
            break;
        case 'h':
        default:
            usage();
            return 0;
        }
    }

    // app startup
    FILE* fp_in = stdin;
    if (rd_filename != NULL) {
        errno_t err = fopen_s(&fp_in, rd_filename, "r");
        if (err != 0) {
            LOG_MESSAGE("Failed to open file for reading\n");
            return 1;
        }
    }

    _setmode(_fileno(fp_in), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    const int N = 28800;
    auto buf = new uint8_t[N];

    auto app = App();

    while (true) {
        const auto nb_read = fread(buf, sizeof(uint8_t), N, fp_in);
        if (nb_read != N) {
            fprintf(stderr, "Failed to read %d bytes\n", N);
            break;
        }
        app.ProcessFrame(buf, N);
    }

    app.FilterPending();

    return 0;
}
