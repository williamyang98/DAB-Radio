#pragma once

#include <easylogging++.h>

struct DAB_Loggers {
    el::Logger* fic_decoder;
    el::Logger* fig_processor;
    el::Logger* radio_fig_handler;
    el::Logger* db_updater;

    el::Logger* msc_decoder;
    el::Logger* msc_xpad_processor;

    el::Logger* aac_frame;
    el::Logger* aac_audio_decoder;
    el::Logger* aac_data_decoder;

    el::Logger* pad_processor;
    el::Logger* pad_data_length;
    el::Logger* pad_dynamic_label;
    el::Logger* pad_MOT;

    el::Logger* mot_processor;
    el::Logger* mot_assembler;
    el::Logger* mot_slideshow;
};

struct DAB_Loggers RegisterLogging() {
    struct DAB_Loggers loggers;

    loggers.fic_decoder         = el::Loggers::getLogger("fic-decoder");
    loggers.fig_processor       = el::Loggers::getLogger("fig-processor");
    loggers.radio_fig_handler   = el::Loggers::getLogger("radio-fig-handler");
    loggers.db_updater          = el::Loggers::getLogger("db-updater");

    loggers.msc_decoder         = el::Loggers::getLogger("msc-decoder");
    loggers.msc_xpad_processor  = el::Loggers::getLogger("msc-xpad-processor");

    loggers.aac_frame           = el::Loggers::getLogger("aac-frame");
    loggers.aac_audio_decoder   = el::Loggers::getLogger("aac-audio-decoder");
    loggers.aac_data_decoder    = el::Loggers::getLogger("aac-data-decoder");

    loggers.pad_processor       = el::Loggers::getLogger("pad-processor");
    loggers.pad_data_length     = el::Loggers::getLogger("pad-data-length");
    loggers.pad_dynamic_label   = el::Loggers::getLogger("pad-dynamic-label");
    loggers.pad_MOT             = el::Loggers::getLogger("pad-MOT");

    loggers.mot_processor       = el::Loggers::getLogger("mot-processor");
    loggers.mot_assembler       = el::Loggers::getLogger("mot-assembler");
    loggers.mot_slideshow       = el::Loggers::getLogger("mot-slideshow");

    return loggers;
}
