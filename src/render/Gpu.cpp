#include <cassert>
#include <display.h>
#include <Gpu.hpp>
#include <spi.h>

Gpu::Gpu() {

}

void Gpu::init() {
    OpenMailbox();
    InitSPI();
    
    gpuFrameWidth = 240;
    gpuFrameHeight = 320;

    displayXOffset = 0;
    displayYOffset = 0;

    gpuFramebufferScanlineStrideBytes = 480;
    gpuFramebufferSizeBytes = 153600;

    excessPixelsLeft = 0;
    excessPixelsRight = 0;
    excessPixelsTop = 0;
    excessPixelsBottom = 0;

    printf("Display X offset %d\n", displayXOffset);
    printf("Display Y offset %d\n", displayYOffset);

    printf("GPU Frames Buffer Scanlines Stride Bytes %d\n", gpuFramebufferScanlineStrideBytes);
    printf("GPU Frame Buffer Size Bytes %d\n", gpuFramebufferSizeBytes);

    printf("Display excessPixelsLeft %d\n", excessPixelsLeft);
    printf("Display excessPixelsRight %d\n", excessPixelsRight);
    printf("Display excessPixelsTop %d\n", excessPixelsTop);
    printf("Display excessPixelsBottom %d\n", excessPixelsBottom);

    printf("GPU Frame Width: %d, GPU Frame Height: %d", gpuFrameWidth, gpuFrameHeight);

    spans = (Span *)Malloc((gpuFrameWidth * gpuFrameHeight / 2) * sizeof(Span), "main() task spans");

    int size = gpuFramebufferSizeBytes;
    framebuffer[0] = (uint16_t *)Malloc(size, "main() framebuffer0");
    framebuffer[1] = (uint16_t *)Malloc(gpuFramebufferSizeBytes, "main() framebuffer1");

    memset(framebuffer[0], 0, size);                    // Doublebuffer received GPU memory contents, first buffer contains current GPU memory,
    memset(framebuffer[1], 0, gpuFramebufferSizeBytes); // second buffer contains whatever the display is currently showing. This allows diffing pixels between the two.

    curFrameEnd = spiTaskMemory->queueTail;
    prevFrameEnd = spiTaskMemory->queueTail;

    prevFrameWasInterlacedUpdate = false;
    interlacedUpdate = false; // True if the previous update we did was an interlaced half field update.
    frameParity = 0;           // For interlaced frame updates, this is either 0 or 1 to denote evens or odds.
}

int Gpu::countChangedPixels(uint16_t *framebuffer, uint16_t *prevFramebuffer) {
  int changedPixels = 0;
  for (int y = 0; y < gpuFrameHeight; ++y)
  {
    for (int x = 0; x < gpuFrameWidth; ++x)
      if (framebuffer[x] != prevFramebuffer[x])
        ++changedPixels;

    framebuffer += gpuFramebufferScanlineStrideBytes >> 1;
    prevFramebuffer += gpuFramebufferScanlineStrideBytes >> 1;
  }
  return changedPixels;
}

void Gpu::createSpans(Span*& head, uint16_t* framebuffer, uint16_t* prevFramebuffer, bool interlacedDiff, int interlacedFieldParity) {
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

      if (scanline + 1 < scanlineEnd) {
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

void Gpu::optimizeSpans(Span* head) {
  for(Span* i = head; i; i = i->next) {
    Span* prev = i;
    for(Span *j = i->next; j; j = j->next) {
      
      // If the spans i and j are vertically apart, don't attempt to merge span i any further,
      // since all spans >= j will also be farther vertically apart.
      // (the list is nondecreasing with respect to Span::y)
      if (j->y > i->endY) {
        break;
      }

      // Merge the spans i and j, and figure out the wastage of doing so
      int x = MIN(i->x, j->x);
      int y = MIN(i->y, j->y);
      int endX = MAX(i->endX, j->endX);
      int endY = MAX(i->endY, j->endY);
      int lastScanEndX;
      if (endY > i->endY) {
        lastScanEndX = j->lastScanEndX;
      } else if (endY > j->endY) {
        lastScanEndX = i->lastScanEndX;
      } else {
        lastScanEndX = MAX(i->lastScanEndX, j->lastScanEndX);
      }
      int newSize = (endX - x) * (endY - y - 1) + (lastScanEndX - x);
      int wastedPixels = newSize - i->size - j->size;
      if (wastedPixels <= SPAN_MERGE_THRESHOLD && newSize * SPI_BYTESPERPIXEL <= MAX_SPI_TASK_SIZE) {
        i->x = x;
        i->y = y;
        i->endX = endX;
        i->endY = endY;
        i->lastScanEndX = lastScanEndX;
        i->size = newSize;
        prev->next = j->next;
        j = prev;
      } else {
        prev = j;
      } 
    }
  }
}

void Gpu::postDisplayXPositionUpdate(spi_loop* loop, uint16_t position) {
  SPITask *task = spi_create_task(loop, 2);
  task->cmd = 0x2A; // CASET
  task->data[0] = (position) >> 8;
  task->data[1] = (position) & 0xFF;
  // bytesTransferred += 3;
  spi_commit_task(loop, task);
}

void Gpu::postDisplayYPositionUpdate(spi_loop* loop, uint16_t position) {
  SPITask *task = spi_create_task(loop, 2);
  task->cmd = 0x2B; // RASET
  task->data[0] = (position) >> 8;
  task->data[1] = (position) & 0xFF;
  // bytesTransferred += 3;
  spi_commit_task(loop, task);
}

void Gpu::postDisplayXWindowUpdate(spi_loop* loop, uint16_t start, uint16_t end) {
  SPITask *task = spi_create_task(loop, 4);
  task->cmd = 0x2A; // CASET
  task->data[0] = (start) >> 8;
  task->data[1] = (start) & 0xFF;
  task->data[2] = (end) >> 8;
  task->data[3] = (end) & 0xFF;
  // bytesTransferred += 5;
  spi_commit_task(loop, task);
}

void Gpu::post(uint16_t* buffer) {
    // memcpy(framebuffer[0], buffer, gpuFramebufferSizeBytes);

    // printf("All initialized, now running main loop...\n");

    prevFrameWasInterlacedUpdate = interlacedUpdate;
  
    // If last update was interlaced, it means we still have half of the image pending to be updated. In such a case,
    // sleep only until when we expect the next new frame of data to appear, and then continue independent of whether
    // a new frame was produced or not - if not, then we will submit the rest of the unsubmitted fields. If yes, then
    // the half fields of the new frame will be sent (or full, if the new frame has very little content)
    if (prevFrameWasInterlacedUpdate)
    {
      // If THROTTLE_INTERLACING is not defined, we'll fall right through and immediately submit the rest of the remaining content on screen to attempt to minimize the visual
      // observable effect of interlacing, although at the expense of smooth animation (falling through here causes jitter)
    }
    else
    {
    }

    bool spiThreadWasWorkingHardBefore = false;

    // At all times keep at most two rendered frames in the SPI task queue pending to be displayed. Only proceed to submit a new frame
    // once the older of those has been displayed.
    bool once = true;
    while ((spiTaskMemory->queueTail + SPI_QUEUE_SIZE - spiTaskMemory->queueHead) % SPI_QUEUE_SIZE > (spiTaskMemory->queueTail + SPI_QUEUE_SIZE - prevFrameEnd) % SPI_QUEUE_SIZE)
    {
      if (spiTaskMemory->spiBytesQueued > 10000)
        spiThreadWasWorkingHardBefore = true; // SPI thread had too much work in queue atm (2 full frames)

      // Peek at the SPI thread's workload and throttle a bit if it has got a lot of work still to do.
      double usecsUntilSpiQueueEmpty = spiTaskMemory->spiBytesQueued * spiUsecsPerByte;
      printf("SPI thread had too much work in queue atm (2 full frames)! %d", usecsUntilSpiQueueEmpty);
      if (usecsUntilSpiQueueEmpty > 0)
      {
        uint32_t bytesInQueueBefore = spiTaskMemory->spiBytesQueued;
        uint32_t sleepUsecs = (uint32_t)(usecsUntilSpiQueueEmpty * 0.4);
#ifdef STATISTICS
        uint64_t t0 = tick();
#endif
        if (sleepUsecs > 1000)
          usleep(500);

#ifdef STATISTICS
        uint64_t t1 = tick();
        uint32_t bytesInQueueAfter = spiTaskMemory->spiBytesQueued;
        bool starved = (spiTaskMemory->queueHead == spiTaskMemory->queueTail);
        if (starved)
          spiThreadWasWorkingHardBefore = false;

/*
        if (once && starved)
        {
          printf("Had %u bytes in queue, asked to sleep for %u usecs, got %u usecs sleep, afterwards %u bytes in queue. (got %.2f%% work done)%s\n",
            bytesInQueueBefore, sleepUsecs, (uint32_t)(t1 - t0), bytesInQueueAfter, (bytesInQueueBefore-bytesInQueueAfter)*100.0/bytesInQueueBefore,
            starved ? "  SLEPT TOO LONG, SPI THREAD STARVED" : "");
          once = false;
        }
*/
#endif
      }
    }

    if (spiThreadWasWorkingHardBefore) {
      printf("GPU Thread had too much too work !\n");
    }

    int expiredFrames = 0;
    uint64_t now = tick();
    while (expiredFrames < frameTimeHistorySize && now - frameTimeHistory[expiredFrames].time >= FRAMERATE_HISTORY_LENGTH)
      ++expiredFrames;
    if (expiredFrames > 0)
    {
      frameTimeHistorySize -= expiredFrames;
      for (int i = 0; i < frameTimeHistorySize; ++i)
        frameTimeHistory[i] = frameTimeHistory[i + expiredFrames];
    }

    // printf("Expired frames logic!");

#ifdef STATISTICS
    int expiredSkippedFrames = 0;
    while (expiredSkippedFrames < frameSkipTimeHistorySize && now - frameSkipTimeHistory[expiredSkippedFrames] >= 1000000 /*FRAMERATE_HISTORY_LENGTH*/)
      ++expiredSkippedFrames;
    if (expiredSkippedFrames > 0)
    {
      frameSkipTimeHistorySize -= expiredSkippedFrames;
      for (int i = 0; i < frameSkipTimeHistorySize; ++i)
        frameSkipTimeHistory[i] = frameSkipTimeHistory[i + expiredSkippedFrames];
    }
#endif

    int numNewFrames = 1;// __atomic_load_n(&numNewGpuFrames, __ATOMIC_SEQ_CST);
    // usleep(16 * 1000);
    // printf("Got num new frames! %d\n", numNewFrames);
    bool gotNewFramebuffer = true;
    bool framebufferHasNewChangedPixels = true;
    uint64_t frameObtainedTime;
    if (gotNewFramebuffer)
    {
      memcpy(framebuffer[0], buffer, gpuFramebufferSizeBytes);

#ifdef STATISTICS
      uint64_t now = tick();
      for (int i = 0; i < numNewFrames - 1 && frameSkipTimeHistorySize < FRAMERATE_HISTORY_LENGTH; ++i)
        frameSkipTimeHistory[frameSkipTimeHistorySize++] = now;
#endif
      //usleep(20 * 1000);
      // __atomic_fetch_sub(&numNewGpuFrames, numNewFrames, __ATOMIC_SEQ_CST);

      DrawStatisticsOverlay(framebuffer[0]);

      if (!displayOff)
        RefreshStatisticsOverlayText();
    }

    // If too many pixels have changed on screen, drop adaptively to interlaced updating to keep up the frame rate.
    double inputDataFps = 1000000.0 / EstimateFrameRateInterval();
    double desiredTargetFps = MAX(1, MIN(inputDataFps, TARGET_FRAME_RATE));

    const double timesliceToUseForScreenUpdates = 1500000;

    const double tooMuchToUpdateUsecs = timesliceToUseForScreenUpdates / desiredTargetFps; // If updating the current and new frame takes too many frames worth of allotted time, drop to interlacing.

    int numChangedPixels = framebufferHasNewChangedPixels ? countChangedPixels(framebuffer[0], framebuffer[1]) : 0;
    printf("Number of changed pixels, %d\n", numChangedPixels);

    uint32_t bytesToSend = numChangedPixels * SPI_BYTESPERPIXEL + (DISPLAY_DRAWABLE_HEIGHT << 1);
    interlacedUpdate = ((bytesToSend + spiTaskMemory->spiBytesQueued) * spiUsecsPerByte > tooMuchToUpdateUsecs); // Decide whether to do interlacedUpdate - only updates half of the screen

    assert(!interlacedUpdate);

    // printf("Interlaced update %d\n", interlacedUpdate);

    if (interlacedUpdate)
      frameParity = 1 - frameParity; // Swap even-odd fields every second time we do an interlaced update (progressive updates ignore field order)
    int bytesTransferred = 0;
    Span *head = 0;

    // Collect all spans in this image
    if (framebufferHasNewChangedPixels || prevFrameWasInterlacedUpdate)
    {
      // If possible, utilize a faster 4-wide pixel diffing method
        createSpans(head, framebuffer[0], framebuffer[1], interlacedUpdate, frameParity);
        // NoDiffChangedRectangle(head);
    }

    // Merge spans together on adjacent scanlines - works only if doing a progressive update
    if (!interlacedUpdate) {
      optimizeSpans(head);
    }

    
    // Submit spans
    if (!displayOff)
      for (Span *i = head; i; i = i->next)
      {
        // printf("Span update the write cursor\n");
        // Update the write cursor if needed
        if (spiY != i->y)
        {
          // printf("Must move cursor cursor task!");
          postDisplayYPositionUpdate(loop, displayYOffset + i->y);
          //printf("\r\n y= %d \r\n",displayYOffset + i->y);
          spiY = i->y;
        }

        if (i->endY > i->y + 1 && (spiX != i->x || spiEndX != i->endX)) // Multiline span?
        {
          postDisplayXWindowUpdate(loop, displayXOffset + i->x, displayXOffset + i->endX - 1);
          spiX = i->x;
          spiEndX = i->endX;
        }
        else // Singleline span
        {
          if (spiEndX < i->endX) // Need to push the X end window?
          {
            // We are doing a single line span and need to increase the X window. If possible,
            // peek ahead to cater to the next multiline span update if that will be compatible.
            int nextEndX = gpuFrameWidth;
            for (Span *j = i->next; j; j = j->next)
              if (j->endY > j->y + 1)
              {
                if (j->endX >= i->endX)
                  nextEndX = j->endX;
                break;
              }
            postDisplayXWindowUpdate(loop, displayXOffset + i->x, displayXOffset + nextEndX - 1);
            spiX = i->x;
            spiEndX = nextEndX;
          }
          else
              // printf("Display write pixels cmd does not reset write cursor!");
              if (spiX != i->x)
              {
                // printf("Must move cursor task! =0");
                postDisplayXPositionUpdate(loop, displayXOffset + i->x);
                IN_SINGLE_THREADED_MODE_RUN_TASK();
                spiX = i->x;
              }
        }

        // Submit the span pixels
        SPITask *task = spi_create_task(loop, i->size * SPI_BYTESPERPIXEL);
        task->cmd = DISPLAY_WRITE_PIXELS;

        bytesTransferred += task->PayloadSize() + 1;
        uint16_t *scanline = framebuffer[0] + i->y * (gpuFramebufferScanlineStrideBytes >> 1);
        uint16_t *prevScanline = framebuffer[1] + i->y * (gpuFramebufferScanlineStrideBytes >> 1);


        uint16_t *data = (uint16_t *)task->data;
        for (int y = i->y; y < i->endY; ++y, scanline += gpuFramebufferScanlineStrideBytes >> 1, prevScanline += gpuFramebufferScanlineStrideBytes >> 1)
        {
          int endX = (y + 1 == i->endY) ? i->lastScanEndX : i->endX;
          int x = i->x;

          while (x < endX && (x & 1))
            *data++ = __builtin_bswap16(scanline[x++]);
          while (x < (endX & ~1U))
          {
            uint32_t u = *(uint32_t *)(scanline + x);
            *(uint32_t *)data = ((u & 0xFF00FF00U) >> 8) | ((u & 0x00FF00FFU) << 8);
            data += 2;
            x += 2;
          }
          while (x < endX)
            *data++ = __builtin_bswap16(scanline[x++]);
          //printf("\r\n x= %d \r\n",displayXOffset + i->x);
          
          // printf("If  diffing,  need to maintain prev frame");
          memcpy(prevScanline + i->x, scanline + i->x, (endX - i->x) * FRAMEBUFFER_BYTESPERPIXEL);
        }

        spi_commit_task(loop, task);
      }

    // Remember where in the command queue this frame ends, to keep track of the SPI thread's progress over it
    if (bytesTransferred > 0)
    {
      prevFrameEnd = curFrameEnd;
      curFrameEnd = spiTaskMemory->queueTail;
    }

#ifdef STATISTICS
    if (bytesTransferred > 0)
    {
      if (frameTimeHistorySize < FRAME_HISTORY_MAX_SIZE)
      {
        frameTimeHistory[frameTimeHistorySize].interlaced = interlacedUpdate || prevFrameWasInterlacedUpdate;
        frameTimeHistory[frameTimeHistorySize++].time = tick();
      }
      AddFrameCompletionTimeMarker();
    }
    statsBytesTransferred += bytesTransferred;
#endif
}

void Gpu::deinit() {
    DeinitSPI();
    CloseMailbox();
    printf("Quit.\n");
}