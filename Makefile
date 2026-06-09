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
	dd if=$(BUILD_DIR)/stage1.bin of=$(BUILD_DIR)/main_floppy.img conv=notrunc
#	Since the floppy is already FAT12, when we copy in files, they are correctly stored in the data section,
#	and directory entries are correctly created in the root directory
	mcopy -i $(BUILD_DIR)/main_floppy.img $(BUILD_DIR)/stage2.bin "::stage2.bin"
	mcopy -i $(BUILD_DIR)/main_floppy.img $(BUILD_DIR)/kernel.bin "::kernel.bin"
	mcopy -i $(BUILD_DIR)/main_floppy.img test.txt "::test.txt"

# Bootloader
bootloader: stage1 stage2

stage1: $(BUILD_DIR)/stage1.bin
$(BUILD_DIR)/stage1.bin: always
	$(MAKE) -C $(SRC_DIR)/bootloader/stage1 BUILD_DIR=$(abspath $(BUILD_DIR))

stage2: $(BUILD_DIR)/stage2.bin
$(BUILD_DIR)/stage2.bin: always
	$(MAKE) -C $(SRC_DIR)/bootloader/stage2 BUILD_DIR=$(abspath $(BUILD_DIR))

# Kernel
kernel: $(BUILD_DIR)/kernel.bin
$(BUILD_DIR)/kernel.bin: always
	$(MAKE) -C $(SRC_DIR)/kernel BUILD_DIR=$(abspath $(BUILD_DIR))

# Always
always:
	mkdir -p $(BUILD_DIR)

# Clean
clean:
	$(MAKE) -C $(SRC_DIR)/bootloader/stage1 BUILD_DIR=$(abspath $(BUILD_DIR)) clean
	$(MAKE) -C $(SRC_DIR)/bootloader/stage2 BUILD_DIR=$(abspath $(BUILD_DIR)) clean
	$(MAKE) -C $(SRC_DIR)/kernel BUILD_DIR=$(abspath $(BUILD_DIR)) clean
	rm -rf $(BUILD_DIR)/*

# Run macro
run:
	qemu-system-i386 -fda build/main_floppy.img

# Build tools
tools_fat: $(BUILD_DIR)/tools/fat
$(BUILD_DIR)/tools/fat: always $(TOOLS_DIR)/fat/fat.c
	mkdir -p $(BUILD_DIR)/tools
	gcc -g -o $(BUILD_DIR)/tools/fat $(TOOLS_DIR)/fat/fat.c