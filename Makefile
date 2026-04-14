#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# ROMFS is the directory containing data to be added to RomFS
#
# NO_ICON: if set to anything, do not use icon.
# NO_NACP: if set to anything, do not build .nacp file.
# APP_TITLE is the name of the app stored in the .nacp file (Optional)
# APP_AUTHOR is the author of the app stored in the .nacp file (Optional)
# APP_VERSION is the version of the app stored in the .nacp file (Optional)
# ICON is the filename of the icon (.jpg), relative to the project folder.
#   If not set, it attempts to use one of the following (in order):
#     - <Project name>.jpg
#     - icon.jpg
#---------------------------------------------------------------------------------
TARGET      := romm2switch
BUILD       := build
SOURCES     := source source/api source/models source/ui source/ui/screens
DATA        := data
INCLUDES    := source
ROMFS       := romfs

APP_TITLE   := RomM2Switch
APP_AUTHOR  := PeriBluGaming
APP_VERSION := 1.0.0

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS   := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS   += $(INCLUDE) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -fno-rtti -fexceptions -std=c++17
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lharfbuzz -lEGL -lGLESv2 -lSDL2_ttf -lSDL2 -lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
        -lfreetype -lbz2 -lpng -ljpeg -lwebp -lz -lnx

#---------------------------------------------------------------------------------
# list of directories containing libraries
#---------------------------------------------------------------------------------
LIBDIRS := $(PORTLIBS) $(LIBNX) $(PORTLIBS)/sdl2

#---------------------------------------------------------------------------------
# no real need to edit
