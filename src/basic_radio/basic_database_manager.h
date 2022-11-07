#pragma once

#include <mutex>

#include "dab/dab_misc_info.h"
#include "dab/database/dab_database_updater.h"
#include "dab/database/dab_database.h"

class Basic_Database_Manager
{
private:
    std::mutex mutex_db;

    // keep track of database with completed entries
    DAB_Misc_Info misc_info;
    DAB_Database* db;
    DAB_Database_Updater::Statistics live_stats;
    DAB_Database_Updater::Statistics stable_stats;

    // don't update unless we have a sufficient cooldown
    bool is_awaiting_db_update = false;
    int nb_cooldown = 0;
    const int nb_cooldown_max = 10;
public:
    Basic_Database_Manager();
    ~Basic_Database_Manager();
    bool OnDatabaseUpdater(DAB_Database* src_db, DAB_Database_Updater* updater);
    void OnMiscInfo(DAB_Misc_Info& _misc_info);
    const auto& GetDABMiscInfo(void) { return misc_info; }
    // NOTE: you must get the mutex associated with this
    auto* GetDatabase(void) { return db; }
    auto& GetDatabaseMutex(void) { return mutex_db; }
    auto GetDatabaseStatistics(void) { return live_stats; }
private:
    void UpdateDatabase(DAB_Database_Updater* updater);
};