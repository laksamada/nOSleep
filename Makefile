# Detect host OS and setup tool chain
UNAME_S := $(shell uname -s)

# Compiler & linker
ASM           = nasm

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025

ifeq ($(UNAME_S), Darwin)
    # macOS 
    LLVM_PREFIX := $(shell brew --prefix llvm 2>/dev/null || echo /opt/homebrew/opt/llvm)
    
    CC          := $(LLVM_PREFIX)/bin/clang
    LIN         := $(shell which ld.lld 2>/dev/null || echo $(LLVM_PREFIX)/bin/ld.lld)
    MKISO       := $(shell which mkisofs 2>/dev/null || echo mkisofs)

    ARCH_CFLAG  := --target=i386-elf \
                   -m32 \
                   -march=i386 \
                   -mno-sse \
                   -mno-sse2 \
                   -mno-mmx \
                   -msoft-float \
                   -mno-avx \
                   -Qunused-arguments 
    LFLAGS      := -T $(SOURCE_FOLDER)/linker.ld -m elf_i386
else
    # Linux/ WSL
    CC          := gcc
    LIN         := ld
    MKISO       := $(shell which genisoimage 2>/dev/null || echo genisoimage)

    ARCH_CFLAG  := -m32
    LFLAGS      := -T $(SOURCE_FOLDER)/linker.ld -melf_i386
endif



# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(ARCH_CFLAG) $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf


run: all
	@qemu-system-i386 -s -S -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso
all: build
build: iso
clean:
	rm -rf *.o *.iso $(OUTPUT_FOLDER)/kernel



kernel:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
    
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o

	@echo Linking object files and generate elf32...
	@$(LIN) $(LFLAGS) bin/*.o -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generate elf32...
	@rm -f *.o
    
iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
	@$(MKISO) -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -boot-info-table -o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/
