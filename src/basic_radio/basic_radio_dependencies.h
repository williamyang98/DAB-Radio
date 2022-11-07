#pragma once

#include "audio/pcm_player.h"

// Callbacks for create OS dependent classes
class Basic_Radio_Dependencies
{
public:
    virtual PCM_Player* Create_PCM_Player(void) = 0;
};