#pragma once

#if BASIC_SCRAPER_LOGGING_USE_EASYLOGGING

#include <easylogging++.h>
#include <string>

constexpr const char* BASIC_SCRAPER_LOGGER = "basic-scraper";
static void BASIC_SCRAPER_LOG_MESSAGE(const std::string&& message) { CLOG(INFO, BASIC_SCRAPER_LOGGER) << message; }
static void BASIC_SCRAPER_LOG_WARN(const std::string&& message) { CLOG(WARNING, BASIC_SCRAPER_LOGGER) << message; }
static void BASIC_SCRAPER_LOG_ERROR(const std::string&& message) { CLOG(ERROR, BASIC_SCRAPER_LOGGER) << message; }

#else

#define BASIC_SCRAPER_LOG_MESSAGE(message) (void)0
#define BASIC_SCRAPER_LOG_WARN(message) (void)0
#define BASIC_SCRAPER_LOG_ERROR(message) (void)0

#endif
