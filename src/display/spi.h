#pragma once

#ifndef KERNEL_MODULE
#include <inttypes.h>
#include <sys/syscall.h>
#endif
#include <linux/futex.h>

#include "display.h"
#include "tick.h"
#include "display.h"
#include <mutex>

using namespace std;


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

struct spi_loop {
  std::mutex mutex;
  volatile SPIRegisterFile* spi;
};
extern spi_loop* loop;


// A convenience for defining and dispatching SPI task bytes inline
#define SPI_TRANSFER(command, ...) do { \
    char data_buffer[] = { __VA_ARGS__ }; \
    SPITask *t = spi_create_task(loop, sizeof(data_buffer)); \
    t->cmd = (command); \
    memcpy(t->data, data_buffer, sizeof(data_buffer)); \
    spi_commit_task(loop, t); \
    spi_run_task(loop, t); \
    spi_pop_task(loop, t); \
  } while(0)

#define QUEUE_SPI_TRANSFER(command, ...) do { \
    char data_buffer[] = { __VA_ARGS__ }; \
    SPITask *t = spi_create_task(loop, sizeof(data_buffer)); \
    t->cmd = (command); \
    memcpy(t->data, data_buffer, sizeof(data_buffer)); \
    spi_commit_task(loop, t); \
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

SPITask* spi_create_task(spi_loop* loop, uint32_t bytes);
void spi_commit_task(spi_loop* loop, SPITask *task); // Advertises the given SPI task from main thread to worker, called on main thread
void spi_run_tasks(spi_loop* loop);
static SPITask* spi_front_task(spi_loop* loop);
void spi_run_task(spi_loop* loop, SPITask *task);
void spi_pop_task(spi_loop* loop, SPITask *task);

void spi_write_fifo(spi_loop* loop, uint8_t word);

#define IN_SINGLE_THREADED_MODE_RUN_TASK() ((void)0)

// #define IN_SINGLE_THREADED_MODE_RUN_TASK() { \
//   SPITask *t = GetTask(); \
//   RunSPITask(t); \
//   DoneTask(t); \
// }

int InitSPI(void);
uint8_t st7789_interface_spi_init();
void DeinitSPI(void);
