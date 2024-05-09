#pragma once

#include <spi.h>

void set_gpio_mode(volatile GPIORegisterFile *gpio, unsigned int pin, unsigned int mode);

int get_gpio_mode(volatile GPIORegisterFile *gpio, unsigned int pin);

int get_gpio(volatile GPIORegisterFile *gpio, unsigned int pin);

void set_gpio(volatile GPIORegisterFile *gpio, unsigned int pin);

void clear_gpio(volatile GPIORegisterFile *gpio, unsigned int pin);