#pragma once

#include <inttypes.h>
#include <unistd.h>

// Initialized in spi.cpp along with the rest of the BCM2835 peripheral:
// extern volatile uint64_t *systemTimerRegister;
// #define tick() (*systemTimerRegister)

#if __aarch64__
#define TIMER_TYPE uint32_t
extern volatile uint32_t *systemTimerRegister;
#define tick() (*systemTimerRegister+((uint64_t)(*(systemTimerRegister+1))<<32))
#else
#define TIMER_TYPE uint64_t
extern volatile uint64_t *systemTimerRegister;
#define tick() (*systemTimerRegister)
#endif 

