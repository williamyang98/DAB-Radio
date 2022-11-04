#pragma once

#include "imgui.h"

#include "dab/database/dab_database_entities.h"

class SimpleViewController
{
public:
    ImGuiTextFilter services_filter;
    void ClearSearch(void);
    service_id_t selected_service = 0;
};