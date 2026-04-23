#pragma once

#include <inttypes.h>
#include "driver/dac_continuous.h"

// Setup function, needs to be called first
void setup_audio_player(void);

bool audio_playback_finished(void);

void play_dialing();
void play_ringing();
void play_busy();
void play_reply();
void play_silence();