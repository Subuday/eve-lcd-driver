#include "gpio_utils.h"

void set_gpio_mode(volatile GPIORegisterFile *gpio, unsigned int pin, unsigned int mode) {
    gpio->gpfsel[(pin)/10] = (gpio->gpfsel[(pin)/10] & ~(0x7 << ((pin) % 10) * 3)) | ((mode) << ((pin) % 10) * 3);
}

int get_gpio_mode(volatile GPIORegisterFile *gpio, unsigned int pin) {
    return (gpio->gpfsel[(pin)/10] & (0x7 << ((pin) % 10) * 3)) >> (((pin) % 10) * 3);
}

int get_gpio(volatile GPIORegisterFile *gpio, unsigned int pin) {
    return gpio->gplev[0] & (1 << (pin));
}

void set_gpio(volatile GPIORegisterFile *gpio, unsigned int pin) {
    gpio->gpset[0] = 1 << (pin);
}

void clear_gpio(volatile GPIORegisterFile *gpio, unsigned int pin) {
    gpio->gpclr[0] = 1 << (pin);
}
