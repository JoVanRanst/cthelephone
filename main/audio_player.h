#pragma once

#include <inttypes.h>
#include "driver/dac_continuous.h"

// Setup function, needs to be called first
void setup_audio_player(void);

bool play_dialing();
bool play_ringing();
bool play_busy();
bool play_reply();
