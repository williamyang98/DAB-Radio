#include "./basic_database_manager.h"
#include "dab/database/dab_database.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(__VA_ARGS__)

Basic_Database_Manager::Basic_Database_Manager() {
    db = std::make_unique<DAB_Database>();
}

Basic_Database_Manager::~Basic_Database_Manager() = default;

bool Basic_Database_Manager::OnDatabaseUpdater(DAB_Database& src_db, DAB_Database_Updater& updater) {
    auto curr_stats = updater.GetStatistics();
    const bool is_changed = (live_stats != curr_stats);
    live_stats = curr_stats;

    const int nb_updates_delta = live_stats.nb_updates - stable_stats.nb_updates;
    const bool is_force_update = (nb_updates_delta > nb_force_update_threshold);

    // if too many changes have occured
    if (is_force_update) {
        LOG_MESSAGE("Force updating internal database");
        UpdateDatabase(updater);
        return true;
    }

    // If there are a few changes, wait for changes to stabilise
    if (is_changed) {
        is_awaiting_db_update = true;
        nb_cooldown = 0;
        return false;
    }

    // If we know the databases are desynced update cooldown
    if (is_awaiting_db_update) {
        nb_cooldown++;
        LOG_MESSAGE("cooldown={}/{}", nb_cooldown, nb_cooldown_max);
    }

    if (nb_cooldown != nb_cooldown_max) {
        return false;
    }

    LOG_MESSAGE("Slow updating internal database");
    UpdateDatabase(updater);
    return true;
}

void Basic_Database_Manager::OnMiscInfo(const DAB_Misc_Info& _misc_info) {
    misc_info = _misc_info;
}

void Basic_Database_Manager::UpdateDatabase(DAB_Database_Updater& updater) {
    stable_stats = live_stats;
    is_awaiting_db_update = false;
    nb_cooldown = 0;
    auto lock = std::scoped_lock(mutex_db);
    updater.ExtractCompletedDatabase(GetDatabase());
}

