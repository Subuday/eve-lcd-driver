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

    uint16_t tempBuffer[320][240];
    for (int i = 0; i < 240; ++i) {
        for (int j = 0; j < 320; ++j) {
            tempBuffer[j][240 - 1 - i] = frameBuffer[i][j];
        }
    }

    uint16_t destinationBuffer[320][240];
    for (int i = 0; i < 320; ++i) {
      for (int j = 0; j < 240; ++j) {
        destinationBuffer[i][240 - 1 - j] = tempBuffer[i][j];
      }
    }

    gpu.post(&destinationBuffer[0][0]);
}

Surface::~Surface() {}