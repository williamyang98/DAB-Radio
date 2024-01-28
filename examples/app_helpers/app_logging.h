#pragma once

#include <easylogging++.h>
#include "dab/dab_logging.h"
#include "basic_radio/basic_radio_logging.h"
#include "basic_scraper/basic_scraper_logging.h"

static void setup_easylogging(bool is_default, bool is_basic_radio, bool is_basic_scraper) {
    el::Helpers::setThreadName("main-thread");
    const char* logging_format = "[%level] [%thread] [%logger] %msg";
    el::Logger* logger = nullptr;
    el::Configurations config;
    config.setToDefault();
    config.setGlobally(el::ConfigurationType::Format, logging_format);
    // default
    config.setGlobally(el::ConfigurationType::Enabled, is_default ? "true" : "false");
    el::Loggers::reconfigureAllLoggers(config);
    // basic radio
    config.setGlobally(el::ConfigurationType::Enabled, is_basic_radio ? "true" : "false");
    for (const char* name: get_dab_registered_loggers()) {
        logger = el::Loggers::getLogger(name);
        if (logger != nullptr) logger->configure(config);
    }
    logger = el::Loggers::getLogger(BASIC_RADIO_LOGGER);
    if (logger != nullptr) logger->configure(config);
    // basic scraper
    config.setGlobally(el::ConfigurationType::Enabled, is_basic_scraper ? "true" : "false");
    logger = el::Loggers::getLogger(BASIC_SCRAPER_LOGGER);
    if (logger != nullptr) logger->configure(config);
}
