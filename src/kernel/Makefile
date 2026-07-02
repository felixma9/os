BUILD_DIR?=build/
ASM?=nasm
ASMFLAGS?=-f elf32

CC32?=i686-elf-gcc
CFLAGS32?=-ffreestanding -fno-pic -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -std=c99
LD32?=i686-elf-gcc
OBJCOPY32?=i686-elf-objcopy

.PHONY: all kernel clean always

all: kernel

kernel: $(BUILD_DIR)/kernel.bin

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(OBJCOPY32) -O binary $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel.bin

$(BUILD_DIR)/kernel.elf: $(BUILD_DIR)/kernel/entry.o $(BUILD_DIR)/kernel/kernel.o linker.ld
	$(LD32) -T linker.ld -ffreestanding -nostdlib -o $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel/entry.o $(BUILD_DIR)/kernel/kernel.o -lgcc

$(BUILD_DIR)/kernel/entry.o: entry.asm always
	$(ASM) $(ASMFLAGS) -o $(BUILD_DIR)/kernel/entry.o entry.asm

$(BUILD_DIR)/kernel/kernel.o: kernel.c always
	$(CC32) $(CFLAGS32) -c kernel.c -o $(BUILD_DIR)/kernel/kernel.o

always:
	mkdir -p $(BUILD_DIR)/kernel

clean:
	rm -rf $(BUILD_DIR)/kernel $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/kernel.elf
