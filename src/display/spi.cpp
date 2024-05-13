#include <stdio.h> // printf, stderr
#include <syslog.h> // syslog
#include <fcntl.h> // open, O_RDWR, O_SYNC
#include <sys/mman.h> // mmap, munmap
#include <pthread.h> // pthread_create
#include <bcm_host.h> // bcm_host_get_peripheral_address, bcm_host_get_peripheral_size, bcm_host_get_sdram_address

#include "config.h"
#include "spi.h"
#include "spi_utils.h"
#include "util.h"
#include "mailbox.h"
#include "mem_alloc.h"
#include "st7789V.h"
#include "gpio_utils.h"

// Uncomment this to print out all bytes sent to the SPI bus
// #define DEBUG_SPI_BUS_WRITES

#ifdef DEBUG_SPI_BUS_WRITES
#define DEBUG_PRINT_WRITTEN_BYTE(byte) do { \
  printf("%02X", byte); \
  if ((writeCounter & 3) == 0) printf("\n"); \
  } while(0)
#else
#define DEBUG_PRINT_WRITTEN_BYTE(byte) ((void)0)
#endif


static uint32_t writeCounter = 0;

int mem_fd = -1;
volatile void *bcm2835 = 0;
volatile GPIORegisterFile *gpio = 0;
volatile SPIRegisterFile *spi = 0;

// Points to the system timer register. N.B. spec sheet says this is two low and high parts, in an 32-bit aligned (but not 64-bit aligned) address. Profiling shows
// that Pi 3 Model B does allow reading this as a u64 load, and even when unaligned, it is around 30% faster to do so compared to loading in parts "lo | (hi << 32)".
volatile uint64_t *systemTimerRegister = 0;

// Errata to BCM2835 behavior: documentation states that the SPI0 DLEN register is only used for DMA. However, even when DMA is not being utilized, setting it from
// a value != 0 or 1 gets rid of an excess idle clock cycle that is present when transmitting each byte. (by default in Polled SPI Mode each 8 bits transfer in 9 clocks)
// With DLEN=2 each byte is clocked to the bus in 8 cycles, observed to improve max throughput from 56.8mbps to 63.3mbps (+11.4%, quite close to the theoretical +12.5%)
// https://www.raspberrypi.org/forums/viewtopic.php?f=44&t=181154
#define UNLOCK_FAST_8_CLOCKS_SPI() (spi->dlen = 2)

SPITask* spi_create_task(spi_loop* loop, uint32_t bytes) {
  // printf("SPI Task allocated with number of bytes %d: \n", bytes);
  uint32_t bytesToAllocate = sizeof(SPITask) + bytes;// + totalBytesFor9BitTask;
  uint32_t tail = spiTaskMemory->queueTail;
  uint32_t newTail = tail + bytesToAllocate;
  // Is the new task too large to write contiguously into the ring buffer, that it's split into two parts? We never split,
  // but instead write a sentinel at the end of the ring buffer, and jump the tail back to the beginning of the buffer and
  // allocate the new task there. However in doing so, we must make sure that we don't write over the head marker.
  if (newTail + sizeof(SPITask)/*Add extra SPITask size so that there will always be room for eob marker*/ >= SPI_QUEUE_SIZE)
  {
    printf("SPI Task allocated with overhead!\n");
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

//TODO: Remove unnessery synchr code
void spi_commit_task(spi_loop* loop, SPITask *task) {
  lock_guard<mutex> guard(loop->mutex);
  __sync_synchronize();
  uint32_t tail = spiTaskMemory->queueTail;
  spiTaskMemory->queueTail = (uint32_t)((uint8_t*)task - spiTaskMemory->buffer) + sizeof(SPITask) + task->size;
  __atomic_fetch_add(&spiTaskMemory->spiBytesQueued, task->PayloadSize()+1, __ATOMIC_RELAXED);
  __sync_synchronize();
  if (spiTaskMemory->queueHead == tail) syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
}

void WaitForPolledSPITransferToFinish()
{
  uint32_t cs;
  while (!(((cs = spi->cs) ^ BCM2835_SPI0_CS_TA) & (BCM2835_SPI0_CS_DONE | BCM2835_SPI0_CS_TA))) // While TA=1 and DONE=0
    if ((cs & (BCM2835_SPI0_CS_RXR | BCM2835_SPI0_CS_RXF)))
      spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;

  if ((cs & BCM2835_SPI0_CS_RXD)) spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;
}

void spi_run_task(spi_loop* loop, SPITask *task) {
  WaitForPolledSPITransferToFinish();

  // The Adafruit 1.65" 240x240 ST7789 based display is unique compared to others that it does want to see the Chip Select line go
  // low and high to start a new command. For that display we let hardware SPI toggle the CS line, and actually run TA<-0 and TA<-1
  // transitions to let the CS line live. For most other displays, we just set CS line always enabled for the display throughout fbcp-ili9341 lifetime,
  // which is a tiny bit faster.
  // printf("SPI Running Task BEGIN SPI COMMUNICATION!");
  begin_spi_communication(spi);

  uint8_t *tStart = task->PayloadStart();
  uint8_t *tEnd = task->PayloadEnd();
  const uint32_t payloadSize = tEnd - tStart;
  uint8_t *tPrefillEnd = tStart + MIN(15, payloadSize);

  // An SPI transfer to the display always starts with one control (command) byte, followed by N data bytes.
  clear_gpio(gpio, GPIO_TFT_DATA_CONTROL);

  // printf("DISPLAY SPI BuS IS NOT 16BITS WIDE!");
  spi_write_fifo(loop, task->cmd);

  while(!(spi->cs & (BCM2835_SPI0_CS_RXD|BCM2835_SPI0_CS_DONE))) /*nop*/;

  set_gpio(gpio, GPIO_TFT_DATA_CONTROL);

  {
    while(tStart < tPrefillEnd) spi_write_fifo(loop, *tStart++);
    while(tStart < tEnd)
    {
      uint32_t cs = spi->cs;
      if ((cs & BCM2835_SPI0_CS_TXD)) spi_write_fifo(loop, *tStart++);
// TODO:      else asm volatile("yield");
      if ((cs & (BCM2835_SPI0_CS_RXR|BCM2835_SPI0_CS_RXF))) spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;
    }
  }

  end_spi_communication(spi);
}

SharedMemory *spiTaskMemory = 0;
volatile uint64_t spiThreadIdleUsecs = 0;
volatile uint64_t spiThreadSleepStartTime = 0;
volatile int spiThreadSleeping = 0;
double spiUsecsPerByte;
spi_loop* loop = nullptr;

static SPITask* spi_front_task(spi_loop* loop) {
  lock_guard<mutex> guard(loop->mutex);
  uint32_t head = spiTaskMemory->queueHead;
  uint32_t tail = spiTaskMemory->queueTail;
  if (head == tail) return 0;
  SPITask *task = (SPITask*)(spiTaskMemory->buffer + head);
  if (task->cmd == 0) // Wrapped around?
  {
    spiTaskMemory->queueHead = 0;
    __sync_synchronize();
    if (tail == 0) return 0;
    task = (SPITask*)spiTaskMemory->buffer;
  }
  return task;
}

void spi_pop_task(spi_loop* loop, SPITask* task) {
  lock_guard<mutex> guard(loop->mutex);
  __atomic_fetch_sub(&spiTaskMemory->spiBytesQueued, task->PayloadSize()+1, __ATOMIC_RELAXED);
  spiTaskMemory->queueHead = (uint32_t)((uint8_t*)task - spiTaskMemory->buffer) + sizeof(SPITask) + task->size;
  __sync_synchronize();
}

extern volatile bool programRunning;

void spi_run_tasks(spi_loop* loop) {
  begin_spi_communication(spi);
  {
    while(programRunning && spiTaskMemory->queueTail != spiTaskMemory->queueHead)
    {
      SPITask *task = spi_front_task(loop);
      if (task)
      {
        spi_run_task(loop, task);
        spi_pop_task(loop, task);
      }
    }
  }
  end_spi_communication(spi);
}

void spi_write_fifo(spi_loop* loop, uint8_t word) {
  loop->spi->fifo = word;
  DEBUG_PRINT_WRITTEN_BYTE(w);
}

pthread_t spiThread;

// A worker thread that keeps the SPI bus filled at all times
void *spi_thread(void *unused)
{
  printf("SPI Worket Thread is created!\n");
  while(programRunning)
  {
    if (spiTaskMemory->queueTail != spiTaskMemory->queueHead)
    {
      spi_run_tasks(loop);
    }
    else
    {
      if (programRunning) syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAIT, spiTaskMemory->queueHead, 0, 0, 0); // Start sleeping until we get new tasks
    }
  }
  pthread_exit(0);
}

int InitSPI()
{
  // Userland version
  // Memory map GPIO and SPI peripherals for direct access
  mem_fd = open("/dev/mem", O_RDWR|O_SYNC);
  if (mem_fd < 0) FATAL_ERROR("can't open /dev/mem (run as sudo)");

  printf("bcm_host_get_peripheral_address: %p, bcm_host_get_peripheral_size: %u, bcm_host_get_sdram_address: %p\n", bcm_host_get_peripheral_address(), bcm_host_get_peripheral_size(), bcm_host_get_sdram_address());
  bcm2835 = mmap(NULL, bcm_host_get_peripheral_size(), (PROT_READ | PROT_WRITE), MAP_SHARED, mem_fd, bcm_host_get_peripheral_address());
  if (bcm2835 == MAP_FAILED) FATAL_ERROR("mapping /dev/mem failed");
  spi = (volatile SPIRegisterFile*)((uintptr_t)bcm2835 + BCM2835_SPI0_BASE);
  gpio = (volatile GPIORegisterFile*)((uintptr_t)bcm2835 + BCM2835_GPIO_BASE);


    printf("Timer is not 32 bit mode\n");
    systemTimerRegister = (volatile TIMER_TYPE*)((uintptr_t)bcm2835 + BCM2835_TIMER_BASE + 0x04); // Generates an unaligned 64-bit pointer, but seems to be fine.

  // TODO: On graceful shutdown, (ctrl-c signal?) close(mem_fd)

  uint32_t currentBcmCoreSpeed = MailboxRet2(0x00030002/*Get Clock Rate*/, 0x4/*CORE*/);
  uint32_t maxBcmCoreTurboSpeed = MailboxRet2(0x00030004/*Get Max Clock Rate*/, 0x4/*CORE*/);

  // Estimate how many microseconds transferring a single byte over the SPI bus takes?
  spiUsecsPerByte = 1000000.0 * 8.0/*bits/byte*/ * SPI_BUS_CLOCK_DIVISOR / maxBcmCoreTurboSpeed;

  printf("BCM core speed: current: %uhz, max turbo: %uhz. SPI CDIV: %d, SPI max frequency: %.0fhz\n", currentBcmCoreSpeed, maxBcmCoreTurboSpeed, SPI_BUS_CLOCK_DIVISOR, (double)maxBcmCoreTurboSpeed / SPI_BUS_CLOCK_DIVISOR);

  // By default all GPIO pins are in input mode (0x00), initialize them for SPI and GPIO writes
  set_gpio_mode(gpio, GPIO_TFT_DATA_CONTROL, 0x01); // Data/Control pin to output (0x01)
  set_gpio_mode(gpio, GPIO_SPI0_MISO, 0x04);
  set_gpio_mode(gpio, GPIO_SPI0_MOSI, 0x04);
  set_gpio_mode(gpio, GPIO_SPI0_CLK, 0x04);
  // The Adafruit 1.65" 240x240 ST7789 based display is unique compared to others that it does want to see the Chip Select line go
  // low and high to start a new command. For that display we let hardware SPI toggle the CS line, and actually run TA<-0 and TA<-1
  // transitions to let the CS line live. For most other displays, we just set CS line always enabled for the display throughout
  // fbcp-ili9341 lifetime, which is a tiny bit faster.
  set_gpio_mode(gpio, GPIO_SPI0_CE0, 0x04);

  spi->cs = BCM2835_SPI0_CS_CLEAR; // Initialize the Control and Status register to defaults: CS=0 (Chip Select), CPHA=0 (Clock Phase), CPOL=0 (Clock Polarity), CSPOL=0 (Chip Select Polarity), TA=0 (Transfer not active), and reset TX and RX queues.
  spi->clk = SPI_BUS_CLOCK_DIVISOR; // Clock Divider determines SPI bus speed, resulting speed=256MHz/clk

  // Initialize SPI thread task buffer memory

  spiTaskMemory = (SharedMemory*)Malloc(SHARED_MEMORY_SIZE, "spi.cpp shared task memory");
  spiTaskMemory->queueHead = spiTaskMemory->queueTail = spiTaskMemory->spiBytesQueued = 0;
  printf("SPI Loop is under creating!\n");
  loop = new spi_loop(); //(spi_loop*)Malloc(sizeof(spi_loop), "spi loop");
  loop->spi = spi;
  printf("SPI LOOP is created\n");
  if (loop == nullptr) {
    printf("SPI LOOP IS NOT CREATED!\n");
  }

  // Enable fast 8 clocks per byte transfer mode, instead of slower 9 clocks per byte.
  UNLOCK_FAST_8_CLOCKS_SPI();

  printf("Initializing display\n");

  init_st7789V();
  // InitSPIDisplay();

  // Create a dedicated thread to feed the SPI bus. While this is fast, it consumes a lot of CPU. It would be best to replace
  // this thread with a kernel module that processes the created SPI task queue using interrupts. (while juggling the GPIO D/C line as well)
  printf("Creating SPI task thread\n");
  int rc = pthread_create(&spiThread, NULL, spi_thread, NULL); // After creating the thread, it is assumed to have ownership of the SPI bus, so no SPI chat on the main thread after this.
  if (rc != 0) FATAL_ERROR("Failed to create SPI thread!");

  LOG("InitSPI done");
  return 0;
}

void DeinitSPI()
{
  // printf("Deinit SPI Thread is called!\n");
  pthread_join(spiThread, NULL);
  spiThread = (pthread_t)0;
  // DeinitSPIDisplay();

  spi->cs = BCM2835_SPI0_CS_CLEAR;

#ifdef GPIO_TFT_DATA_CONTROL
  set_gpio_mode(gpio, GPIO_TFT_DATA_CONTROL, 0);
#endif
  set_gpio_mode(gpio, GPIO_SPI0_CE1, 0);
  set_gpio_mode(gpio, GPIO_SPI0_CE0, 0);
  set_gpio_mode(gpio, GPIO_SPI0_MISO, 0);
  set_gpio_mode(gpio, GPIO_SPI0_MOSI, 0);
  set_gpio_mode(gpio, GPIO_SPI0_CLK, 0);

  if (bcm2835)
  {
    munmap((void*)bcm2835, bcm_host_get_peripheral_size());
    bcm2835 = 0;
  }

  if (mem_fd >= 0)
  {
    close(mem_fd);
    mem_fd = -1;
  }


  free(spiTaskMemory);
  spiTaskMemory = 0;
}
