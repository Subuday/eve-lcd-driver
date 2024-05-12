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
void NoDiffChangedRectangle(Span *&head)
{
  head = spans;
  head->x = 0;
  head->endX = head->lastScanEndX = gpuFrameWidth;
  head->y = 0;
  head->endY = gpuFrameHeight;
  head->size = gpuFrameWidth*gpuFrameHeight;
  head->next = 0;
}
// #endif

void DiffFramebuffersToScanlineSpansFastAndCoarse4Wide(uint16_t *framebuffer, uint16_t *prevFramebuffer, bool interlacedDiff, int interlacedFieldParity, Span *&head)
{
  int numSpans = 0;
  int y = interlacedDiff ? interlacedFieldParity : 0;
  int yInc = interlacedDiff ? 2 : 1;
  // If doing an interlaced update, skip over every second scanline.
  int scanlineInc = interlacedDiff ? (gpuFramebufferScanlineStrideBytes>>2) : (gpuFramebufferScanlineStrideBytes>>3);
  uint64_t *scanline = (uint64_t *)(framebuffer + y*(gpuFramebufferScanlineStrideBytes>>1));
  uint64_t *prevScanline = (uint64_t *)(prevFramebuffer + y*(gpuFramebufferScanlineStrideBytes>>1)); // (same scanline from previous frame, not preceding scanline)

  const int W = gpuFrameWidth>>2;
  printf("Diff frame buffers to scanline spans fast! %d %d\n", gpuFrameWidth, W);

  Span *span = spans;
  while(y < gpuFrameHeight)
  {
    uint16_t *scanlineStart = (uint16_t *)scanline;
    for(int x = 0; x < W;)
    {
      if (scanline[x] != prevScanline[x])
      {

        // XOR operation highlights differences between the corresponding blocks of pixels by producing a 64-bit result
        // __builtin_ctzll() computes position index of the first '1' bit when counted from the least significant bit. This index indicates the first bit difference in the 64-bit chunk
        //  Pixel-based indexing is done by shifting right by 4 bits (>> 4), effectively dividing the bit position by 16 (2^4) because each pixel occupies 16 bits.
        // scanline + x Allows access to individual pixels
        uint16_t *spanStart = (uint16_t *)(scanline + x) + (__builtin_ctzll(scanline[x] ^ prevScanline[x]) >> 4);
        ++x;

        // We've found a start of a span of different pixels on this scanline, now find where this span ends
        uint16_t *spanEnd;
        for(;;)
        {
          if (x < W)
          {
            if (scanline[x] != prevScanline[x])
            {
              ++x;
              continue;
            }
            else
            {
              spanEnd = (uint16_t *)(scanline + x) + 1 - (__builtin_clzll(scanline[x-1] ^ prevScanline[x-1]) >> 4);
              ++x;
              break;
            }
          }
          else
          {
            spanEnd = scanlineStart + gpuFrameWidth;
            break;
          }
        }

        // Submit the span update task
        span->x = spanStart - scanlineStart;
        span->endX = span->lastScanEndX = spanEnd - scanlineStart;
        span->y = y;
        span->endY = y+1;
        span->size = spanEnd - spanStart;
        span->next = span+1;
        ++span;
        ++numSpans;
      }
      else
      {
        ++x;
      }
    }
    y += yInc;
    scanline += scanlineInc;
    prevScanline += scanlineInc;
  }

  if (numSpans > 0)
  {
    head = &spans[0];
    spans[numSpans-1].next = 0;
  }
  else
    head = 0;
}

// TODO: Maybe remove interlace update
void DiffFramebuffersToScanlineSpansExact(uint16_t *framebuffer, uint16_t *prevFramebuffer, bool interlacedDiff, int interlacedFieldParity, Span *&head) {
  int numSpans = 0;

  int y = interlacedDiff ? interlacedFieldParity : 0;
  int yInc = interlacedDiff ? 2 : 1;

  // If doing an interlaced update, skip over every second scanline.
  int scanlineInc = interlacedDiff ? gpuFramebufferScanlineStrideBytes : (gpuFramebufferScanlineStrideBytes>>1);
  int scanlineEndInc = scanlineInc - gpuFrameWidth;
  
  uint16_t *scanline = framebuffer + y*(gpuFramebufferScanlineStrideBytes>>1);
  uint16_t *prevScanline = prevFramebuffer + y*(gpuFramebufferScanlineStrideBytes>>1); // (same scanline from previous frame, not preceding scanline)

  while(y < gpuFrameHeight) {
    uint16_t *scanlineStart = scanline;
    uint16_t *scanlineEnd = scanline + gpuFrameWidth;
    while(scanline < scanlineEnd) {
      uint16_t *spanStart;
      uint16_t *spanEnd;

      if (scanline + 1 < scanlineEnd)
      {
        uint32_t diff = (*(uint32_t *)scanline) ^ (*(uint32_t *)prevScanline);

        if (diff == 0) {  // Both 1st and 2nd pixels are the same
          scanline += 2;
          prevScanline += 2;
          continue;
        }

        int numConsecutiveUnchangedPixels = 0;

        if ((diff & 0xFFFF) == 0) {  // 1st pixels are the same, 2nd pixels are not
          spanStart = scanline + 1;
          spanEnd = scanline + 2;
        } else { // 1st pixels are different
          spanStart = scanline;
          if ((diff & 0xFFFF0000u) == 0) { // 2nd pixels are the same
            spanEnd = scanline + 1;
            numConsecutiveUnchangedPixels = 1;
          } else { // 2nd pixels are different
            spanEnd = scanline + 2;
          }
        }

        scanline += 2;
        prevScanline += 2;

        // We've found a start of a span of different pixels on this scanline, now find where this span ends
        while(scanline < scanlineEnd) {
          bool arePixelsDifferent = (*scanline != *prevScanline);
          scanline += 1;
          prevScanline += 1;

          if (arePixelsDifferent) {
            spanEnd = scanline;
            numConsecutiveUnchangedPixels = 0;
          } else {
            numConsecutiveUnchangedPixels += 1;
            if (numConsecutiveUnchangedPixels > SPAN_MERGE_THRESHOLD) {
              break;
            }
          }
        }
      } else { // handle the single last pixel on the row
        bool arePixelsDifferent = (*scanline != *prevScanline);

        if (arePixelsDifferent) {
          spanStart = scanline;
          spanEnd = scanline  + 1;
        }

        scanline += 1;
        prevScanline += 1;
      }

      // Submit the span update task
      Span *span = spans + numSpans;
      span->x = spanStart - scanlineStart;
      span->endX = span->lastScanEndX = spanEnd - scanlineStart;
      span->y = y;
      span->endY = y + 1;
      span->size = spanEnd - spanStart;
      if (numSpans > 0) {
        span[-1].next = span;
      } else {
        head = span;
      }
      span->next = 0;
      numSpans += 1;
    }
    y += yInc;
    scanline += scanlineEndInc;
    prevScanline += scanlineEndInc;
  }
}

void MergeScanlineSpanList(Span *listHead)
{
  for(Span *i = listHead; i; i = i->next)
  {
    Span *prev = i;
    for(Span *j = i->next; j; j = j->next)
    {
      // If the spans i and j are vertically apart, don't attempt to merge span i any further, since all spans >= j will also be farther vertically apart.
      // (the list is nondecreasing with respect to Span::y)
      if (j->y > i->endY) break;

      // Merge the spans i and j, and figure out the wastage of doing so
      int x = MIN(i->x, j->x);
      int y = MIN(i->y, j->y);
      int endX = MAX(i->endX, j->endX);
      int endY = MAX(i->endY, j->endY);
      int lastScanEndX = (endY > i->endY) ? j->lastScanEndX : ((endY > j->endY) ? i->lastScanEndX : MAX(i->lastScanEndX, j->lastScanEndX));
      int newSize = (endX-x)*(endY-y-1) + (lastScanEndX - x);
      int wastedPixels = newSize - i->size - j->size;
      if (wastedPixels <= SPAN_MERGE_THRESHOLD
#ifdef MAX_SPI_TASK_SIZE
        && newSize*SPI_BYTESPERPIXEL <= MAX_SPI_TASK_SIZE
#endif
      )
      {
        i->x = x;
        i->y = y;
        i->endX = endX;
        i->endY = endY;
        i->lastScanEndX = lastScanEndX;
        i->size = newSize;
        prev->next = j->next;
        j = prev;
      }
      else // Not merging - travel to next node remembering where we came from
        prev = j;
    }
  }
}
