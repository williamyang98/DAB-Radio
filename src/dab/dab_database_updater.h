#pragma once

#include <stdint.h>

#include "dab_database_entities.h"
#include "dab_database_types.h"

#include <map>
#include <vector>

class DAB_Database;

// Updater entities can call the parent
// 
class UpdaterParent 
{
public:
    virtual DAB_Database* GetDatabase() = 0;
    virtual void SignalComplete() = 0;
    virtual void SignalPending() = 0;
    virtual void SignalConflict() = 0;
};

// An updater basically acts like a web form
// Keeps track of which fields were modified/set
// Acts as a setter wrapper around the core data (which it stores as a pointer)
// This is done so the fields can be tracked
// If a field is updated more than once, a equality check is done
// This is to alert us if a field in the data was given two conflicting values
class UpdaterBase 
{
protected:
    UpdaterParent* parent = NULL;
    int total_conflicts = 0;
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
    // Update internal state and transmit signals to parent
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
};

enum UpdateResult { SUCCESS, CONFLICT, NO_CHANGE };

class EnsembleUpdater: public UpdaterBase 
{
private:
    Ensemble* data;
    uint8_t dirty_field = 0x00;
    int total_conflicts = 0;
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
};

class ServiceUpdater: public UpdaterBase 
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
};

class ServiceComponentUpdater: public UpdaterBase 
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
};

class SubchannelUpdater: public UpdaterBase 
{
private:
    Subchannel* data;
    uint8_t dirty_field = 0x00;
public:
    SubchannelUpdater(Subchannel* _data): data(_data) {}
    UpdateResult SetStartAddress(const subchannel_addr_t start_address);
    UpdateResult SetLength(const subchannel_size_t length);
    UpdateResult SetIsUEP(const bool is_uep);
    UpdateResult SetProtectionLevel(const protection_level_t protection_level);
    UpdateResult SetEEPType(const EEP_Type eep_type);
    UpdateResult SetFECScheme(const fec_scheme_t fec_scheme);
    virtual bool IsComplete();
};

class LinkServiceUpdater: public UpdaterBase 
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
};

class FM_ServiceUpdater: public UpdaterBase 
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
};

class DRM_ServiceUpdater: public UpdaterBase 
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
};

class AMSS_ServiceUpdater: public UpdaterBase 
{
private:
    AMSS_Service* data;
    uint8_t dirty_field = 0x00;
public:
    AMSS_ServiceUpdater(AMSS_Service* _data): data(_data) {}
    UpdateResult SetIsTimeCompensated(const bool is_time_compensated);
    UpdateResult AddFrequency(const freq_t frequency);
    virtual bool IsComplete();
};

class OtherEnsembleUpdater: public UpdaterBase 
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
};

class DAB_Database_Updater: public UpdaterParent
{
private:
    // keep track of update statuses
    struct {
        int nb_total = 0;
        int nb_pending = 0;
        int nb_completed = 0;
        int nb_conflicts = 0;
    } stats;
    // db is not owned by updater 
    DAB_Database* db;  
    // keep reference to all updaters for global conflict/completion check
    std::vector<UpdaterBase*> all_updaters;
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
    inline std::vector<UpdaterBase*>& GetUpdaters() { return all_updaters; }
};