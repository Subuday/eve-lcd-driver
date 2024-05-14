#include <Surface.h>

Surface::Surface(View& view) : view(view) {
    for (int i = 0; i < HEIGHT; ++i) {
        for (int j = 0; j < WIDTH; ++j) {
            frameBuffer[i][j] = 0;
        }
    }
}

void Surface::init() {
    gpu.init();
    vsync.callback([this]() {
        performDrawing();
    });
    vsync.start();
}

void Surface::performDrawing() {
    view.draw(Surface::WIDTH, Surface::HEIGHT, &frameBuffer[0][0]);
    gpu.post(&frameBuffer[0][0]);
}

Surface::~Surface() {}