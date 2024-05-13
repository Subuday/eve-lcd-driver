#include "config.h"
#include "diff.h"
#include "util.h"
#include "display.h"
#include "gpu.h"
#include "spi.h"
#include <cstdio>

Span *spans = 0;

// #ifdef UPDATE_FRAMES_WITHOUT_DIFFING
// Naive non-diffing functionality: just submit the whole display contents
void NoDiffChangedRectangle(Span*& head) {
  head = spans;
  head->x = 0;
  head->endX = head->lastScanEndX = gpuFrameWidth;
  head->y = 0;
  head->endY = gpuFrameHeight;
  head->size = gpuFrameWidth*gpuFrameHeight;
  head->next = 0;
}
// #endif