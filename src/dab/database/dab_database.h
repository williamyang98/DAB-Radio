#pragma once

#include "./dab_database_entities.h"
#include "./dab_database_types.h"

#include <vector>
#include <deque>
#include <map>
#include <set>

// The combined database
class DAB_Database 
{
public:
    enum LinkResult { NO_CHANGE, ADDED, CONFLICT };
public:
    Ensemble ensemble;
    // contiguous storage arenas so we don't reallocate if there is a resize
    std::deque<Service> services;
    std::deque<ServiceComponent> service_components;
    std::deque<Subchannel> subchannels;
    std::deque<LinkService> link_services;
    std::deque<FM_Service> fm_services;
    std::deque<DRM_Service> drm_services;
    std::deque<AMSS_Service> amss_services;
    std::deque<OtherEnsemble> other_ensembles;
private:
    // NOTE: (automatic) in the lookup means it is automatically created when object is instantiated
    // NOTE: (manual) means that it needs to be notified when a property is changed
    // NOTE: We use lookup tables to reduce the amount of time spent searching for linking components
    // Perhaps it would be a better idea to use a third party database that can automatically
    // creating an index using foreign keys (sqlite, etc)

    // (automatic) lookup tables that have an id to object relationship
    std::map<service_id_t, Service*> lut_services;                
    std::map<subchannel_id_t, Subchannel*> lut_subchannels;           
    std::map<lsn_t, LinkService*> lut_link_services;
    std::map<fm_id_t, FM_Service*> lut_fm_services;
    std::map<drm_id_t, DRM_Service*> lut_drm_services;
    std::map<amss_id_t, AMSS_Service*> lut_amss_services;
    std::map<ensemble_id_t, OtherEnsemble*> lut_other_ensembles;

    // (manual) needs to be created when a service component is given a global id
    std::map<service_component_global_id_t, ServiceComponent*> lut_global_service_components;

    // (automatic) nested lookup using a service reference and component id to service component
    std::map<service_id_t, std::map<service_component_id_t, ServiceComponent*>> lut_service_components_table;
    // (automatic) lookup using service reference to list of service components
    std::map<service_id_t, std::set<ServiceComponent*>> lut_service_components_list;

    // (manual) lookup using subchannel id to service component
    // Needs to be manually added when the service component is given a subchannel id
    std::map<subchannel_id_t, ServiceComponent*> lut_subchannel_to_service_component;           

    // (manual) lookup using LSN to list of linked services
    // Needs to be manually added when the linked service is given a LSN
    std::map<lsn_t, std::set<FM_Service*>> lut_link_fm_services;
    std::map<lsn_t, std::set<DRM_Service*>> lut_link_drm_services;

    // (manual) lookup the LSN's connected to a service
    std::map<service_id_t, std::set<LinkService*>> lut_service_lsn;
public:
    // Already created
    Ensemble* GetEnsemble() { return &ensemble; }
    // Calls that create an object if it doesn't exist
    Service* GetService(const service_id_t service_reference, const bool is_create=false);
    ServiceComponent* GetServiceComponent(
        const service_id_t service_reference, 
        const service_component_id_t component_id,
        const bool is_create=false);
    Subchannel* GetSubchannel(const subchannel_id_t subchannel_id, const bool is_create=false);
    LinkService* GetLinkService(const lsn_t linkage_set_number, const bool is_create=false);
    FM_Service* Get_FM_Service(const fm_id_t rds_pi_code, const bool is_create=false);
    DRM_Service* Get_DRM_Service(const drm_id_t drm_id, const bool is_create=false);
    AMSS_Service* Get_AMSS_Service(const amss_id_t amss_id, const bool is_create=false);
    OtherEnsemble* GetOtherEnsemble(const ensemble_id_t ensemble_reference, const bool is_create=false);
    // Calls that don't create an object and can return NULL
    ServiceComponent* GetServiceComponent_Global(const service_component_global_id_t global_id);
    ServiceComponent* GetServiceComponent_Subchannel(const subchannel_id_t subchannel_id);
    std::set<ServiceComponent*>* GetServiceComponents(const service_id_t service_reference);
    std::set<LinkService*>* GetServiceLSNs(const service_id_t service_reference);
    std::set<FM_Service*>* Get_LSN_FM_Services(const lsn_t lsn);
    std::set<DRM_Service*>* Get_LSN_DRM_Services(const lsn_t lsn);

    // Create a linkage between database entities in realtime as they get added
    // This is like a foreign key inside a proper database
    LinkResult CreateLink_ServiceComponent_Global(
        const service_id_t service_reference, 
        const service_component_id_t component_id, 
        const service_component_global_id_t global_id);
    LinkResult CreateLink_ServiceComponent_Subchannel(
        const service_id_t service_reference, 
        const service_component_id_t component_id, 
        const subchannel_id_t subchannel_id);
    LinkResult CreateLink_FM_Service(
        const lsn_t linkage_set_number, const fm_id_t rds_pi_code);
    LinkResult CreateLink_DRM_Service(
        const lsn_t linkage_set_number, const drm_id_t drm_id);
    LinkResult CreateLink_Service_LSN(
        const service_id_t service_reference,
        const lsn_t linkage_set_number);

    // Recreate the lookup tables from the existing entities
    bool RegenerateLookups();
    void ClearAll();
};