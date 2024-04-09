#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "utility/span.h"
#include "./dab_database.h"
#include "./dab_database_entities.h"
#include "./dab_database_types.h"

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
    size_t m_total_conflicts = 0;
    size_t m_total_updates = 0;
    bool m_is_complete = false;
    T m_dirty_field = T(0);
private:
    DatabaseUpdaterGlobalStatistics& m_stats;
public:
    DatabaseEntityUpdater(DatabaseUpdaterGlobalStatistics& stats): m_stats(stats) {}
    virtual ~DatabaseEntityUpdater() {}
    virtual bool IsComplete() = 0;
    void OnCreate() {
        m_stats.nb_total++;
        m_stats.nb_pending++; 
        OnComplete();
    }
    void OnConflict() {
        m_total_conflicts++;
        m_stats.nb_conflicts++;
    }
    void OnComplete() {
        const bool new_is_complete = IsComplete();
        if (m_is_complete == new_is_complete) return;
        m_is_complete = new_is_complete;
        if (new_is_complete) {
            m_stats.nb_completed++;
            m_stats.nb_pending--;
        } else {
            m_stats.nb_completed--;
            m_stats.nb_pending++;
        }
    }
    void OnUpdate() {
        m_total_updates++;
        m_stats.nb_updates++;
    }
    UpdateResult UpdateField(std::string& dst, std::string_view src, T dirty_flag, bool ignore_conflict=false) {
        if (m_dirty_field & dirty_flag) {
            if (src.compare(dst) == 0) {
                return UpdateResult::NO_CHANGE;
            } else if (!ignore_conflict) {
                OnConflict();
                return UpdateResult::CONFLICT;
            }
        }
        m_dirty_field |= dirty_flag;
        dst = src;
        OnComplete();
        OnUpdate();
        return UpdateResult::SUCCESS;
    }
    template <typename U>
    UpdateResult UpdateField(U& dst, U src, T dirty_flag, bool ignore_conflict=false) {
        if (m_dirty_field & dirty_flag) {
            if (dst == src) {
                return UpdateResult::NO_CHANGE;
            } else if (!ignore_conflict) {
                OnConflict();
                return UpdateResult::CONFLICT;
            }
        }
        m_dirty_field |= dirty_flag;
        dst = src;
        OnComplete();
        OnUpdate();
        return UpdateResult::SUCCESS;
    }
};

class EnsembleUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& m_db;
public:
    explicit EnsembleUpdater(DAB_Database& db, DatabaseUpdaterGlobalStatistics& stats)
        : DatabaseEntityUpdater<uint8_t>(stats), m_db(db) { OnCreate(); } 
    UpdateResult SetReference(const ensemble_id_t reference);
    UpdateResult SetCountryID(const country_id_t country_id);
    UpdateResult SetExtendedCountryCode(const extended_country_id_t extended_country_code);
    UpdateResult SetLabel(tcb::span<const uint8_t> buf);
    UpdateResult SetNumberServices(const uint8_t nb_services);
    UpdateResult SetReconfigurationCount(const uint16_t reconfiguration_count);
    UpdateResult SetLocalTimeOffset(const int8_t local_time_offset);
    UpdateResult SetInternationalTableID(const uint8_t international_table_id) ;
    auto& GetData() { return m_db.ensemble; }
private:
    bool IsComplete() override;
};

class ServiceUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& m_db;
    const size_t m_index;
public:
    explicit ServiceUpdater(DAB_Database& db, size_t index, DatabaseUpdaterGlobalStatistics& stats)
        : DatabaseEntityUpdater<uint8_t>(stats), m_db(db), m_index(index) { OnCreate(); }
    UpdateResult SetCountryID(const country_id_t country_id);
    UpdateResult SetExtendedCountryCode(const extended_country_id_t extended_country_code);
    UpdateResult SetLabel(tcb::span<const uint8_t> buf);
    UpdateResult SetProgrammeType(const programme_id_t programme_type);
    UpdateResult SetLanguage(const language_id_t language);
    UpdateResult SetClosedCaption(const closed_caption_id_t closed_caption);
    auto& GetData() { return m_db.services[m_index]; }
private:
    bool IsComplete() override;
};

class ServiceComponentUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& m_db;
    const size_t m_index;
public:
    explicit ServiceComponentUpdater(DAB_Database& db, size_t index, DatabaseUpdaterGlobalStatistics& stats)
        : DatabaseEntityUpdater<uint8_t>(stats), m_db(db), m_index(index) { OnCreate(); }
    UpdateResult SetLabel(tcb::span<const uint8_t> buf);
    UpdateResult SetTransportMode(const TransportMode transport_mode);
    UpdateResult SetAudioServiceType(const AudioServiceType audio_service_type);
    UpdateResult SetDataServiceType(const DataServiceType data_service_type);
    UpdateResult SetSubchannel(const subchannel_id_t subchannel_id);
    UpdateResult SetGlobalID(const service_component_global_id_t global_id);
    uint32_t GetServiceReference();
    auto& GetData() { return m_db.service_components[m_index]; }
private:
    bool IsComplete() override;
};

class SubchannelUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& m_db;
    const size_t m_index;
public:
    explicit SubchannelUpdater(DAB_Database& db, size_t index, DatabaseUpdaterGlobalStatistics& stats)
        : DatabaseEntityUpdater<uint8_t>(stats), m_db(db), m_index(index) { OnCreate(); }
    UpdateResult SetStartAddress(const subchannel_addr_t start_address);
    UpdateResult SetLength(const subchannel_size_t length);
    UpdateResult SetIsUEP(const bool is_uep);
    UpdateResult SetUEPProtIndex(const uep_protection_index_t uep_prot_index);
    UpdateResult SetEEPProtLevel(const eep_protection_level_t eep_prot_level);
    UpdateResult SetEEPType(const EEP_Type eep_type);
    UpdateResult SetFECScheme(const FEC_Scheme fec_scheme);
    auto& GetData() { return m_db.subchannels[m_index]; }
private:
    bool IsComplete() override;
};

class LinkServiceUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& m_db;
    const size_t m_index;
public:
    explicit LinkServiceUpdater(DAB_Database& db, size_t index, DatabaseUpdaterGlobalStatistics& stats)
        : DatabaseEntityUpdater<uint8_t>(stats), m_db(db), m_index(index) { OnCreate(); }
    UpdateResult SetIsActiveLink(const bool is_active_link);
    UpdateResult SetIsHardLink(const bool is_hard_link);
    UpdateResult SetIsInternational(const bool is_international);
    UpdateResult SetServiceReference(const service_id_t service_reference);
    service_id_t GetServiceReference();
    auto& GetData() { return m_db.link_services[m_index]; }
private:
    bool IsComplete() override;
};

class FM_ServiceUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& m_db;
    const size_t m_index;
public:
    explicit FM_ServiceUpdater(DAB_Database& db, size_t index, DatabaseUpdaterGlobalStatistics& stats)
        : DatabaseEntityUpdater<uint8_t>(stats), m_db(db), m_index(index) { OnCreate(); }
    UpdateResult SetLinkageSetNumber(const lsn_t linkage_set_number); 
    UpdateResult SetIsTimeCompensated(const bool is_time_compensated);
    UpdateResult AddFrequency(const freq_t frequency);
    auto& GetData() { return m_db.fm_services[m_index]; }
private:
    bool IsComplete() override;
};

class DRM_ServiceUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& m_db;
    const size_t m_index;
public:
    explicit DRM_ServiceUpdater(DAB_Database& db, size_t index, DatabaseUpdaterGlobalStatistics& stats)
        : DatabaseEntityUpdater<uint8_t>(stats), m_db(db), m_index(index) { OnCreate(); }
    UpdateResult SetLinkageSetNumber(const lsn_t linkage_set_number); 
    UpdateResult SetIsTimeCompensated(const bool is_time_compensated);
    UpdateResult AddFrequency(const freq_t frequency);
    auto& GetData() { return m_db.drm_services[m_index]; }
private:
    bool IsComplete() override;
};

class AMSS_ServiceUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& m_db;
    const size_t m_index;
public:
    explicit AMSS_ServiceUpdater(DAB_Database& db, size_t index, DatabaseUpdaterGlobalStatistics& stats)
        : DatabaseEntityUpdater<uint8_t>(stats), m_db(db), m_index(index) { OnCreate(); }
    UpdateResult SetIsTimeCompensated(const bool is_time_compensated);
    UpdateResult AddFrequency(const freq_t frequency);
    auto& GetData() { return m_db.amss_services[m_index]; }
private:
    bool IsComplete() override;
};

class OtherEnsembleUpdater: private DatabaseEntityUpdater<uint8_t>
{
private:
    DAB_Database& m_db;
    const size_t m_index;
public:
    explicit OtherEnsembleUpdater(DAB_Database& db, size_t index, DatabaseUpdaterGlobalStatistics& stats)
        : DatabaseEntityUpdater<uint8_t>(stats), m_db(db), m_index(index) { OnCreate(); }
    UpdateResult SetCountryID(const country_id_t country_id);
    UpdateResult SetIsContinuousOutput(const bool is_continuous_output);
    UpdateResult SetIsGeographicallyAdjacent(const bool is_geographically_adjacent);
    UpdateResult SetIsTransmissionModeI(const bool is_transmission_mode_I);
    UpdateResult SetFrequency(const freq_t frequency);
    auto& GetData() { return m_db.other_ensembles[m_index]; }
private:
    bool IsComplete() override;
};

class DAB_Database_Updater
{
private:
    std::unique_ptr<DatabaseUpdaterGlobalStatistics> m_stats;
    std::unique_ptr<DAB_Database> m_db;
    std::unique_ptr<EnsembleUpdater> m_ensemble_updater;
    std::vector<ServiceUpdater> m_service_updaters;
    std::vector<ServiceComponentUpdater> m_service_component_updaters;
    std::vector<SubchannelUpdater> m_subchannel_updaters;
    std::vector<LinkServiceUpdater> m_link_service_updaters;
    std::vector<FM_ServiceUpdater> m_fm_service_updaters;
    std::vector<DRM_ServiceUpdater> m_drm_service_updaters;
    std::vector<AMSS_ServiceUpdater> m_amss_service_updaters;
    std::vector<OtherEnsembleUpdater> m_other_ensemble_updaters;
public:
    explicit DAB_Database_Updater();
    EnsembleUpdater& GetEnsembleUpdater() { return *(m_ensemble_updater.get()); }
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
    const auto& GetDatabase() const { return *(m_db.get()); }
    const auto& GetStatistics() const { return *(m_stats.get()); }
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
            updaters.emplace_back(*(m_db.get()), index, *(m_stats.get()));
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