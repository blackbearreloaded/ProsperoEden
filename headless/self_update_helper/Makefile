# ps5-native-app-boilerplate - The self-update helper, built as a payload for the console's loader.
# ProsperoEden: copied from the boilerplate's examples/self-update-helper; only the source folders
# below differ (the engine is in headless/update_check, miniz in third_party/miniz).
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
include /opt/ps5-payload-sdk/toolchain/prospero.mk
endif

ROOT := ../..
OUTPUT ?= $(ROOT)/build/self-update/self-updater.elf
WORK := $(dir $(OUTPUT))
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -I$(ROOT)/third_party -I../update_check
CFLAGS := -std=c11 -I../update_check -O2 -Wall -Wextra -Werror -DUPDATE_CHECK_NO_NETWORK
MINIZ_CFLAGS := -std=c11 -O2 -w
LDFLAGS := -Wl,--build-id=none

CXX_SOURCES := main.cpp updater.cpp archive.cpp files.cpp
C_SOURCES := ../update_check/self_update_sha256.c ../update_check/update_check.c
MINIZ_SOURCES := $(addprefix $(ROOT)/third_party/miniz/,miniz.c miniz_tinfl.c miniz_tdef.c miniz_zip.c)
OBJECTS := $(addprefix $(WORK),$(notdir $(CXX_SOURCES:.cpp=.o) $(C_SOURCES:.c=.o) $(MINIZ_SOURCES:.c=.o)))
vpath %.c ../update_check $(ROOT)/third_party/miniz

.PHONY: all clean

all: $(OUTPUT)

$(WORK)%.o: %.cpp $(wildcard *.hpp ../update_check/*.h)
	@mkdir -p -- $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(WORK)miniz%.o: miniz%.c
	@mkdir -p -- $(@D)
	$(CC) $(MINIZ_CFLAGS) -c -o $@ $<

$(WORK)%.o: %.c
	@mkdir -p -- $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTPUT): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^

clean:
	rm -rf -- $(WORK)
