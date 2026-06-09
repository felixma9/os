BUILD_DIR?=build/
ASM?=nasm
ASMFLAGS?=-f obj

# use windows watcom installation
CC16?=/mnt/c/WATCOM/binnt64/wcc.exe
CFLAGS16?=-4 -d3 -s -wx -ms -zl -zq
LD16?=/mnt/c/WATCOM/binnt64/wlink.exe

# convert unix path to windows path for watcom
wp = $(shell wslpath -w $(1) | sed 's/\\/\\\\/g')

SOURCES_C=$(wildcard *.c)
SOURCES_ASM=$(wildcard *.asm)

OBJECTS_C=$(patsubst %.c, $(BUILD_DIR)/stage2/c/%.obj, $(SOURCES_C))
OBJECTS_ASM=$(patsubst %.asm, $(BUILD_DIR)/stage2/asm/%.obj, $(SOURCES_ASM))

.PHONY: all stage2 clean always

all: stage2

stage2: $(BUILD_DIR)/stage2.bin
$(BUILD_DIR)/stage2.bin: $(OBJECTS_ASM) $(OBJECTS_C)
	$(LD16) NAME $(call wp,$(BUILD_DIR)/stage2.bin) FILE { $(foreach obj,$(OBJECTS_ASM) $(OBJECTS_C),$(call wp,$(obj))) } OPTION MAP=$(call wp,$(BUILD_DIR)/stage2.map) @$(call wp,$(CURDIR)/linker.lnk)

$(BUILD_DIR)/stage2/c/%.obj: %.c always
	$(CC16) $(CFLAGS16) -fo=$(call wp,$@) $(call wp,$<)

$(BUILD_DIR)/stage2/asm/%.obj: %.asm always
	$(ASM) $(ASMFLAGS) -o $@ $<

always:
	mkdir -p $(BUILD_DIR)/stage2/c
	mkdir -p $(BUILD_DIR)/stage2/asm

clean:
	rm -rf $(BUILD_DIR)/stage2