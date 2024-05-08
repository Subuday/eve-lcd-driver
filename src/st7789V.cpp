#include <st7789V.h>
#include <stdio.h>
#include <memory.h>
#include <spi.h>
#include <spi_utils.h>

void init_st7789V() {
    // If a Reset pin is defined, toggle it briefly high->low->high to enable the device. Some devices do not have a reset pin, in which case compile with GPIO_TFT_RESET_PIN left undefined.
    printf("Resetting display at reset GPIO pin %d\n", GPIO_TFT_RESET_PIN);
    SET_GPIO_MODE(GPIO_TFT_RESET_PIN, 1);
    SET_GPIO(GPIO_TFT_RESET_PIN);
    usleep(120 * 1000);
    CLEAR_GPIO(GPIO_TFT_RESET_PIN);
    usleep(120 * 1000);
    SET_GPIO(GPIO_TFT_RESET_PIN);
    usleep(120 * 1000);

    // Do the initialization with a very low SPI bus speed, so that it will succeed even if the bus speed chosen by the user is too high.
    spi->clk = 34;
    __sync_synchronize();

    begin_spi_communication(spi);
    usleep(120 * 1000);

    SPI_TRANSFER(0x11 /*Sleep Out*/);
    usleep(120 * 1000);

    SPI_TRANSFER(0x3A /*COLMOD: Pixel Format Set*/, 0x05 /*16bpp*/);
    usleep(20 * 1000);

    #define MADCTL_ROW_ADDRESS_ORDER_SWAP (1 << 7)
    uint8_t madctl = 0;
    madctl |= MADCTL_ROW_ADDRESS_ORDER_SWAP;
    SPI_TRANSFER(0x36 /*MADCTL: Memory Access Control*/, madctl);
    usleep(20 * 1000);

    SPI_TRANSFER(0x21 /*Display Inversion On*/);
    usleep(20 * 1000);

    SPI_TRANSFER(0x13 /*NORON: Partial off (normal)*/);
    usleep(20 * 1000);

    // The ST7789 controller is actually a unit with 320x240 graphics memory area, but only 240x240 portion
    // of it is displayed. Therefore if we wanted to swap row address mode above, writes to Y=0...239 range will actually land in
    // memory in row addresses Y = 319-(0...239) = 319...80 range. To view this range, we must scroll the view by +80 units in Y
    // direction so that contents of Y=80...319 is displayed instead of Y=0...239.
    if ((madctl & MADCTL_ROW_ADDRESS_ORDER_SWAP)) {
        // printf("ST7789 VSCSAD SENT WIDTH %d", DISPLAY_WIDTH);
        SPI_TRANSFER(0x37 /*VSCSAD: Vertical Scroll Start Address of RAM*/, 0, 320 - DISPLAY_WIDTH);
        usleep(20 * 1000);
    }

    // TODO: The 0xB1 command is not Frame Rate Control for ST7789VW, 0xB3 is (add support to it)
    // Frame rate = 850000 / [ (2*RTNA+40) * (162 + FPA+BPA)]
    // SPI_TRANSFER(0xB1 /*FRMCTR1:Frame Rate Control*/, /*RTNA=*/6, /*FPA=*/1, /*BPA=*/1); // This should set frame rate = 99.67 Hz

    SPI_TRANSFER(/*Display ON*/ 0x29);
    usleep(120 * 1000);

    SPI_TRANSFER(/*VSCSAD*/ 0x37, 0X00,0X00);
    usleep(20 * 1000);

    // #if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
    // printf("Setting TFT backlight on at pin %d\n", GPIO_TFT_BACKLIGHT);
    // SET_GPIO_MODE(GPIO_TFT_BACKLIGHT, 0x01); // Set backlight pin to digital 0/1 output mode (0x01) in case it had been PWM controlled
    // SET_GPIO(GPIO_TFT_BACKLIGHT);            // And turn the backlight on.
    // #endif

    ClearScreen();
    usleep(120 * 1000);

    end_spi_communication(spi);
    usleep(120 * 1000); // Delay a bit before restoring CLK, or otherwise this has been observed to cause the display not init if done back to back after the clear operation above.

    // And speed up to the desired operation speed finally after init is done.
    spi->clk = SPI_BUS_CLOCK_DIVISOR;
}