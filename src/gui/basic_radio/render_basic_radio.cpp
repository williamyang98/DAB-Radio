#include "render_basic_radio.h"
#include "basic_radio/basic_radio.h"

#include "imgui.h"
#include <fmt/core.h>
#include "formatters.h"

#include "render_simple_view.h"
#include "render_common.h"

void RenderBasicRadio(BasicRadio* radio) {
    auto lock_db = std::scoped_lock(radio->GetDatabaseManager()->GetDatabaseMutex());

    RenderSubchannels(radio);
    static SimpleViewController simple_view_controller;
    RenderSimple_Root(radio, &simple_view_controller);
}
