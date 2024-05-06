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
#include <Gpu.hpp>

#include <stdlib.h>  // For random number generation
#include <stdint.h>  // For uint16_t and other standard integer types
#include <time.h>    // For seeding the random number generator with time

int startY = 10;
int inv = 0;

int CountNumChangedPixels(uint16_t *framebuffer, uint16_t *prevFramebuffer)
{
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

uint64_t displayContentsLastChanged = 0;
bool displayOff = false;

volatile bool programRunning = true;

const char *SignalToString(int signal)
{
  if (signal == SIGINT)
    return "SIGINT";
  if (signal == SIGQUIT)
    return "SIGQUIT";
  if (signal == SIGUSR1)
    return "SIGUSR1";
  if (signal == SIGUSR2)
    return "SIGUSR2";
  if (signal == SIGTERM)
    return "SIGTERM";
  return "?";
}

void MarkProgramQuitting()
{
  programRunning = false;
}

void ProgramInterruptHandler(int signal)
{
  printf("Signal %s(%d) received, quitting\n", SignalToString(signal), signal);
  static int quitHandlerCalled = 0;
  if (++quitHandlerCalled >= 5)
  {
    printf("Ctrl-C handler invoked five times, looks like fbcp-ili9341 is not gracefully quitting - performing a forcible shutdown!\n");
    exit(1);
  }
  MarkProgramQuitting();
  __sync_synchronize();
  // Wake the SPI thread if it was sleeping so that it can gracefully quit
  if (spiTaskMemory)
  {
    __atomic_fetch_add(&spiTaskMemory->queueHead, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&spiTaskMemory->queueTail, 1, __ATOMIC_SEQ_CST);
    syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
  }

  // Wake the main thread if it was sleeping for a new frame so that it can gracefully quit
  __atomic_fetch_add(&numNewGpuFrames, 1, __ATOMIC_SEQ_CST);
  syscall(SYS_futex, &numNewGpuFrames, FUTEX_WAKE, 1, 0, 0, 0);
}

int main()
{
  signal(SIGINT, ProgramInterruptHandler);
  signal(SIGQUIT, ProgramInterruptHandler);
  signal(SIGUSR1, ProgramInterruptHandler);
  signal(SIGUSR2, ProgramInterruptHandler);
  signal(SIGTERM, ProgramInterruptHandler);

  Gpu gpu;
  gpu.init();

  while (programRunning) {
    const size_t size = 320 * 240 * sizeof(uint16_t);  // Example size calculation for a framebuffer
    uint16_t* sourceBuffer = new uint16_t[320 * 240];   // Example source buffer, assuming it matches the dimensions of a single framebuffer

    // Optionally initialize sourceBuffer, as an example, fill with some data
    for (int i = 0; i < 320 * 240; ++i) {
      sourceBuffer[i] = static_cast<uint16_t>(i % 65536);  // Example data
    }

    gpu.post(sourceBuffer);

    delete sourceBuffer;
  }

  gpu.deinit();



//   OpenMailbox();
//   InitSPI();
//   // st7789_interface_spi_init();
//   displayContentsLastChanged = tick();
//   displayOff = false;
//   // InitLowBatterySystem();

//   // Track current SPI display controller write X and Y cursors.
//   int spiX = -1;
//   int spiY = -1;
//   int spiEndX = DISPLAY_WIDTH;

//   gpuFrameWidth = 240;
//   gpuFrameHeight = 320;

//   displayXOffset = 0;
//   displayYOffset = 0;

//   gpuFramebufferScanlineStrideBytes = 480;
//   gpuFramebufferSizeBytes = 153600;

//   excessPixelsLeft = 0;
//   excessPixelsRight = 0;
//   excessPixelsTop = 0;
//   excessPixelsBottom = 0;

//   // InitGPU();

//   printf("Display X offset %d\n", displayXOffset);
//   printf("Display Y offset %d\n", displayYOffset);

//   printf("GPU Frames Buffer Scanlines Stride Bytes %d\n", gpuFramebufferScanlineStrideBytes);
//   printf("GPU Frame Buffer Size Bytes %d\n", gpuFramebufferSizeBytes);

//   printf("Display excessPixelsLeft %d\n", excessPixelsLeft);
//   printf("Display excessPixelsRight %d\n", excessPixelsRight);
//   printf("Display excessPixelsTop %d\n", excessPixelsTop);
//   printf("Display excessPixelsBottom %d\n", excessPixelsBottom);

//   printf("GPU Frame Width: %d, GPU Frame Height: %d", gpuFrameWidth, gpuFrameHeight);

//   spans = (Span *)Malloc((gpuFrameWidth * gpuFrameHeight / 2) * sizeof(Span), "main() task spans");


//   int size = gpuFramebufferSizeBytes;

//   printf("Gpu buffer size: %d \n", gpuFramebufferSizeBytes);

//   uint16_t *framebuffer[2] = {(uint16_t *)Malloc(size, "main() framebuffer0"), (uint16_t *)Malloc(gpuFramebufferSizeBytes, "main() framebuffer1")};
//   memset(framebuffer[0], 0, size);                    // Doublebuffer received GPU memory contents, first buffer contains current GPU memory,
//   memset(framebuffer[1], 0, gpuFramebufferSizeBytes); // second buffer contains whatever the display is currently showing. This allows diffing pixels between the two.

//   uint32_t curFrameEnd = spiTaskMemory->queueTail;
//   uint32_t prevFrameEnd = spiTaskMemory->queueTail;

//   bool prevFrameWasInterlacedUpdate = false;
//   bool interlacedUpdate = false; // True if the previous update we did was an interlaced half field update.
//   int frameParity = 0;           // For interlaced frame updates, this is either 0 or 1 to denote evens or odds.
//   // OpenKeyboard();


//   printf("All initialized, now running main loop...\n");
//   while (programRunning)
//   {
//     prevFrameWasInterlacedUpdate = interlacedUpdate;
  
//     // If last update was interlaced, it means we still have half of the image pending to be updated. In such a case,
//     // sleep only until when we expect the next new frame of data to appear, and then continue independent of whether
//     // a new frame was produced or not - if not, then we will submit the rest of the unsubmitted fields. If yes, then
//     // the half fields of the new frame will be sent (or full, if the new frame has very little content)
//     if (prevFrameWasInterlacedUpdate)
//     {
//       // If THROTTLE_INTERLACING is not defined, we'll fall right through and immediately submit the rest of the remaining content on screen to attempt to minimize the visual
//       // observable effect of interlacing, although at the expense of smooth animation (falling through here causes jitter)
//     }
//     else
//     {
//     }

//     bool spiThreadWasWorkingHardBefore = false;

//     // At all times keep at most two rendered frames in the SPI task queue pending to be displayed. Only proceed to submit a new frame
//     // once the older of those has been displayed.
//     bool once = true;
//     while ((spiTaskMemory->queueTail + SPI_QUEUE_SIZE - spiTaskMemory->queueHead) % SPI_QUEUE_SIZE > (spiTaskMemory->queueTail + SPI_QUEUE_SIZE - prevFrameEnd) % SPI_QUEUE_SIZE)
//     {
//       if (spiTaskMemory->spiBytesQueued > 10000)
//         spiThreadWasWorkingHardBefore = true; // SPI thread had too much work in queue atm (2 full frames)

//       // Peek at the SPI thread's workload and throttle a bit if it has got a lot of work still to do.
//       double usecsUntilSpiQueueEmpty = spiTaskMemory->spiBytesQueued * spiUsecsPerByte;
//       printf("SPI thread had too much work in queue atm (2 full frames)! %d", usecsUntilSpiQueueEmpty);
//       if (usecsUntilSpiQueueEmpty > 0)
//       {
//         uint32_t bytesInQueueBefore = spiTaskMemory->spiBytesQueued;
//         uint32_t sleepUsecs = (uint32_t)(usecsUntilSpiQueueEmpty * 0.4);
// #ifdef STATISTICS
//         uint64_t t0 = tick();
// #endif
//         if (sleepUsecs > 1000)
//           usleep(500);

// #ifdef STATISTICS
//         uint64_t t1 = tick();
//         uint32_t bytesInQueueAfter = spiTaskMemory->spiBytesQueued;
//         bool starved = (spiTaskMemory->queueHead == spiTaskMemory->queueTail);
//         if (starved)
//           spiThreadWasWorkingHardBefore = false;

// /*
//         if (once && starved)
//         {
//           printf("Had %u bytes in queue, asked to sleep for %u usecs, got %u usecs sleep, afterwards %u bytes in queue. (got %.2f%% work done)%s\n",
//             bytesInQueueBefore, sleepUsecs, (uint32_t)(t1 - t0), bytesInQueueAfter, (bytesInQueueBefore-bytesInQueueAfter)*100.0/bytesInQueueBefore,
//             starved ? "  SLEPT TOO LONG, SPI THREAD STARVED" : "");
//           once = false;
//         }
// */
// #endif
//       }
//     }

//     int expiredFrames = 0;
//     uint64_t now = tick();
//     while (expiredFrames < frameTimeHistorySize && now - frameTimeHistory[expiredFrames].time >= FRAMERATE_HISTORY_LENGTH)
//       ++expiredFrames;
//     if (expiredFrames > 0)
//     {
//       frameTimeHistorySize -= expiredFrames;
//       for (int i = 0; i < frameTimeHistorySize; ++i)
//         frameTimeHistory[i] = frameTimeHistory[i + expiredFrames];
//     }

//     // printf("Expired frames logic!");

// #ifdef STATISTICS
//     int expiredSkippedFrames = 0;
//     while (expiredSkippedFrames < frameSkipTimeHistorySize && now - frameSkipTimeHistory[expiredSkippedFrames] >= 1000000 /*FRAMERATE_HISTORY_LENGTH*/)
//       ++expiredSkippedFrames;
//     if (expiredSkippedFrames > 0)
//     {
//       frameSkipTimeHistorySize -= expiredSkippedFrames;
//       for (int i = 0; i < frameSkipTimeHistorySize; ++i)
//         frameSkipTimeHistory[i] = frameSkipTimeHistory[i + expiredSkippedFrames];
//     }
// #endif

//     int numNewFrames = 1;// __atomic_load_n(&numNewGpuFrames, __ATOMIC_SEQ_CST);
//     usleep(16 * 1000);
//     // printf("Got num new frames! %d\n", numNewFrames);
//     bool gotNewFramebuffer = true;
//     bool framebufferHasNewChangedPixels = true;
//     uint64_t frameObtainedTime;
//     if (gotNewFramebuffer)
//     {
//       //memcpy(framebuffer[0], videoCoreFramebuffer[1], gpuFramebufferSizeBytes);
//       // Assuming gpuFramebufferSizeBytes gives the total size in bytes and each pixel is 2 bytes
//       int numPixels = gpuFramebufferSizeBytes / sizeof(uint16_t);
//       //printf("Draw num pixels %d", numPixels);
//       int w2 = 320;  // Framebuffer width in pixels
//       int h2 = 240; // Framebuffer height in pixels
//       uint16_t blueColor = (0 << 11) | (0 << 5) | 31;  // Blue color in 5-6-5 format (max value for blue)
//       startY  += 10; // Start 5 lines above the vertical center
//       if (startY >= 320) {
//         startY = 10;
//       }
//       // for (int i = 0; i < numPixels; ++i) {
//       //   uint16_t blackColor = (0 << 11) | (0 << 5) | 0;  // Black color in 5-6-5 format
//       //   framebuffer[0][i] = blackColor;
//       // }
//       int endY = startY + 10;        // End 5 lines below the vertical center (10 px total)
//       if (inv == 0) {
//         blueColor = (0 << 11) | (0 << 5) | 0;
//         inv = 1;
//       } else {
//         inv = 0;
//       }

//       for (int y = startY; y < endY; ++y) {
//           for (int x = 0; x < w2; ++x) {
//               int r = rand() % 2;
//               framebuffer[0][y * w2 + x] = blueColor;
//           }
//       }

// #ifdef STATISTICS
//       uint64_t now = tick();
//       for (int i = 0; i < numNewFrames - 1 && frameSkipTimeHistorySize < FRAMERATE_HISTORY_LENGTH; ++i)
//         frameSkipTimeHistory[frameSkipTimeHistorySize++] = now;
// #endif
//       //usleep(20 * 1000);
//       // __atomic_fetch_sub(&numNewGpuFrames, numNewFrames, __ATOMIC_SEQ_CST);

//       DrawStatisticsOverlay(framebuffer[0]);

//       if (!displayOff)
//         RefreshStatisticsOverlayText();
//     }

//     // If too many pixels have changed on screen, drop adaptively to interlaced updating to keep up the frame rate.
//     double inputDataFps = 1000000.0 / EstimateFrameRateInterval();
//     double desiredTargetFps = MAX(1, MIN(inputDataFps, TARGET_FRAME_RATE));

//     const double timesliceToUseForScreenUpdates = 1500000;

//     const double tooMuchToUpdateUsecs = timesliceToUseForScreenUpdates / desiredTargetFps; // If updating the current and new frame takes too many frames worth of allotted time, drop to interlacing.

//     int numChangedPixels = framebufferHasNewChangedPixels ? CountNumChangedPixels(framebuffer[0], framebuffer[1]) : 0;
//     printf("Number of changed pixels, %d\n", numChangedPixels);

//     uint32_t bytesToSend = numChangedPixels * SPI_BYTESPERPIXEL + (DISPLAY_DRAWABLE_HEIGHT << 1);
//     interlacedUpdate = ((bytesToSend + spiTaskMemory->spiBytesQueued) * spiUsecsPerByte > tooMuchToUpdateUsecs); // Decide whether to do interlacedUpdate - only updates half of the screen

//     if (interlacedUpdate)
//       frameParity = 1 - frameParity; // Swap even-odd fields every second time we do an interlaced update (progressive updates ignore field order)
//     int bytesTransferred = 0;
//     Span *head = 0;

//     // Collect all spans in this image
//     if (framebufferHasNewChangedPixels || prevFrameWasInterlacedUpdate)
//     {
//       // If possible, utilize a faster 4-wide pixel diffing method
// #ifdef FAST_BUT_COARSE_PIXEL_DIFF
//       if (gpuFrameWidth % 4 == 0 && gpuFramebufferScanlineStrideBytes % 8 == 0)
//         DiffFramebuffersToScanlineSpansFastAndCoarse4Wide(framebuffer[0], framebuffer[1], interlacedUpdate, frameParity, head);
//       else
// #endif
//         DiffFramebuffersToScanlineSpansExact(framebuffer[0], framebuffer[1], interlacedUpdate, frameParity, head); // If disabled, or framebuffer width is not compatible, use the exact method
//     }

//     // Merge spans together on adjacent scanlines - works only if doing a progressive update
//     if (!interlacedUpdate)
//       MergeScanlineSpanList(head);

//     // Submit spans
//     if (!displayOff)
//       for (Span *i = head; i; i = i->next)
//       {
//         // Update the write cursor if needed
//         if (spiY != i->y)
//         {
//           // printf("Must move cursor cursor task!");
//           QUEUE_MOVE_CURSOR_TASK(DISPLAY_SET_CURSOR_Y, displayYOffset + i->y);
//           //printf("\r\n y= %d \r\n",displayYOffset + i->y);
//           spiY = i->y;
//         }

//         if (i->endY > i->y + 1 && (spiX != i->x || spiEndX != i->endX)) // Multiline span?
//         {
//           QUEUE_SET_WRITE_WINDOW_TASK(DISPLAY_SET_CURSOR_X, displayXOffset + i->x, displayXOffset + i->endX - 1);
//           spiX = i->x;
//           spiEndX = i->endX;
          
//         }
//         else // Singleline span
//         {
//           if (spiEndX < i->endX) // Need to push the X end window?
//           {
//             // We are doing a single line span and need to increase the X window. If possible,
//             // peek ahead to cater to the next multiline span update if that will be compatible.
//             int nextEndX = gpuFrameWidth;
//             for (Span *j = i->next; j; j = j->next)
//               if (j->endY > j->y + 1)
//               {
//                 if (j->endX >= i->endX)
//                   nextEndX = j->endX;
//                 break;
//               }
//             QUEUE_SET_WRITE_WINDOW_TASK(DISPLAY_SET_CURSOR_X, displayXOffset + i->x, displayXOffset + nextEndX - 1);
//             spiX = i->x;
//             spiEndX = nextEndX;
//           }
//           else
//               // printf("Display write pixels cmd does not reset write cursor!");
//               if (spiX != i->x)
//               {
//                 // printf("Must move cursor task! =0");
//                 QUEUE_MOVE_CURSOR_TASK(DISPLAY_SET_CURSOR_X, displayXOffset + i->x);
//                 IN_SINGLE_THREADED_MODE_RUN_TASK();
//                 spiX = i->x;
//               }
//         }
//         // printf("x = %d y = %d\r\n",displayXOffset,displayYOffset);
//         // Submit the span pixels
//         SPITask *task = AllocTask(i->size * SPI_BYTESPERPIXEL);
//         task->cmd = DISPLAY_WRITE_PIXELS;

//         bytesTransferred += task->PayloadSize() + 1;
//         uint16_t *scanline = framebuffer[0] + i->y * (gpuFramebufferScanlineStrideBytes >> 1);
//         uint16_t *prevScanline = framebuffer[1] + i->y * (gpuFramebufferScanlineStrideBytes >> 1);


//         uint16_t *data = (uint16_t *)task->data;
//         for (int y = i->y; y < i->endY; ++y, scanline += gpuFramebufferScanlineStrideBytes >> 1, prevScanline += gpuFramebufferScanlineStrideBytes >> 1)
//         {
//           int endX = (y + 1 == i->endY) ? i->lastScanEndX : i->endX;
//           int x = i->x;

//           while (x < endX && (x & 1))
//             *data++ = __builtin_bswap16(scanline[x++]);
//           while (x < (endX & ~1U))
//           {
//             uint32_t u = *(uint32_t *)(scanline + x);
//             *(uint32_t *)data = ((u & 0xFF00FF00U) >> 8) | ((u & 0x00FF00FFU) << 8);
//             data += 2;
//             x += 2;
//           }
//           while (x < endX)
//             *data++ = __builtin_bswap16(scanline[x++]);
//           //printf("\r\n x= %d \r\n",displayXOffset + i->x);
          
//           // printf("If  diffing,  need to maintain prev frame");
//           memcpy(prevScanline + i->x, scanline + i->x, (endX - i->x) * FRAMEBUFFER_BYTESPERPIXEL);
//         }

//         CommitTask(task);
//       }

//     // Remember where in the command queue this frame ends, to keep track of the SPI thread's progress over it
//     if (bytesTransferred > 0)
//     {
//       prevFrameEnd = curFrameEnd;
//       curFrameEnd = spiTaskMemory->queueTail;
//     }

// #ifdef STATISTICS
//     if (bytesTransferred > 0)
//     {
//       if (frameTimeHistorySize < FRAME_HISTORY_MAX_SIZE)
//       {
//         frameTimeHistory[frameTimeHistorySize].interlaced = interlacedUpdate || prevFrameWasInterlacedUpdate;
//         frameTimeHistory[frameTimeHistorySize++].time = tick();
//       }
//       AddFrameCompletionTimeMarker();
//     }
//     statsBytesTransferred += bytesTransferred;
// #endif

//   }

//   // DeinitGPU();
//   DeinitSPI();
//   CloseMailbox();
//   printf("Quit.\n");
}
