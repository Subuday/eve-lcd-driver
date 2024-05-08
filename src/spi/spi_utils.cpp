#include <spi_utils.h>

void begin_spi_communication(volatile SPIRegisterFile *spi) {
    spi->cs = BCM2835_SPI0_CS_TA;
}

void end_spi_communication(volatile SPIRegisterFile *spi) {
    uint32_t cs;
    /* While TA=1 and DONE=0*/ 
    while (!(((cs = spi->cs) ^ BCM2835_SPI0_CS_TA) & (BCM2835_SPI0_CS_DONE | BCM2835_SPI0_CS_TA))) {
        if ((cs & (BCM2835_SPI0_CS_RXR | BCM2835_SPI0_CS_RXF))) {
            spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;
        }
    }
    spi->cs = BCM2835_SPI0_CS_CLEAR_RX; /* Clear TA and any pending bytes */ \
}