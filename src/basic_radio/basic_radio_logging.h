#pragma once

#if BASIC_RADIO_LOGGING_USE_EASYLOGGING

#include <easylogging++.h>
#include <string>

static const char* BASIC_RADIO_LOGGER = "basic-radio";
static void BASIC_RADIO_LOG_MESSAGE(const std::string&& message) { CLOG(INFO, BASIC_RADIO_LOGGER) << message; }
static void BASIC_RADIO_LOG_WARN(const std::string&& message) { CLOG(WARNING, BASIC_RADIO_LOGGER) << message; }
static void BASIC_RADIO_LOG_ERROR(const std::string&& message) { CLOG(ERROR, BASIC_RADIO_LOGGER) << message; }
static void BASIC_RADIO_SET_THREAD_NAME(const std::string&& name) { el::Helpers::setThreadName(name); }

#else

#define BASIC_RADIO_LOG_MESSAGE(message) (void)0
#define BASIC_RADIO_LOG_WARN(message) (void)0
#define BASIC_RADIO_LOG_ERROR(message) (void)0
#define BASIC_RADIO_SET_THREAD_NAME(name) (void)0

#endif
