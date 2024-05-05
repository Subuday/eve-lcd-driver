#pragma once

#include <inttypes.h>
#include <unistd.h>

// Initialized in spi.cpp along with the rest of the BCM2835 peripheral:
// extern volatile uint64_t *systemTimerRegister;
// #define tick() (*systemTimerRegister)

#define TIMER_TYPE uint64_t
extern volatile uint64_t *systemTimerRegister;
#define tick() (*systemTimerRegister)

