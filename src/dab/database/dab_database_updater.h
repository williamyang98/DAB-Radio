#pragma once

#include <stdint.h>

#include "dab_database.h"

#include <map>
#include <vector>

// The topology of the updater classes is as follows
// Parent:  Stores the database (which stores its entities)
//          Stores lookup tables that associates database entities to their updater (child)
//          Keeps track of the global status of the database (how many entities are completed)
// Child:   Stores a reference to their database entity
//          Keeps track of which fields were updated
//          Determines if there are enough provided fields for an entity to be complete
//          Detects if there are conflicting field values
//          Reports to the parent when it is complete
// Here is the relationship between the updater parent/child, and the database entity:
//          Parent updater <-- one to many --> Child updater <-- one to one --> Database entity
class UpdaterParent 
{
public:
    virtual DAB_Database* GetDatabase() = 0;
    virtual void SignalComplete() = 0;
    virtual void SignalPending() = 0;
    virtual void SignalConflict() = 0;
    virtual void SignalUpdate() = 0;
};

// Think of this like a web form
// Acts as a setter wrapper around the database entity (which it stores as a pointer)
//      Keeps track of which fields were modified/set
//      This is done so the fields can be tracked
// If a field is updated more than once, check if the new value conflicts with old value
// If a set of declare fields isn't valid report a conflict
//      I.e. Audio service requires different fields to data service
class UpdaterChild 
{
protected:
    UpdaterParent* parent = NULL;
    int total_conflicts = 0;
    int total_updates = 0;
    bool is_complete = false;
public:
    // NOTE: This must be called upon creation of the updater
    void BindParent(UpdaterParent* _parent) { 
        parent = _parent; 
        parent->SignalPending();
        CheckIsComplete();
    }
    int GetTotalConflicts() { return total_conflicts; }
    // override with completion requirements
    virtual bool IsComplete() = 0;
protected:
    // Update internal state and report state to parent
    void OnConflict() {
        total_conflicts++;
        parent->SignalConflict();
    }
    void CheckIsComplete() {
        if (is_complete) return;
        if (IsComplete()) {
            is_complete = true;
            parent->SignalComplete();
        }
    }
    void OnUpdate() {
        total_updates++;
        parent->SignalUpdate();
    }
};

// When we set a field, return what the database did with it
enum UpdateResult { SUCCESS, CONFLICT, NO_CHANGE };

// All the updater to database entity associations
// NOTE: Most or all of the updaters keep track of changed fields with a dirty field
class EnsembleUpdater: public UpdaterChild 
{
private:
    Ensemble* data;
    uint8_t dirty_field = 0x00;
public:
    EnsembleUpdater(Ensemble* _data): data(_data) {}
    UpdateResult SetReference(const ensemble_id_t reference);
    UpdateResult SetCountryID(const country_id_t country_id);
    UpdateResult SetExtendedCountryCode(const extended_country_id_t extended_country_code);
    UpdateResult SetLabel(const uint8_t* buf, const int N);
    UpdateResult SetNumberServices(const uint8_t nb_services);
    UpdateResult SetReconfigurationCount(const uint16_t reconfiguration_count);
    UpdateResult SetLocalTimeOffset(const int local_time_offset);
    UpdateResult SetInternationalTableID(const uint8_t international_table_id) ;
    virtual bool IsComplete();
    Ensemble* GetData() { return data; }
};

class ServiceUpdater: public UpdaterChild 
{
private:
    Service* data;
    uint8_t dirty_field = 0x00;
public:
    ServiceUpdater(Service* _data): data(_data) {}
    UpdateResult SetCountryID(const country_id_t country_id);
    UpdateResult SetExtendedCountryCode(const extended_country_id_t extended_country_code);
    UpdateResult SetLabel(const uint8_t* buf, const int N);
    UpdateResult SetProgrammeType(const programme_id_t programme_type);
    UpdateResult SetLanguage(const language_id_t language);
    UpdateResult SetClosedCaption(const closed_caption_id_t closed_caption);
    virtual bool IsComplete();
    Service* GetData() { return data; }
};

class ServiceComponentUpdater: public UpdaterChild 
{
private:   
    ServiceComponent* data;
    uint8_t dirty_field = 0x00;
public:
    ServiceComponentUpdater(ServiceComponent* _data): data(_data) {}
    UpdateResult SetLabel(const uint8_t* buf, const int N);
    UpdateResult SetTransportMode(const TransportMode transport_mode);
    UpdateResult SetAudioServiceType(const AudioServiceType audio_service_type);
    UpdateResult SetDataServiceType(const DataServiceType data_service_type);
    UpdateResult SetSubchannel(const subchannel_id_t subchannel_id);
    UpdateResult SetGlobalID(const service_component_global_id_t global_id);
    uint32_t GetServiceReference();
    virtual bool IsComplete();
    ServiceComponent* GetData() { return data; }
};

class SubchannelUpdater: public UpdaterChild 
{
private:
    Subchannel* data;
    uint8_t dirty_field = 0x00;
public:
    SubchannelUpdater(Subchannel* _data): data(_data) {}
    UpdateResult SetStartAddress(const subchannel_addr_t start_address);
    UpdateResult SetLength(const subchannel_size_t length);
    UpdateResult SetIsUEP(const bool is_uep);
    UpdateResult SetUEPProtIndex(const uep_protection_index_t uep_prot_index);
    UpdateResult SetEEPProtLevel(const eep_protection_level_t eep_prot_level);
    UpdateResult SetEEPType(const EEP_Type eep_type);
    UpdateResult SetFECScheme(const fec_scheme_t fec_scheme);
    virtual bool IsComplete();
    Subchannel* GetData() { return data; }
};

class LinkServiceUpdater: public UpdaterChild 
{
private:
    LinkService* data;
    uint8_t dirty_field = 0x00;
public:
    LinkServiceUpdater(LinkService* _data): data(_data) {}
    UpdateResult SetIsActiveLink(const bool is_active_link);
    UpdateResult SetIsHardLink(const bool is_hard_link);
    UpdateResult SetIsInternational(const bool is_international);
    UpdateResult SetServiceReference(const service_id_t service_reference);
    service_id_t GetServiceReference();
    virtual bool IsComplete();
    LinkService* GetData() { return data; }
};

class FM_ServiceUpdater: public UpdaterChild 
{
private:
    FM_Service* data;
    uint8_t dirty_field = 0x00;
public:
    FM_ServiceUpdater(FM_Service* _data): data(_data) {}
    UpdateResult SetLinkageSetNumber(const lsn_t linkage_set_number); 
    UpdateResult SetIsTimeCompensated(const bool is_time_compensated);
    UpdateResult AddFrequency(const freq_t frequency);
    virtual bool IsComplete();
    FM_Service* GetData() { return data; }
};

class DRM_ServiceUpdater: public UpdaterChild 
{
private:
    DRM_Service* data;
    uint8_t dirty_field = 0x00;
public:
    DRM_ServiceUpdater(DRM_Service* _data): data(_data) {}
    UpdateResult SetLinkageSetNumber(const lsn_t linkage_set_number); 
    UpdateResult SetIsTimeCompensated(const bool is_time_compensated);
    UpdateResult AddFrequency(const freq_t frequency);
    virtual bool IsComplete();
    DRM_Service* GetData() { return data; }
};

class AMSS_ServiceUpdater: public UpdaterChild 
{
private:
    AMSS_Service* data;
    uint8_t dirty_field = 0x00;
public:
    AMSS_ServiceUpdater(AMSS_Service* _data): data(_data) {}
    UpdateResult SetIsTimeCompensated(const bool is_time_compensated);
    UpdateResult AddFrequency(const freq_t frequency);
    virtual bool IsComplete();
    AMSS_Service* GetData() { return data; }
};

class OtherEnsembleUpdater: public UpdaterChild 
{
private:
    OtherEnsemble* data;
    uint8_t dirty_field = 0x00;
public:
    OtherEnsembleUpdater(OtherEnsemble* _data): data(_data) {}
    UpdateResult SetCountryID(const country_id_t country_id);
    UpdateResult SetIsContinuousOutput(const bool is_continuous_output);
    UpdateResult SetIsGeographicallyAdjacent(const bool is_geographically_adjacent);
    UpdateResult SetIsTransmissionModeI(const bool is_transmission_mode_I);
    UpdateResult SetFrequency(const freq_t frequency);
    virtual bool IsComplete();
    OtherEnsemble* GetData() { return data; }
};

class DAB_Database_Updater: public UpdaterParent
{
public:
    struct Statistics {
        int nb_total = 0;
        int nb_pending = 0;
        int nb_completed = 0;
        int nb_conflicts = 0;
        int nb_updates = 0;
        bool operator==(const Statistics& other) {
            return 
                (nb_total == other.nb_total) &&
                (nb_pending == other.nb_pending) &&
                (nb_completed == other.nb_completed) &&
                (nb_conflicts == other.nb_conflicts) &&
                (nb_updates == other.nb_updates);
        }
        bool operator!=(const Statistics& other) {
            return 
                (nb_total != other.nb_total) ||
                (nb_pending != other.nb_pending) || 
                (nb_completed != other.nb_completed) || 
                (nb_conflicts != other.nb_conflicts) || 
                (nb_updates != other.nb_updates);
        }
    };
private:
    // keep track of update statuses
    Statistics stats;
    // db is not owned by updater 
    DAB_Database* db;  
    // keep reference to all updaters for global conflict/completion check
    std::vector<UpdaterChild*> all_updaters;
    EnsembleUpdater ensemble_updater;
    // to get the ids for each updater, we get the ids from the database
    std::map<service_id_t, ServiceUpdater> service_updaters;
    std::map<std::pair<service_id_t, service_component_id_t>, ServiceComponentUpdater> service_component_updaters;
    std::map<subchannel_id_t, SubchannelUpdater> subchannel_updaters;
    std::map<lsn_t, LinkServiceUpdater> link_service_updaters;
    std::map<fm_id_t, FM_ServiceUpdater> fm_service_updaters;
    std::map<drm_id_t, DRM_ServiceUpdater> drm_service_updaters;
    std::map<amss_id_t, AMSS_ServiceUpdater> amss_service_updaters;
    std::map<ensemble_id_t, OtherEnsembleUpdater> other_ensemble_updaters;
public:
    DAB_Database_Updater(DAB_Database* _db);
    virtual DAB_Database* GetDatabase() { return db; }
    virtual void SignalComplete();
    virtual void SignalPending();
    virtual void SignalConflict();
    virtual void SignalUpdate();
    EnsembleUpdater* GetEnsembleUpdater();
    // Create the instance
    ServiceUpdater* GetServiceUpdater(const service_id_t service_ref);
    ServiceComponentUpdater* GetServiceComponentUpdater_Service(const service_id_t service_ref, const service_component_id_t component_id);
    SubchannelUpdater* GetSubchannelUpdater(const subchannel_id_t subchannel_id);
    LinkServiceUpdater* GetLinkServiceUpdater(const lsn_t link_service_number);
    FM_ServiceUpdater* GetFMServiceUpdater(const fm_id_t RDS_PI_code);
    DRM_ServiceUpdater* GetDRMServiceUpdater(const drm_id_t drm_code);
    AMSS_ServiceUpdater* GetAMSS_ServiceUpdater(const amss_id_t amss_code);
    OtherEnsembleUpdater* GetOtherEnsemble(const ensemble_id_t ensemble_reference);
    // Returns NULL if it couldn't find the instance
    ServiceComponentUpdater* GetServiceComponentUpdater_GlobalID(const service_component_global_id_t global_id);
    ServiceComponentUpdater* GetServiceComponentUpdater_Subchannel(const subchannel_id_t subchannel_id);
    // TODO: remove this is for debugging
    inline std::vector<UpdaterChild*>& GetUpdaters() { return all_updaters; }
    // Create a copy of the database with complete entities
    void ExtractCompletedDatabase(DAB_Database& dest_db);
    // Get status of database
    inline Statistics GetStatistics() const { return stats; }
};