#pragma once

#include <vector>
#include "./dab_database_entities.h"

struct DAB_Database 
{
public:
    Ensemble ensemble;
    std::vector<Service> services;
    std::vector<ServiceComponent> service_components;
    std::vector<Subchannel> subchannels;
    std::vector<LinkService> link_services;
    std::vector<FM_Service> fm_services;
    std::vector<DRM_Service> drm_services;
    std::vector<AMSS_Service> amss_services;
    std::vector<OtherEnsemble> other_ensembles;

    void reset() {
        ensemble = Ensemble{};
        services.clear();
        service_components.clear();
        subchannels.clear();
        link_services.clear();
        fm_services.clear();
        drm_services.clear();
        amss_services.clear();
        other_ensembles.clear();
    }
};