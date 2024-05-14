#pragma once

#include <View.hpp>
#include <Vsync.hpp>
#include <stdint.h>
#include <Gpu.hpp>

class Surface {
    public:
        static const int WIDTH = 320;
        static const int HEIGHT = 240;
    private:
        Vsync vsync;
        Gpu gpu;
        uint16_t frameBuffer[HEIGHT][WIDTH];
        View& view;
    public:
        Surface(View& View);
        ~Surface();

        void init();
        void performDrawing();
};