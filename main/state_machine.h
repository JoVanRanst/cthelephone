#pragma once

#include <inttypes.h>
#include <stdbool.h>

// Led control function
void set_RGB_color(bool red, bool green, bool blue);

// State functions
void state_machine_run();