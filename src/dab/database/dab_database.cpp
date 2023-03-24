#include "./dab_database.h"

// Calls that create an object if it doesn't exist
Service* DAB_Database::GetService(
    const service_id_t service_reference, const bool is_create) {
    auto res = lut_services.find(service_reference);
    if (res == lut_services.end()) {
        if (!is_create) {
            return NULL;
        }

        auto& service = services.emplace_back(service_reference);
        res = lut_services.insert({service_reference, &service}).first;
        // initialise their containers
        lut_service_components_list[service_reference];
        lut_service_components_table[service_reference];
    }
    
    return res->second;
}

ServiceComponent* DAB_Database::GetServiceComponent(
    const service_id_t service_reference, 
    const service_component_id_t component_id,
    const bool is_create)
{
    auto* service = GetService(service_reference, is_create);
    if (service == NULL) {
        return NULL;
    }

    auto& components_table = lut_service_components_table[service_reference];
    auto& components_list = lut_service_components_list[service_reference];

    auto res = components_table.find(component_id);
    if (res == components_table.end()) {
        if (!is_create) {
            return NULL;
        }
        auto& component = service_components.emplace_back(
            service_reference, component_id);
        res = components_table.insert({component_id, &component}).first;
        components_list.insert(&component);
    }
    return res->second;
}

Subchannel* DAB_Database::GetSubchannel(
    const subchannel_id_t subchannel_id,
    const bool is_create) 
{
    auto res = lut_subchannels.find(subchannel_id);
    if (res == lut_subchannels.end()) {
        if (!is_create) {
            return NULL;
        }
        auto& subchannel = subchannels.emplace_back(subchannel_id);
        res = lut_subchannels.insert({subchannel_id, &subchannel}).first;
    }
    return res->second;
}

LinkService* DAB_Database::GetLinkService(
    const lsn_t linkage_set_number, const bool is_create) 
{
    auto res = lut_link_services.find(linkage_set_number);
    if (res == lut_link_services.end()) {
        if (!is_create) {
            return NULL;
        }
        auto& link_service = link_services.emplace_back(linkage_set_number);
        res = lut_link_services.insert({linkage_set_number, &link_service}).first;
        // initialise their containers
        lut_link_fm_services[linkage_set_number];
        lut_link_drm_services[linkage_set_number];
    }
    return res->second;
}

FM_Service* DAB_Database::Get_FM_Service(
    const fm_id_t rds_pi_code, const bool is_create) 
{
    auto res = lut_fm_services.find(rds_pi_code);
    if (res == lut_fm_services.end()) {
        if (!is_create) {
            return NULL;
        }
        auto& fm = fm_services.emplace_back(rds_pi_code);
        res = lut_fm_services.insert({rds_pi_code, &fm}).first;
    }
    return res->second;
}

DRM_Service* DAB_Database::Get_DRM_Service(
    const drm_id_t drm_id, const bool is_create) 
{
    auto res = lut_drm_services.find(drm_id);
    if (res == lut_drm_services.end()) {
        if (!is_create) {
            return NULL;
        }
        auto& drm = drm_services.emplace_back(drm_id);
        res = lut_drm_services.insert({drm_id, &drm}).first;
    }
    return res->second;
}

AMSS_Service* DAB_Database::Get_AMSS_Service(
    const amss_id_t amss_id, const bool is_create) 
{
    auto res = lut_amss_services.find(amss_id);
    if (res == lut_amss_services.end()) {
        if (!is_create) {
            return NULL;
        }
        auto& amss = amss_services.emplace_back(amss_id);
        res = lut_amss_services.insert({amss_id, &amss}).first;
    }
    return res->second;
}

OtherEnsemble* DAB_Database::GetOtherEnsemble(
    const ensemble_id_t ensemble_reference, const bool is_create) 
{
    auto res = lut_other_ensembles.find(ensemble_reference);
    if (res == lut_other_ensembles.end()) {
        if (!is_create) {
            return NULL;
        }
        auto& oe = other_ensembles.emplace_back(ensemble_reference);
        res = lut_other_ensembles.insert({ensemble_reference, &oe}).first;
    }
    return res->second;
}

// Calls that don't create an object and can return NULL
ServiceComponent* DAB_Database::GetServiceComponent_Global(const service_component_global_id_t global_id) {
    auto res = lut_global_service_components.find(global_id);
    if (res == lut_global_service_components.end()) {
        return NULL;
    }
    return res->second;
}

ServiceComponent* DAB_Database::GetServiceComponent_Subchannel(const subchannel_id_t subchannel_id) {
    auto res = lut_subchannel_to_service_component.find(subchannel_id);
    if (res == lut_subchannel_to_service_component.end()) {
        return NULL;
    }
    return res->second;
}

std::set<ServiceComponent*>* DAB_Database::GetServiceComponents(const service_id_t service_reference) {
    auto res = lut_service_components_list.find(service_reference);
    if (res == lut_service_components_list.end()) {
        return NULL;
    }
    return &(res->second);
}

std::set<LinkService*>* DAB_Database::GetServiceLSNs(const service_id_t service_reference) {
    auto res = lut_service_lsn.find(service_reference);
    if (res == lut_service_lsn.end()) {
        return NULL;
    }
    return &(res->second);
}

std::set<FM_Service*>* DAB_Database::Get_LSN_FM_Services(const lsn_t lsn) {
    auto res = lut_link_fm_services.find(lsn);
    if (res == lut_link_fm_services.end()) {
        return NULL;
    }
    return &(res->second);
}

std::set<DRM_Service*>* DAB_Database::Get_LSN_DRM_Services(const lsn_t lsn) {
    auto res = lut_link_drm_services.find(lsn);
    if (res == lut_link_drm_services.end()) {
        return NULL;
    }
    return &(res->second);
}

// Create a linkage between database entities
// This is like a foreign key inside a proper database
DAB_Database::LinkResult DAB_Database::CreateLink_ServiceComponent_Global(
    const service_id_t service_reference, 
    const service_component_id_t component_id, 
    const service_component_global_id_t global_id)
{
    auto res = lut_global_service_components.find(global_id);
    auto* component = GetServiceComponent(service_reference, component_id, true);
    if (res != lut_global_service_components.end()) {
        const bool is_match = (component == res->second);
        return is_match ? NO_CHANGE : CONFLICT;
    }

    lut_global_service_components.insert({global_id, component});
    return ADDED;
}

DAB_Database::LinkResult DAB_Database::CreateLink_ServiceComponent_Subchannel(
    const service_id_t service_reference, 
    const service_component_id_t component_id, 
    const subchannel_id_t subchannel_id) 
{
    GetSubchannel(subchannel_id, true);
    auto* component = GetServiceComponent(service_reference, component_id, true);
    auto res = lut_subchannel_to_service_component.find(subchannel_id);
    if (res != lut_subchannel_to_service_component.end()) {
        const bool is_match = (component == res->second);
        return is_match ? NO_CHANGE : CONFLICT;
    }

    lut_subchannel_to_service_component.insert({subchannel_id, component});
    return ADDED;
}

DAB_Database::LinkResult DAB_Database::CreateLink_FM_Service(
    const lsn_t linkage_set_number, const fm_id_t rds_pi_code)
{
    GetLinkService(linkage_set_number, true);
    auto* fm = Get_FM_Service(rds_pi_code, true);
    auto& fm_list = lut_link_fm_services[linkage_set_number];

    auto res = fm_list.find(fm);
    if (res != fm_list.end()) {
        return NO_CHANGE;
    }

    fm_list.insert(fm);
    return ADDED;
}

DAB_Database::LinkResult DAB_Database::CreateLink_DRM_Service(
    const lsn_t linkage_set_number, const drm_id_t drm_id)
{
    GetLinkService(linkage_set_number, true);
    auto* drm = Get_DRM_Service(drm_id, true);
    auto& drm_list = lut_link_drm_services[linkage_set_number];

    auto res = drm_list.find(drm);
    if (res != drm_list.end()) {
        return NO_CHANGE;
    }

    drm_list.insert(drm);
    return ADDED;
}

DAB_Database::LinkResult DAB_Database::CreateLink_Service_LSN(
    const service_id_t service_reference,
    const lsn_t linkage_set_number)
{
    auto* link_service = GetLinkService(linkage_set_number, true);
    auto& lsn_list = lut_service_lsn[service_reference];

    auto res = lsn_list.find(link_service);
    if (res != lsn_list.end()) {
        return NO_CHANGE;
    }

    lsn_list.insert(link_service);
    return ADDED;
}

// In the event we are loading the database from memory
// We only have the serialised objects 
bool DAB_Database::RegenerateLookups() {
    // (automatic) id lookups
    lut_services.clear();
    lut_subchannels.clear();
    lut_link_services.clear();
    lut_fm_services.clear();
    lut_drm_services.clear();
    lut_amss_services.clear();
    lut_other_ensembles.clear();

    // (manual) when component given global id
    lut_global_service_components.clear();

    // (automatic) on component create associated to service
    lut_service_components_table.clear();
    // (automatic) on component create associated to service
    lut_service_components_list.clear();

    // (manual) when component given subchannel
    lut_subchannel_to_service_component.clear();

    // (manual) when fm/drm service given linkage set number
    lut_link_fm_services.clear();
    lut_link_drm_services.clear();

    // (manual) lookup the LSN's connected to a service
    lut_service_lsn.clear();

    bool is_success = true;

    // (automatic) generate id lookup tables
    for (auto& e: services) {
        auto [_, rv] = lut_services.insert({e.reference, &e});
        is_success = is_success && rv;
    }
    for (auto& e: subchannels) {
        auto [_, rv] = lut_subchannels.insert({e.id, &e});
        is_success = is_success && rv;
    }
    for (auto& e: link_services) {
        auto [_, rv] = lut_link_services.insert({e.id, &e});
        is_success = is_success && rv;
    }
    for (auto& e: fm_services) {
        auto [_, rv] = lut_fm_services.insert({e.RDS_PI_code, &e});
        is_success = is_success && rv;
    }
    for (auto& e: drm_services) {
        auto [_, rv] = lut_drm_services.insert({e.drm_code, &e});
        is_success = is_success && rv;
    }
    for (auto& e: amss_services) {
        auto [_, rv] = lut_amss_services.insert({e.amss_code, &e});
        is_success = is_success && rv;
    }
    for (auto& e: other_ensembles) {
        auto [_, rv] = lut_other_ensembles.insert({e.reference, &e});
        is_success = is_success && rv;
    }

    // (automatic) nested lookup for service component using service reference and component id
    // lookup table of lists to components belonging to a service
    // lookup table from subchannel id to 
    for (auto& e: service_components) {
        const auto service_ref = e.service_reference;
        const auto component_id = e.component_id;

        auto& components_table = lut_service_components_table[service_ref];
        auto& components_list = lut_service_components_list[service_ref];

        auto [it0, rv0] = components_table.insert({component_id, &e});
        auto [it1, rv1] = components_list.insert(&e);
        is_success = is_success && rv0 && rv1;
    }

    // (manual) service component to global id
    for (auto& e: service_components) {
        const auto id = e.global_id;
        if (id == 0) continue;
        auto [_, rv] = lut_global_service_components.insert({id, &e});
        is_success = is_success && rv;
    }

    // (manual) lookup using subchannel id to service component
    for (auto& e: service_components) {
        const auto subchannel_id = e.subchannel_id;
        auto [_, rv] = lut_subchannel_to_service_component.insert({subchannel_id, &e});
        is_success = is_success && rv;
    }

    // (manual) lookup using LSN to list of linked services
    for (auto &e: fm_services) {
        const auto lsn = e.linkage_set_number;
        auto& fm_list = lut_link_fm_services[lsn];
        auto [_, rv] = fm_list.insert(&e);
        is_success = is_success && rv;
    }

    for (auto &e: drm_services) {
        const auto lsn = e.linkage_set_number;
        auto& drm_list = lut_link_drm_services[lsn];
        auto [_, rv] = drm_list.insert(&e);
        is_success = is_success && rv;
    }

    // (manual) lookup the LSN's connected to a service
    for (auto& e: link_services) {
        const auto service_ref = e.service_reference;
        auto& lsn_list = lut_service_lsn[service_ref];
        auto [_, rv] = lsn_list.insert(&e);
        is_success = is_success && rv;
    }

    return is_success;
}

void DAB_Database::ClearAll() {
    services.clear();
    service_components.clear();
    subchannels.clear();
    link_services.clear();
    fm_services.clear();
    drm_services.clear();
    amss_services.clear();
    other_ensembles.clear();
}