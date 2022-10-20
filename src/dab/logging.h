#pragma once

#include <easylogging++.h>

struct DAB_Loggers {
    el::Logger* aac_decoder;
    el::Logger* aac_frame;
    el::Logger* radio_fig_handler;
    el::Logger* db_updater;
    el::Logger* fic_decoder;
    el::Logger* fig_processor;
    el::Logger* msc_decoder;
};

struct DAB_Loggers RegisterLogging() {
    struct DAB_Loggers loggers;
    loggers.aac_decoder         = el::Loggers::getLogger("aac-decoder");
    loggers.aac_frame           = el::Loggers::getLogger("aac-frame");
    loggers.radio_fig_handler   = el::Loggers::getLogger("radio-fig-handler");
    loggers.db_updater          = el::Loggers::getLogger("db-updater");
    loggers.fic_decoder         = el::Loggers::getLogger("fic-decoder");
    loggers.fig_processor       = el::Loggers::getLogger("fig-processor");
    loggers.msc_decoder         = el::Loggers::getLogger("msc-decoder");
    return loggers;
}
