ASM=nasm

SRC_DIR=src
BUILD_DIR=build
TOOLS_DIR=tools

.PHONY: all floppy_image kernel bootloader clean always run tools_fat

all: floppy_image tools_fat

# Floppy image
floppy_image: $(BUILD_DIR)/main_floppy.img

$(BUILD_DIR)/main_floppy.img: bootloader kernel
#	Create empty floppy image
	dd if=/dev/zero of=$(BUILD_DIR)/main_floppy.img bs=512 count=2880
#	mkfs.fat loads the FAT12 structure into our floppy image, but the bootloader part is empty
	mkfs.fat -F 12 -n "NBOS" $(BUILD_DIR)/main_floppy.img
#	Now we replace the first sector (512 bytes) with our own bootloader; note that the FAT BPB (BIOS Parameter
# 	Block) has been overwritten, so we need to match the FAT format exactly to preserve FAT functionality
#	that's why we write out the FAT values from the specs
	dd if=$(BUILD_DIR)/bootloader.bin of=$(BUILD_DIR)/main_floppy.img conv=notrunc
#	Since the floppy is already FAT12, when we copy in files, they are correctly stored in the data section,
#	and directory entries are correctly created in the root directory
	mcopy -i $(BUILD_DIR)/main_floppy.img $(BUILD_DIR)/kernel.bin "::kernel.bin"
	mcopy -i $(BUILD_DIR)/main_floppy.img test.txt "::test.txt"

# Bootloader
bootloader: $(BUILD_DIR)/bootloader.bin

$(BUILD_DIR)/bootloader.bin: always
	$(ASM) $(SRC_DIR)/bootloader/boot.asm -f bin -o $(BUILD_DIR)/bootloader.bin

# Kernel
kernel: $(BUILD_DIR)/kernel.bin

$(BUILD_DIR)/kernel.bin: always
	$(ASM) $(SRC_DIR)/kernel/main.asm -f bin -o $(BUILD_DIR)/kernel.bin

# Always
always:
	mkdir -p $(BUILD_DIR)

# Clean
clean:
	rm -rf $(BUILD_DIR)/*

# Run macro
run:
	qemu-system-i386 -fda build/main_floppy.img

# Build tools
tools_fat: $(BUILD_DIR)/tools/fat

$(BUILD_DIR)/tools/fat: always $(TOOLS_DIR)/fat/fat.c
	mkdir -p $(BUILD_DIR)/tools
	gcc -g -o $(BUILD_DIR)/tools/fat $(TOOLS_DIR)/fat/fat.c