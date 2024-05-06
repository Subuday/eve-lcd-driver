#include <Surface.h>

Surface::Surface(View& view) : view(view) {}

void Surface::performDrawing() {
    view.draw(Surface::WIDTH, Surface::HEIGHT, &frameBuffer[0][0]);
    post();
}

void Surface::post() {
    
}

Surface::~Surface() {}