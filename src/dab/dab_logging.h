#pragma once

#if DAB_LOGGING_USE_EASYLOGGING

#include <easylogging++.h>
#include <vector>
#include <string>
std::vector<const char*>& get_dab_registered_loggers();
static bool DAB_LOG_REGISTER(const char* name) {
    auto& loggers = get_dab_registered_loggers();
    for (const char* other_logger: loggers) {
        if (other_logger == name) return false;
    }
    loggers.push_back(name);
    return true;
}
static void DAB_LOG_MESSAGE(const char* name, const std::string&& message) { CLOG(INFO, name) << message; }
static void DAB_LOG_WARN(const char* name, const std::string&& message) { CLOG(WARNING, name) << message; }
static void DAB_LOG_ERROR(const char* name, const std::string&& message) { CLOG(ERROR, name) << message; }

#else

static bool DAB_LOG_REGISTER(const char* name) { return false; }
#define DAB_LOG_MESSAGE(name, message) (void)0
#define DAB_LOG_WARN(name, message) (void)0
#define DAB_LOG_ERROR(name, message) (void)0

#endif
