BUILD_DIR?=build/
ASM?=nasm
ASMFLAGS?=-f elf32

CC32?=i686-elf-gcc
CFLAGS32?=-ffreestanding -fno-pic -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -std=c99
LD32?=i686-elf-gcc
OBJCOPY32?=i686-elf-objcopy

SOURCES_C   = $(wildcard *.c)
SOURCES_ASM = $(wildcard *.asm)
OBJECTS_C   = $(patsubst %.c,   $(BUILD_DIR)/kernel/%.o, $(SOURCES_C))
OBJECTS_ASM = $(patsubst %.asm, $(BUILD_DIR)/kernel/%.o, $(SOURCES_ASM))

.PHONY: all kernel clean always

all: kernel

kernel: $(BUILD_DIR)/kernel.bin

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(OBJCOPY32) -O binary $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel.bin

$(BUILD_DIR)/kernel.elf: $(OBJECTS_ASM) $(OBJECTS_C) linker.ld
	$(LD32) -T linker.ld -ffreestanding -nostdlib -o $@ $(OBJECTS_ASM) $(OBJECTS_C) -lgcc

$(BUILD_DIR)/kernel/%.o: %.c always
	$(CC32) $(CFLAGS32) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: %.asm always
	$(ASM) $(ASMFLAGS) -o $@ $<

always:
	mkdir -p $(BUILD_DIR)/kernel

clean:
	rm -rf $(BUILD_DIR)/kernel $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/kernel.elf
