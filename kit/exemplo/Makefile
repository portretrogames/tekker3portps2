# make -C kit/exemplo  -> exemplo.elf (PCSX2 ou PS2 via OPL/wLaunchELF)
EE_BIN = exemplo.elf
EE_OBJS = build/main.o
KIT_DIR := ..
KIT_BUILD := build
include $(KIT_DIR)/kit.mk
EE_OBJS += $(KIT_OBJS)
EE_INCS += $(KIT_INCS)
EE_LIBS += $(KIT_LIBS)

all: $(EE_BIN)
build/main.o: main.c
	@mkdir -p build
	$(EE_CC) -D_EE -G0 -O2 -Wall $(KIT_INCS) -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -c $< -o $@

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
