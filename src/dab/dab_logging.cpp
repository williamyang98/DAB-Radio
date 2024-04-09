#include "./dab_logging.h"
#include <vector>

#if DAB_LOGGING_USE_EASYLOGGING

std::vector<const char*>& get_dab_registered_loggers() {
    static std::vector<const char*> loggers;
    return loggers;
}

#endif
