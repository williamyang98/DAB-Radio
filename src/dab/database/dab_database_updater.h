#pragma once

#include <stdint.h>
#include <assert.h>
#include <vector>
#include <string>
#include <memory>
#include "./dab_database.h"
#include "utility/span.h"

struct DatabaseUpdaterGlobalStatistics {
    size_t nb_total = 0;
    size_t nb_pending = 0;
    size_t nb_completed = 0;
    size_t nb_conflicts = 0;
    size_t nb_updates = 0;
    bool operator==(const DatabaseUpdaterGlobalStatistics& other) const {
        return 
            (nb_total == other.nb_total) &&
            (nb_pending == other.nb_pending) &&
            (nb_completed == other.nb_completed) &&
            (nb_conflicts == other.nb_conflicts) &&
            (nb_updates == other.nb_updates);
    }
    bool operator!=(const DatabaseUpdaterGlobalStatistics& other) const {
        return !(*this == other);
    }
};

enum class UpdateResult { SUCCESS, CONFLICT, NO_CHANGE };

template <typename T>
class DatabaseEntityUpdater
{
protected:
    size_t total_conflicts = 0;
    size_t total_updates = 0;
    bool is_complete = false;
    T dirty_field = T(0);
private:
    DatabaseUpdaterGlobalStatistics& stats;
public:
    DatabaseEntityUpdater(DatabaseUpdaterGlobalStatistics& _stats): stats(_stats) {}
    ~DatabaseEntityUpdater() {}
    virtual bool IsComplete() = 0;
    void OnCreate() {
        stats.nb_total++;
        stats.nb_pending++; 
        OnComplete();
    }
    void OnConflict() {
        total_conflicts++;
        stats.nb_conflicts++;
    }
    void OnComplete() {
        const bool new_is_complete = IsComplete();
        if (is_complete == new_is_complete) return;
        is_complete = new_is_complete;
        if (new_is_complete) {
            stats.nb_completed++;
            stats.nb_pending--;
        } else {
            stats.nb_completed--;
            stats.nb_pending++;
        }
    }
    void OnUpdate() {
        total_updates++;
        stats.nb_updates++;
    }
    UpdateResult UpdateField(std::string& dst, std::string_view src, T dirty_flag) {
        if (dirty_field & dirty_flag) {
            if (src.compare(dst) != 0) {
                OnConflict();
                return UpdateResult::CONFLICT;
            }
            return UpdateResult::NO_CHANGE;
        }
        dirty_field |= dirty_flag;
        dst = src;
        OnComplete();
        OnUpdate();
        return UpdateResult::SUCCESS;
    }
    template <typename U>
    UpdateResult UpdateField(U& dst, U src, T dirty_flag) {
        if (dirty_field & dirty_flag) {
            if (dst != src) {
                OnConflict();
                return UpdateResult::CONFLICT;
            }
            return UpdateResult::NO_CHANGE;
        }
        dirty_field |= dirty_flag;
        dst = src;
        OnComplete();
        OnUpdate();
        return UpdateResult::SUCCESS;
    }
};

class EnsembleUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& db;
public:
    explicit EnsembleUpdater(DAB_Database& _db, DatabaseUpdaterGlobalStatistics& _stats)
        : DatabaseEntityUpdater<uint8_t>(_stats), db(_db) { OnCreate(); } 
    UpdateResult SetReference(const ensemble_id_t reference);
    UpdateResult SetCountryID(const country_id_t country_id);
    UpdateResult SetExtendedCountryCode(const extended_country_id_t extended_country_code);
    UpdateResult SetLabel(tcb::span<const uint8_t> buf);
    UpdateResult SetNumberServices(const uint8_t nb_services);
    UpdateResult SetReconfigurationCount(const uint16_t reconfiguration_count);
    UpdateResult SetLocalTimeOffset(const int8_t local_time_offset);
    UpdateResult SetInternationalTableID(const uint8_t international_table_id) ;
    auto& GetData() { return db.ensemble; }
private:
    bool IsComplete() override;
};

class ServiceUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& db;
    const size_t index;
public:
    explicit ServiceUpdater(DAB_Database& _db, size_t _index, DatabaseUpdaterGlobalStatistics& _stats)
        : DatabaseEntityUpdater<uint8_t>(_stats), db(_db), index(_index) { OnCreate(); }
    UpdateResult SetCountryID(const country_id_t country_id);
    UpdateResult SetExtendedCountryCode(const extended_country_id_t extended_country_code);
    UpdateResult SetLabel(tcb::span<const uint8_t> buf);
    UpdateResult SetProgrammeType(const programme_id_t programme_type);
    UpdateResult SetLanguage(const language_id_t language);
    UpdateResult SetClosedCaption(const closed_caption_id_t closed_caption);
    auto& GetData() { return db.services[index]; }
private:
    bool IsComplete() override;
};

class ServiceComponentUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& db;
    const size_t index;
public:
    explicit ServiceComponentUpdater(DAB_Database& _db, size_t _index, DatabaseUpdaterGlobalStatistics& _stats)
        : DatabaseEntityUpdater<uint8_t>(_stats), db(_db), index(_index) { OnCreate(); }
    UpdateResult SetLabel(tcb::span<const uint8_t> buf);
    UpdateResult SetTransportMode(const TransportMode transport_mode);
    UpdateResult SetAudioServiceType(const AudioServiceType audio_service_type);
    UpdateResult SetDataServiceType(const DataServiceType data_service_type);
    UpdateResult SetSubchannel(const subchannel_id_t subchannel_id);
    UpdateResult SetGlobalID(const service_component_global_id_t global_id);
    uint32_t GetServiceReference();
    auto& GetData() { return db.service_components[index]; }
private:
    bool IsComplete() override;
};

class SubchannelUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& db;
    const size_t index;
public:
    explicit SubchannelUpdater(DAB_Database& _db, size_t _index, DatabaseUpdaterGlobalStatistics& _stats)
        : DatabaseEntityUpdater<uint8_t>(_stats), db(_db), index(_index) { OnCreate(); }
    UpdateResult SetStartAddress(const subchannel_addr_t start_address);
    UpdateResult SetLength(const subchannel_size_t length);
    UpdateResult SetIsUEP(const bool is_uep);
    UpdateResult SetUEPProtIndex(const uep_protection_index_t uep_prot_index);
    UpdateResult SetEEPProtLevel(const eep_protection_level_t eep_prot_level);
    UpdateResult SetEEPType(const EEP_Type eep_type);
    UpdateResult SetFECScheme(const fec_scheme_t fec_scheme);
    auto& GetData() { return db.subchannels[index]; }
private:
    bool IsComplete() override;
};

class LinkServiceUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& db;
    const size_t index;
public:
    explicit LinkServiceUpdater(DAB_Database& _db, size_t _index, DatabaseUpdaterGlobalStatistics& _stats)
        : DatabaseEntityUpdater<uint8_t>(_stats), db(_db), index(_index) { OnCreate(); }
    UpdateResult SetIsActiveLink(const bool is_active_link);
    UpdateResult SetIsHardLink(const bool is_hard_link);
    UpdateResult SetIsInternational(const bool is_international);
    UpdateResult SetServiceReference(const service_id_t service_reference);
    service_id_t GetServiceReference();
    auto& GetData() { return db.link_services[index]; }
private:
    bool IsComplete() override;
};

class FM_ServiceUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& db;
    const size_t index;
public:
    explicit FM_ServiceUpdater(DAB_Database& _db, size_t _index, DatabaseUpdaterGlobalStatistics& _stats)
        : DatabaseEntityUpdater<uint8_t>(_stats), db(_db), index(_index) { OnCreate(); }
    UpdateResult SetLinkageSetNumber(const lsn_t linkage_set_number); 
    UpdateResult SetIsTimeCompensated(const bool is_time_compensated);
    UpdateResult AddFrequency(const freq_t frequency);
    auto& GetData() { return db.fm_services[index]; }
private:
    bool IsComplete() override;
};

class DRM_ServiceUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& db;
    const size_t index;
public:
    explicit DRM_ServiceUpdater(DAB_Database& _db, size_t _index, DatabaseUpdaterGlobalStatistics& _stats)
        : DatabaseEntityUpdater<uint8_t>(_stats), db(_db), index(_index) { OnCreate(); }
    UpdateResult SetLinkageSetNumber(const lsn_t linkage_set_number); 
    UpdateResult SetIsTimeCompensated(const bool is_time_compensated);
    UpdateResult AddFrequency(const freq_t frequency);
    auto& GetData() { return db.drm_services[index]; }
private:
    bool IsComplete() override;
};

class AMSS_ServiceUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& db;
    const size_t index;
public:
    explicit AMSS_ServiceUpdater(DAB_Database& _db, size_t _index, DatabaseUpdaterGlobalStatistics& _stats)
        : DatabaseEntityUpdater<uint8_t>(_stats), db(_db), index(_index) { OnCreate(); }
    UpdateResult SetIsTimeCompensated(const bool is_time_compensated);
    UpdateResult AddFrequency(const freq_t frequency);
    auto& GetData() { return db.amss_services[index]; }
private:
    bool IsComplete() override;
};

class OtherEnsembleUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& db;
    const size_t index;
public:
    explicit OtherEnsembleUpdater(DAB_Database& _db, size_t _index, DatabaseUpdaterGlobalStatistics& _stats)
        : DatabaseEntityUpdater<uint8_t>(_stats), db(_db), index(_index) { OnCreate(); }
    UpdateResult SetCountryID(const country_id_t country_id);
    UpdateResult SetIsContinuousOutput(const bool is_continuous_output);
    UpdateResult SetIsGeographicallyAdjacent(const bool is_geographically_adjacent);
    UpdateResult SetIsTransmissionModeI(const bool is_transmission_mode_I);
    UpdateResult SetFrequency(const freq_t frequency);
    auto& GetData() { return db.other_ensembles[index]; }
private:
    bool IsComplete() override;
};

class DAB_Database_Updater
{
private:
    std::unique_ptr<DatabaseUpdaterGlobalStatistics> stats;
    std::unique_ptr<DAB_Database> db;
    std::unique_ptr<EnsembleUpdater> ensemble_updater;
    std::vector<ServiceUpdater> service_updaters;
    std::vector<ServiceComponentUpdater> service_component_updaters;
    std::vector<SubchannelUpdater> subchannel_updaters;
    std::vector<LinkServiceUpdater> link_service_updaters;
    std::vector<FM_ServiceUpdater> fm_service_updaters;
    std::vector<DRM_ServiceUpdater> drm_service_updaters;
    std::vector<AMSS_ServiceUpdater> amss_service_updaters;
    std::vector<OtherEnsembleUpdater> other_ensemble_updaters;
public:
    explicit DAB_Database_Updater();
    EnsembleUpdater& GetEnsembleUpdater() { return *(ensemble_updater.get()); }
    ServiceUpdater& GetServiceUpdater(const service_id_t service_ref);
    ServiceComponentUpdater& GetServiceComponentUpdater_Service(const service_id_t service_ref, const service_component_id_t component_id);
    SubchannelUpdater& GetSubchannelUpdater(const subchannel_id_t subchannel_id);
    LinkServiceUpdater& GetLinkServiceUpdater(const lsn_t link_service_number);
    FM_ServiceUpdater& GetFMServiceUpdater(const fm_id_t RDS_PI_code);
    DRM_ServiceUpdater& GetDRMServiceUpdater(const drm_id_t drm_code);
    AMSS_ServiceUpdater& GetAMSS_ServiceUpdater(const amss_id_t amss_code);
    OtherEnsembleUpdater& GetOtherEnsemble(const ensemble_id_t ensemble_reference);
    ServiceComponentUpdater* GetServiceComponentUpdater_GlobalID(const service_component_global_id_t global_id);
    ServiceComponentUpdater* GetServiceComponentUpdater_Subchannel(const subchannel_id_t subchannel_id);
    const auto& GetDatabase() const { return *(db.get()); }
    const auto& GetStatistics() const { return *(stats.get()); }
private:
    template <typename T, typename U, typename F, typename ... Args>
    U& find_or_insert_updater(std::vector<T>& entries, std::vector<U>& updaters, F&& func, Args... args) {
        assert(entries.size() == updaters.size());
        const size_t N = entries.size();
        size_t index = 0;
        for (index = 0; index < N; index++) {
            if (func(entries[index])) break;
        }
        if (index == N) {
            entries.emplace_back(std::forward<Args>(args)...);
            updaters.emplace_back(*(db.get()), index, *(stats.get()));
        }
        return updaters[index];
    }

    template <typename T, typename U, typename F>
    U* find_updater(std::vector<T>& entries, std::vector<U>& updaters, F&& func) {
        assert(entries.size() == updaters.size());
        const size_t N = entries.size();
        size_t index = 0;
        for (index = 0; index < N; index++) {
            if (func(entries[index])) break;
        }
        if (index == N) {
            return nullptr;
        }
        return &updaters[index];
    }
};