#pragma once

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/futex.h>
#include <linux/spi/spidev.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <endian.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>

#include "config.h"
#include "text.h"
#include "spi.h"
#include "gpu.h"
#include "statistics.h"
#include "tick.h"
#include "display.h"
#include "util.h"
#include "mailbox.h"
#include "diff.h"
#include "mem_alloc.h"

class Gpu {

    private:
        // int gpuFrameWidth = 240;
        // int gpuFrameHeight = 320;

        // int displayXOffset = 0;
        // int displayYOffset = 0;

        // TODO: Calculate in runtime
        // int gpuFramebufferScanlineStrideBytes = 480;
        // int gpuFramebufferSizeBytes = 153600;

        // int excessPixelsLeft = 0;
        // int excessPixelsRight = 0;
        // int excessPixelsTop = 0;
        // int excessPixelsBottom = 0;

        int spiX = -1;
        int spiEndX = DISPLAY_WIDTH;
        int spiY = -1;

        bool displayOff = false;

        int startY = 10;
        int inv = 0;

        uint16_t* framebuffer[2];

        uint32_t curFrameEnd;
        uint32_t prevFrameEnd;

        bool prevFrameWasInterlacedUpdate = false;
        bool interlacedUpdate = false; // True if the previous update we did was an interlaced half field update.
        int frameParity = 0;           // For interlaced frame updates, this is either 0 or 1 to denote evens or odds.

        int countChangedPixels(uint16_t *framebuffer, uint16_t *prevFramebuffer);
    public:
        Gpu();
        void init();
        void post(uint16_t* buffer);
        void deinit();
};