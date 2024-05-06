#pragma once

#include <View.hpp>
#include <stdint.h>

class Surface {
    public:
        static const int WIDTH = 320;
        static const int HEIGHT = 240;
    private:
        uint16_t frameBuffer[HEIGHT][WIDTH];
        View& view;

        void post();
    public:
        Surface(View& View);
        ~Surface();

        void performDrawing();
};