#pragma once

#include <stdint.h>
#include <Surface.h>

class View {
    public:
        virtual void draw(int width, int height, uint16_t* buffer) = 0;
};