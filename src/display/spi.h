#pragma once

#ifndef KERNEL_MODULE
#include <inttypes.h>
#include <sys/syscall.h>
#endif
#include <linux/futex.h>

#include "display.h"
#include "tick.h"
#include "display.h"

#define BCM2835_GPIO_BASE                    0x200000   // Address to GPIO register file
#define BCM2835_SPI0_BASE                    0x204000   // Address to SPI0 register file
#define BCM2835_TIMER_BASE                   0x3000     // Address to System Timer register file

#define BCM2835_SPI0_CS_RXF                  0x00100000 // Receive FIFO is full
#define BCM2835_SPI0_CS_RXR                  0x00080000 // FIFO needs reading
#define BCM2835_SPI0_CS_TXD                  0x00040000 // TXD TX FIFO can accept Data
#define BCM2835_SPI0_CS_RXD                  0x00020000 // RXD RX FIFO contains Data
#define BCM2835_SPI0_CS_DONE                 0x00010000 // Done transfer Done
#define BCM2835_SPI0_CS_ADCS                 0x00000800 // Automatically Deassert Chip Select
#define BCM2835_SPI0_CS_INTR                 0x00000400 // Fire interrupts on RXR?
#define BCM2835_SPI0_CS_INTD                 0x00000200 // Fire interrupts on DONE?
#define BCM2835_SPI0_CS_DMAEN                0x00000100 // Enable DMA transfers?
#define BCM2835_SPI0_CS_TA                   0x00000080 // Transfer Active
#define BCM2835_SPI0_CS_CLEAR                0x00000030 // Clear FIFO Clear RX and TX
#define BCM2835_SPI0_CS_CLEAR_RX             0x00000020 // Clear FIFO Clear RX
#define BCM2835_SPI0_CS_CLEAR_TX             0x00000010 // Clear FIFO Clear TX
#define BCM2835_SPI0_CS_CPOL                 0x00000008 // Clock Polarity
#define BCM2835_SPI0_CS_CPHA                 0x00000004 // Clock Phase
#define BCM2835_SPI0_CS_CS                   0x00000003 // Chip Select

#define BCM2835_SPI0_CS_RXF_SHIFT                  20
#define BCM2835_SPI0_CS_RXR_SHIFT                  19
#define BCM2835_SPI0_CS_TXD_SHIFT                  18
#define BCM2835_SPI0_CS_RXD_SHIFT                  17
#define BCM2835_SPI0_CS_DONE_SHIFT                 16
#define BCM2835_SPI0_CS_ADCS_SHIFT                 11
#define BCM2835_SPI0_CS_INTR_SHIFT                 10
#define BCM2835_SPI0_CS_INTD_SHIFT                 9
#define BCM2835_SPI0_CS_DMAEN_SHIFT                8
#define BCM2835_SPI0_CS_TA_SHIFT                   7
#define BCM2835_SPI0_CS_CLEAR_RX_SHIFT             5
#define BCM2835_SPI0_CS_CLEAR_TX_SHIFT             4
#define BCM2835_SPI0_CS_CPOL_SHIFT                 3
#define BCM2835_SPI0_CS_CPHA_SHIFT                 2
#define BCM2835_SPI0_CS_CS_SHIFT                   0

#define GPIO_SPI0_MOSI  10        // Pin P1-19, MOSI when SPI0 in use
#define GPIO_SPI0_MISO   9        // Pin P1-21, MISO when SPI0 in use
#define GPIO_SPI0_CLK   11        // Pin P1-23, CLK when SPI0 in use
#define GPIO_SPI0_CE0    8        // Pin P1-24, CE0 when SPI0 in use
#define GPIO_SPI0_CE1    7        // Pin P1-26, CE1 when SPI0 in use

extern volatile void *bcm2835;

typedef struct GPIORegisterFile
{
  uint32_t gpfsel[6], reserved0; // GPIO Function Select registers, 3 bits per pin, 10 pins in an uint32_t
  uint32_t gpset[2], reserved1; // GPIO Pin Output Set registers, write a 1 to bit at index I to set the pin at index I high
  uint32_t gpclr[2], reserved2; // GPIO Pin Output Clear registers, write a 1 to bit at index I to set the pin at index I low
  uint32_t gplev[2];
} GPIORegisterFile;
extern volatile GPIORegisterFile *gpio;

typedef struct SPIRegisterFile
{
  uint32_t cs;   // SPI Master Control and Status register
  uint32_t fifo; // SPI Master TX and RX FIFOs
  uint32_t clk;  // SPI Master Clock Divider
  uint32_t dlen; // SPI Master Number of DMA Bytes to Write
} SPIRegisterFile;
extern volatile SPIRegisterFile *spi;

// Defines the size of the SPI task memory buffer in bytes. This memory buffer can contain two frames worth of tasks at maximum,
// so for best performance, should be at least ~DISPLAY_WIDTH*DISPLAY_HEIGHT*BYTES_PER_PIXEL*2 bytes in size, plus some small
// amount for structuring each SPITask command. Technically this can be something very small, like 4096b, and not need to contain
// even a single full frame of data, but such small buffers can cause performance issues from threads starving.
#define SHARED_MEMORY_SIZE (DISPLAY_DRAWABLE_WIDTH*DISPLAY_DRAWABLE_HEIGHT*SPI_BYTESPERPIXEL*3)
#define SPI_QUEUE_SIZE (SHARED_MEMORY_SIZE - sizeof(SharedMemory))

#define SPI_9BIT_TASK_PADDING_BYTES 0

// Defines the maximum size of a single SPI task, in bytes. This excludes the command byte. If MAX_SPI_TASK_SIZE
// is not defined, there is no length limit that applies. (In ALL_TASKS_SHOULD_DMA version of DMA transfer,
// there is DMA chaining, so SPI tasks can be arbitrarily long)
#define MAX_SPI_TASK_SIZE 65528

typedef struct __attribute__((packed)) SPITask
{
  uint32_t size; // Size, including both 8-bit and 9-bit tasks
  uint8_t cmd;
  uint32_t dmaSpiHeader;
  uint8_t data[]; // Contains both 8-bit and 9-bit tasks back to back, 8-bit first, then 9-bit.
  inline uint8_t *PayloadStart() { return data; }
  inline uint8_t *PayloadEnd() { return data + size; }
  inline uint32_t PayloadSize() const { return size; }
  inline uint32_t *DmaSpiHeaderAddress() { return &dmaSpiHeader; }
} SPITask;

#define WAIT_SPI_FINISHED()  do { \
    uint32_t cs; \
    while (!((cs = spi->cs) & BCM2835_SPI0_CS_DONE)) /* While DONE=0*/ \
    { \
      if ((cs & (BCM2835_SPI0_CS_RXR | BCM2835_SPI0_CS_RXF))) \
        spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA | DISPLAY_SPI_DRIVE_SETTINGS; \
    } \
  } while(0)


// A convenience for defining and dispatching SPI task bytes inline
#define SPI_TRANSFER(command, ...) do { \
    char data_buffer[] = { __VA_ARGS__ }; \
    SPITask *t = AllocTask(sizeof(data_buffer)); \
    t->cmd = (command); \
    memcpy(t->data, data_buffer, sizeof(data_buffer)); \
    CommitTask(t); \
    RunSPITask(t); \
    DoneTask(t); \
  } while(0)

#define QUEUE_SPI_TRANSFER(command, ...) do { \
    char data_buffer[] = { __VA_ARGS__ }; \
    SPITask *t = AllocTask(sizeof(data_buffer)); \
    t->cmd = (command); \
    memcpy(t->data, data_buffer, sizeof(data_buffer)); \
    CommitTask(t); \
  } while(0)

// Regular 8-bit interface with 16bits wide set cursor commands (most displays)
#define QUEUE_MOVE_CURSOR_TASK(cursor, pos) do { \
    SPITask *task = AllocTask(2); \
    task->cmd = (cursor); \
    task->data[0] = (pos) >> 8; \
    task->data[1] = (pos) & 0xFF; \
    bytesTransferred += 3; \
    CommitTask(task); \
  } while(0)

#define QUEUE_SET_WRITE_WINDOW_TASK(cursor, x, endX) do { \
    SPITask *task = AllocTask(4); \
    task->cmd = (cursor); \
    task->data[0] = (x) >> 8; \
    task->data[1] = (x) & 0xFF; \
    task->data[2] = (endX) >> 8; \
    task->data[3] = (endX) & 0xFF; \
    bytesTransferred += 5; \
    CommitTask(task); \
  } while(0)

typedef struct SharedMemory
{
  volatile uint32_t queueHead;
  volatile uint32_t queueTail;
  volatile uint32_t spiBytesQueued; // Number of actual payload bytes in the queue
  volatile uint32_t interruptsRaised;
  volatile uintptr_t sharedMemoryBaseInPhysMemory;
  volatile uint8_t buffer[];
} SharedMemory;

extern SharedMemory *spiTaskMemory;
extern double spiUsecsPerByte;

extern SharedMemory *dmaSourceMemory; // TODO: Optimize away the need to have this at all, instead DMA directly from SPI ring buffer if possible

#ifdef STATISTICS
extern volatile uint64_t spiThreadIdleUsecs;
extern volatile uint64_t spiThreadSleepStartTime;
extern volatile int spiThreadSleeping;
#endif

extern int mem_fd;

static inline SPITask *AllocTask(uint32_t bytes) // Returns a pointer to a new SPI task block, called on main thread
{
  uint32_t bytesToAllocate = sizeof(SPITask) + bytes;// + totalBytesFor9BitTask;
  uint32_t tail = spiTaskMemory->queueTail;
  uint32_t newTail = tail + bytesToAllocate;
  // Is the new task too large to write contiguously into the ring buffer, that it's split into two parts? We never split,
  // but instead write a sentinel at the end of the ring buffer, and jump the tail back to the beginning of the buffer and
  // allocate the new task there. However in doing so, we must make sure that we don't write over the head marker.
  if (newTail + sizeof(SPITask)/*Add extra SPITask size so that there will always be room for eob marker*/ >= SPI_QUEUE_SIZE)
  {
    uint32_t head = spiTaskMemory->queueHead;
    // Write a sentinel, but wait for the head to advance first so that it is safe to write.
    while(head > tail || head == 0/*Head must move > 0 so that we don't stomp on it*/)
    {
      head = spiTaskMemory->queueHead;
    }
    SPITask *endOfBuffer = (SPITask*)(spiTaskMemory->buffer + tail);
    endOfBuffer->cmd = 0; // Use cmd=0x00 to denote "end of buffer, wrap to beginning"
    __sync_synchronize();
    spiTaskMemory->queueTail = 0;
    __sync_synchronize();
    if (spiTaskMemory->queueHead == tail) syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
    tail = 0;
    newTail = bytesToAllocate;
  }

  // If the SPI task queue is full, wait for the SPI thread to process some tasks. This throttles the main thread to not run too fast.
  uint32_t head = spiTaskMemory->queueHead;
  while(head > tail && head <= newTail)
  {
    usleep(100); // Since the SPI queue is full, we can afford to sleep a bit on the main thread without introducing lag.
    head = spiTaskMemory->queueHead;
  }

  SPITask *task = (SPITask*)(spiTaskMemory->buffer + tail);
  task->size = bytes;
  return task;
}

static inline void CommitTask(SPITask *task) // Advertises the given SPI task from main thread to worker, called on main thread
{
  __sync_synchronize();
  uint32_t tail = spiTaskMemory->queueTail;
  spiTaskMemory->queueTail = (uint32_t)((uint8_t*)task - spiTaskMemory->buffer) + sizeof(SPITask) + task->size;
  __atomic_fetch_add(&spiTaskMemory->spiBytesQueued, task->PayloadSize()+1, __ATOMIC_RELAXED);
  __sync_synchronize();
  if (spiTaskMemory->queueHead == tail) syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
}

#define IN_SINGLE_THREADED_MODE_RUN_TASK() ((void)0)

int InitSPI(void);
uint8_t st7789_interface_spi_init();
void DeinitSPI(void);
void ExecuteSPITasks(void);
void RunSPITask(SPITask *task);
SPITask *GetTask(void);
void DoneTask(SPITask *task);
