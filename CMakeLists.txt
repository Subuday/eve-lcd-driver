cmake_minimum_required(VERSION 2.8)

project(waveshare_fbcp)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

EXECUTE_PROCESS( COMMAND getconf LONG_BIT COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE )
message( STATUS "Architecture: ${ARCHITECTURE}bit" )

include_directories(/opt/vc/include)
link_directories(/opt/vc/lib)

include_directories(
		src 
		src/config
		src/display
		src/lcd_driver
		src/board
		src/render
		src/spi
		src/gpio/
	)

file(GLOB DIR_SRCS 	*
					src/*
					src/display/*
					src/lcd_driver/*
					src/render/*
					src/spi/*
					src/gpio/*
				)

message(STATUS "Doing a ${CMAKE_BUILD_TYPE} build")
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
  message(STATUS "Pass -DCMAKE_BUILD_TYPE=Release to do a fast optimized build.")
endif()


set(DEFAULT_TO_SINGLE_CORE_BOARD OFF)
set(DEFAULT_TO_ARMV6Z OFF)
set(DEFAULT_TO_ARMV7A OFF)
set(DEFAULT_TO_ARMV8A ON)
set(DEFAULT_USE_VCSM_CMA OFF)

option(SINGLE_CORE_BOARD "Target a Raspberry Pi with only one hardware core (Pi Zero)" ${DEFAULT_TO_SINGLE_CORE_BOARD})
if (SINGLE_CORE_BOARD)
  message(STATUS "Targeting a Raspberry Pi with only one hardware core")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSINGLE_CORE_BOARD=1")
endif()

if (${ARCHITECTURE} STREQUAL "64")
	message(STATUS "Enable AARCH64 build")
	set(DEFAULT_USE_VCSM_CMA ON)
	#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mlittle-endian -funsafe-math-optimizations -DTIMER_32BIT")
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mlittle-endian -funsafe-math-optimizations -mstrict-align -DTIMER_32BIT")
 
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -marm -mabi=aapcs-linux -mhard-float -mfloat-abi=hard -mlittle-endian -mtls-dialect=gnu2 -funsafe-math-optimizations")
endif()

option(USE_VCSM_CMA "Map Memory from CPU instead of GPU" ${DEFAULT_USE_VCSM_CMA})
if (USE_VCSM_CMA)
	message(STATUS "Enabling Map Memory from CPU instead of GPU")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_VCSM_CMA=1")
endif()

option(ARMV6Z "Target a Raspberry Pi with ARMv6Z instruction set (Pi 1A, 1A+, 1B, 1B+, Zero, Zero W)" ${DEFAULT_TO_ARMV6Z})
if (ARMV6Z)
  message(STATUS "Enabling optimization flags that target ARMv6Z instruction set (Pi Model A, Pi Model B, Compute Module 1, Pi Zero/Zero W)")
  # Currently disabled, no test data to know if this would be faster
#  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv6z -mfpu=vfp")
endif()

option(ARMV7A "Target a Raspberry Pi with ARMv7-A instruction set (Pi 2B < rev 1.2)" ${DEFAULT_TO_ARMV7A})
if (ARMV7A)
  message(STATUS "Enabling optimization flags that target ARMv7-A instruction set (Pi 2B < rev 1.2)")
  # Currently disabled, no test data to know if this would be faster
#  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=cortex-a7 -mcpu=cortex-a7 -mtune=cortex-a7 -mfpu=neon-vfpv4")
endif()

option(ARMV8A "Target a Raspberry Pi with ARMv8-A instruction set (Pi 2B >= rev. 1.2, 3B, 3B+, CM3 or CM3 lite)" ${DEFAULT_TO_ARMV8A})
if (ARMV8A)
  message(STATUS "Enabling optimization flags that target ARMv8-A instruction set (Pi 2B >= rev. 1.2, 3B, 3B+, CM3 or CM3 lite)")
  # N.B. Here should be possible to set -mfpu=neon-vfpv4, though for some reason that generates really slow code, tested on gcc (Raspbian 6.3.0-18+rpi1+deb9u1) 6.3.0 20170516
  # Currently disabled, seems to be a tiny 1-2% slower (or no difference, drown in noise)
#  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+crc -mcpu=cortex-a53 -mtune=cortex-a53")
endif()

#
#
#

set(SET_DISPLAY_WIDTH 0 CACHE STRING "Explicitly specify the Display Width (leave out if there is no change Display Width)")
if (SET_DISPLAY_WIDTH)
	message(STATUS "Using ${SET_DISPLAY_WIDTH}px for Display Width")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSET_DISPLAY_WIDTH=${SET_DISPLAY_WIDTH}")
endif()

set(SET_DISPLAY_HEIGHT 0 CACHE STRING "Explicitly specify the Display Heigth (leave out if there is no change Display Heigth)")
if (SET_DISPLAY_HEIGHT)
	message(STATUS "Using ${SET_DISPLAY_HEIGHT}px for Display Width")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSET_DISPLAY_HEIGHT=${SET_DISPLAY_HEIGHT}")
endif()





#
#
#

set(GPIO_TFT_DATA_CONTROL 0 CACHE STRING "Explicitly specify the Data/Control GPIO pin (sometimes also called Register Select)")
if (GPIO_TFT_DATA_CONTROL GREATER 0)
	message(STATUS "Using 4-wire SPI mode of communication, with GPIO pin ${GPIO_TFT_DATA_CONTROL} for Data/Control line")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGPIO_TFT_DATA_CONTROL=${GPIO_TFT_DATA_CONTROL}")
elseif (GPIO_TFT_DATA_CONTROL LESS 0)
	message(STATUS "Using 3-wire SPI mode of communication, i.e. a display that does not have a Data/Control line")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSPI_3WIRE_PROTOCOL=1")
endif()

set(GPIO_TFT_RESET_PIN 0 CACHE STRING "Explicitly specify the Reset GPIO pin (leave out if there is no Reset line)")
if (GPIO_TFT_RESET_PIN)
	message(STATUS "Using GPIO pin ${GPIO_TFT_RESET_PIN} for Reset line")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGPIO_TFT_RESET_PIN=${GPIO_TFT_RESET_PIN}")
endif()

set(GPIO_TFT_BACKLIGHT 0 CACHE STRING "Explicitly specify the Backlight GPIO pin (leave out if there is no controllable Backlight line)")
if (GPIO_TFT_BACKLIGHT)
	message(STATUS "Using GPIO pin ${GPIO_TFT_BACKLIGHT} for backlight")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGPIO_TFT_BACKLIGHT=${GPIO_TFT_BACKLIGHT}")
endif()

set(LOW_BATTERY_PIN 0 CACHE STRING "Explicitly specify the low batt GPIO pin (leave out if there is no low batt signal)")
if (LOW_BATTERY_PIN)
    message(STATUS "Using GPIO pin ${LOW_BATTERY_PIN} for low battery status")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLOW_BATTERY_PIN=${LOW_BATTERY_PIN}")
endif()

option(BACKLIGHT_CONTROL "If true, enables fbcp-ili9341 to take control of backlight" OFF)
if (BACKLIGHT_CONTROL)
	message(STATUS "Enabling fbcp-ili9341 backlight control")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBACKLIGHT_CONTROL")
endif()

option(DISPLAY_CROPPED_INSTEAD_OF_SCALING "If ON, displays the cropped center part of the source image on the SPI display. If OFF, scales the source image to the SPI display" OFF)
if (DISPLAY_CROPPED_INSTEAD_OF_SCALING)
	message(STATUS "Cropping source image to view instead of scaling. This will produce crisp pixel perfect rendering, though edges of the display will be cut off if the HDMI and SPI display resolutions do not match. (pass -DDISPLAY_CROPPED_INSTEAD_OF_SCALING=OFF to scale instead of crop)")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDISPLAY_CROPPED_INSTEAD_OF_SCALING")
else()
	message(STATUS "Scaling source image to view. If the HDMI resolution does not match the SPI display resolution, this will produce blurriness. Match the HDMI display resolution with the SPI resolution in /boot/config.txt to get crisp pixel perfect rendering, or alternatively pass -DDISPLAY_CROPPED_INSTEAD_OF_SCALING=ON to crop instead of scale if you want to view the center of the screen pixel perfect when HDMI and SPI resolutions do not match.")
endif()

option(DISPLAY_BREAK_ASPECT_RATIO_WHEN_SCALING "If ON, the display is scaled stretched to fit the screen, disregarding preserving aspect ratio." ON)
if (DISPLAY_BREAK_ASPECT_RATIO_WHEN_SCALING)
	message(STATUS "Ignoring aspect ratio when scaling source image to the SPI display (Pass -DDISPLAY_BREAK_ASPECT_RATIO_WHEN_SCALING=OFF to preserve aspect ratio)")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDISPLAY_BREAK_ASPECT_RATIO_WHEN_SCALING")
else()
	message(STATUS "Preserving aspect ratio when scaling source image to the SPI display, introducing letterboxing/pillarboxing if HDMI and SPI aspect ratios are different (Pass -DDISPLAY_BREAK_ASPECT_RATIO_WHEN_SCALING=ON to stretch HDMI to cover full screen if you do not care about aspect ratio)")
endif()

set(STATISTICS 1 CACHE STRING "Set to 0, 1 or 2 to configure the level of statistics to display. 0=OFF, 1=regular statistics, 2=frame rate interval histogram")
if (STATISTICS GREATER 1)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFRAME_COMPLETION_TIME_STATISTICS")
endif()

if (STATISTICS GREATER 0)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATISTICS")
endif()

set(SPI_BUS_CLOCK_DIVISOR 0 CACHE STRING "Specify the SPI0 CDIV register value that defines the SPI0 bus speed (No default value, try a safe setting, e.g. -DSPI_BUS_CLOCK_DIVISOR=40 first")
if (SPI_BUS_CLOCK_DIVISOR)
	message(STATUS "SPI_BUS_CLOCK_DIVISOR set to ${SPI_BUS_CLOCK_DIVISOR}. Try setting this to a higher value (must be an even number) if this causes problems. Display update speed = core_freq/divisor. (on Pi3B, by default core_freq=400). A safe starting default value may be -DSPI_BUS_CLOCK_DIVISOR=40")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSPI_BUS_CLOCK_DIVISOR=${SPI_BUS_CLOCK_DIVISOR}")
else()
	message(FATAL_ERROR "Please define -DSPI_BUS_CLOCK_DIVISOR=<some even number> on the CMake command line! (see files ili9341.h/waveshare35b.h for details) This parameter along with core_freq=xxx in /boot/config.txt defines the SPI display speed. Smaller divisor number=faster speed, higher number=slower.")
endif()

option(KERNEL_MODULE_CLIENT "If enabled, run fbcp-ili9341 userland program against the kernel module found in kernel/ subdirectory (must be started before the userland program)" OFF)
if (KERNEL_MODULE_CLIENT)
	message(STATUS "KERNEL_MODULE_CLIENT enabled, building userland program to operate against fbcp-ili9341 kernel module")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DKERNEL_MODULE_CLIENT=1")
endif()

option(DISPLAY_SWAP_BGR "If true, reverses RGB<->BGR color channels" OFF)
if (DISPLAY_SWAP_BGR)
	message(STATUS "Swapping RGB<->BGR color channels")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDISPLAY_SWAP_BGR")
endif()

option(DISPLAY_INVERT_COLORS "If true, inverts display colors (white=0, black=31/63" OFF)
if (DISPLAY_INVERT_COLORS)
	message(STATUS "Inverting display colors (white=0, black=31/63)")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDISPLAY_INVERT_COLORS")
endif()

option(DISPLAY_ROTATE_180_DEGREES "If true, rotates display upside down" OFF)
if (DISPLAY_ROTATE_180_DEGREES)
	message(STATUS "Rotating display output by 180 degrees")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDISPLAY_ROTATE_180_DEGREES")
endif()

option(USE_DMA_TRANSFERS "If enabled, fbcp-ili9341 utilizes DMA to transfer data to the display. Otherwise, Polled SPI mode is used to drive communication with the SPI display" OFF)


if (USE_DMA_TRANSFERS)
	message(STATUS "USE_DMA_TRANSFERS enabled, this improves performance. Try running CMake with -DUSE_DMA_TRANSFERS=OFF it this causes problems, or try adjusting the DMA channels to use with -DDMA_TX_CHANNEL=<num> -DDMA_RX_CHANNEL=<num>.")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_DMA_TRANSFERS=1")
else()
	message(STATUS "USE_DMA_TRANSFERS is disabled, this is not the most efficient mode. Try running CMake with -DUSE_DMA_TRANSFERS=ON to improve performance.")
endif()

set(DMA_TX_CHANNEL 0 CACHE STRING "Specifies the DMA send channel to use")
if (DMA_TX_CHANNEL)
	message(STATUS "DMA_TX_CHANNEL=${DMA_TX_CHANNEL}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDMA_TX_CHANNEL=${DMA_TX_CHANNEL}")
endif()

set(DMA_RX_CHANNEL 0 CACHE STRING "Specifies the DMA receive channel to use")
if (DMA_RX_CHANNEL)
	message(STATUS "DMA_RX_CHANNEL=${DMA_RX_CHANNEL}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDMA_RX_CHANNEL=${DMA_RX_CHANNEL}")
endif()

option(ADAFRUIT_ILI9341_PITFT "Target Adafruit's ILI9341-based PiTFT display" OFF)
option(FREEPLAYTECH_WAVESHARE32B "Target WaveShare32B ILI9341 display on Freeplaytech's CM3/Zero devices)" OFF)
option(WAVESHARE35B_ILI9486 "Target Waveshare's ILI9486-based Waveshare Wavepear 3.5 inch (B) display" OFF)
option(TONTEC_MZ61581 "Target Tontec's MZ61581-based 3.5 inch display" OFF)

if(WAVESHARE_2INCH_LCD)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DST7789 -DST7789VW -DWAVESHARE_2INCH_LCD")
	message(STATUS "Targeting WaveShare 240x320 2inch IPS LCD Hat with ST7789VW controller")
elseif(ST7789)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DST7789")
	message(STATUS "Targeting ST7789")
elseif(ST7789VW)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DST7789 -DST7789VW")
	message(STATUS "Targeting ST7789VW (based as variant of ST7789)")
else()
	message(FATAL_ERROR "Please specify which display controller to use on command line to CMake!")
endif()

add_compile_options(-g)

add_executable(fbcp ${DIR_SRCS})

target_link_libraries(fbcp pthread bcm_host atomic)
