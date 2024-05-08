#pragma once

#include <spi.h>

void begin_spi_communication(volatile SPIRegisterFile *spi);
void end_spi_communication(volatile SPIRegisterFile *spi);