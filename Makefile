# Detect host OS and setup tool chain
UNAME_S := $(shell uname -s)

# Compiler & linker
ASM           = nasm

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage

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
    HOST_CC          := cc
    HOST_CFLAGS      :=
    USER_LFLAGS      := -T $(SOURCE_FOLDER)/user-linker.ld -m elf_i386
    USER_ELF_OFORMAT :=
    PRINT_SIZE       := stat -f "size: %z bytes (%N)"
else
    # Linux/ WSL
    CC          := gcc
    LIN         := ld
    MKISO       := $(shell which genisoimage 2>/dev/null || echo genisoimage)

    ARCH_CFLAG  := -m32
    LFLAGS      := -T $(SOURCE_FOLDER)/linker.ld -melf_i386
    HOST_CC          := gcc
    HOST_CFLAGS      := -Wno-builtin-declaration-mismatch
    USER_LFLAGS      := -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386
    USER_ELF_OFORMAT := --oformat=elf32-i386
    PRINT_SIZE       := size --target=binary
endif



# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(ARCH_CFLAG) $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf

QEMU_FLAGS    = -rtc base=localtime -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk \
                -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso


run: build
	@qemu-system-i386 -s -S $(QEMU_FLAGS)
run-nogdb: build
	@qemu-system-i386 $(QEMU_FLAGS)
build: iso
clean:
	rm -rf *.o *.iso $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/*.iso $(OUTPUT_FOLDER)/kernel

disk:	
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M
kernel:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/disk.c -o $(OUTPUT_FOLDER)/disk.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/string.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/ext2.c -o $(OUTPUT_FOLDER)/ext2.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/paging.c -o $(OUTPUT_FOLDER)/paging.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/swap.c -o $(OUTPUT_FOLDER)/swap.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/process.c -o $(OUTPUT_FOLDER)/process.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/scheduler.c -o $(OUTPUT_FOLDER)/scheduler.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/context-switch.s -o $(OUTPUT_FOLDER)/context-switch.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cmos.c -o $(OUTPUT_FOLDER)/cmos.o

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

inserter:
	@$(HOST_CC) $(HOST_CFLAGS) -g -I$(SOURCE_FOLDER) \
		$(SOURCE_FOLDER)/stdlib/string.c \
		$(SOURCE_FOLDER)/ext2.c \
		$(SOURCE_FOLDER)/external-inserter.c \
		-o $(OUTPUT_FOLDER)/inserter

user-shell:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC)  $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/user-shell.c -o user-shell.o
	@$(CC)  $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/stdlib/string.c -o string.o
	
	@$(LIN) $(USER_LFLAGS) --oformat=binary \
		crt0.o user-shell.o string.o -o $(OUTPUT_FOLDER)/shell
	@echo Linking object shell object files and generate flat binary...

	@$(LIN) $(USER_LFLAGS) $(USER_ELF_OFORMAT) \
		crt0.o user-shell.o string.o -o $(OUTPUT_FOLDER)/shell_elf
	@echo Linking object shell object files and generate ELF32 for debugging...

	@$(PRINT_SIZE) $(OUTPUT_FOLDER)/shell
	@rm -f *.o

insert-shell: inserter user-shell
	@echo Inserting shell into root directory...
	@cd $(OUTPUT_FOLDER); ./inserter -f shell 1 $(DISK_NAME).bin
