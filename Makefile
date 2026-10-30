# Compiler & linker
ASM           = nasm

UNAME_S      := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LLVM_PREFIX  := $(shell brew --prefix llvm)
CC            = clang
LIN           = $(shell brew --prefix lld)/bin/ld.lld
II            = mkisofs
ARCH_CFLAG    = --target=i386-elf -march=i386 -mno-mmx -mno-sse -mno-sse2 -mno-avx -msoft-float -Wno-unused-command-line-argument
else
CC            = gcc
LIN           = ld
II            = genisoimage
ARCH_CFLAG    =
endif

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage
QEMU_MEM     ?= 128

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) $(ARCH_CFLAG) -fno-builtin -m32 -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386
IFLAGS        = -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -A os -input-charset utf8 -quiet -boot-info-table


run: all
	@qemu-system-i386 -s -S -m $(QEMU_MEM)M -drive file=$(OUTPUT_FOLDER)/storage.bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso
all: build
build: iso
clean:
	rm -rf $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/*.iso $(OUTPUT_FOLDER)/kernel



disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 8M

# host test for ext2 CRUD, fake disk on RAM (no QEMU needed)
HOST_CC       = cc
TEST_FOLDER   = tests/host
TEST_SOURCE   = $(SOURCE_FOLDER)/filesystem/ext2.c $(TEST_FOLDER)/fake_disk.c $(TEST_FOLDER)/test_ext2.c
HOST_CFLAGS   = -Wall -Wextra -Werror -std=gnu11 -I$(SOURCE_FOLDER)
TEST_CFLAGS   = $(HOST_CFLAGS) -Dread=ext2_read -Dwrite=ext2_write -Ddelete=ext2_delete
test:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(HOST_CC) $(TEST_CFLAGS) $(TEST_SOURCE) -o $(OUTPUT_FOLDER)/test_ext2
	@./$(OUTPUT_FOLDER)/test_ext2 .kit/ch2/sample-image.bin

# host test for swap slot manager & swap page I/O
SWAP_TEST_SOURCE = $(SOURCE_FOLDER)/memory/swap.c $(TEST_FOLDER)/fake_disk.c $(TEST_FOLDER)/test_swap.c
test-swap:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(HOST_CC) $(HOST_CFLAGS) $(SWAP_TEST_SOURCE) -o $(OUTPUT_FOLDER)/test_swap
	@./$(OUTPUT_FOLDER)/test_swap

# host test for shell parser, commands and pipeline, syscalls emulated on top of ext2
SHELL_TEST_SOURCE = $(SOURCE_FOLDER)/filesystem/ext2.c $(filter-out %/syscall.c,$(wildcard $(SOURCE_FOLDER)/shell/*.c)) $(TEST_FOLDER)/fake_disk.c $(TEST_FOLDER)/test_shell.c
test-shell:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(HOST_CC) $(TEST_CFLAGS) $(SHELL_TEST_SOURCE) -o $(OUTPUT_FOLDER)/test_shell
	@./$(OUTPUT_FOLDER)/test_shell

# user shell: flat binary (shell) for the inserter, ELF (shell_elf) for the debugger
SHELL_SOURCES = $(wildcard $(SOURCE_FOLDER)/shell/*.c) $(SOURCE_FOLDER)/user-shell.c $(SOURCE_FOLDER)/stdlib/string.c
SHELL_OBJECTS = $(patsubst $(SOURCE_FOLDER)/%.c,$(OUTPUT_FOLDER)/user/%.o,$(SHELL_SOURCES))
USER_CRT0     = $(OUTPUT_FOLDER)/user/crt0.o
USER_LFLAGS   = -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386

$(USER_CRT0): $(SOURCE_FOLDER)/crt0.s
	@mkdir -p $(dir $@)
	@$(ASM) $(AFLAGS) $< -o $@

$(OUTPUT_FOLDER)/user/%.o: $(SOURCE_FOLDER)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -fno-pie $< -o $@

user-shell: $(USER_CRT0) $(SHELL_OBJECTS)
	@$(LIN) $(USER_LFLAGS) --oformat=binary $^ -o $(OUTPUT_FOLDER)/shell
	@$(LIN) $(USER_LFLAGS) --oformat=elf32-i386 $^ -o $(OUTPUT_FOLDER)/shell_elf
	@rm -rf $(OUTPUT_FOLDER)/user
	@echo Linking user shell, generate flat binary and shell_elf...

kernel:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/cpu/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/string.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/text/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/disk.c -o $(OUTPUT_FOLDER)/disk.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/filesystem/ext2.c -o $(OUTPUT_FOLDER)/ext2.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/memory/paging.c -o $(OUTPUT_FOLDER)/paging.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/memory/swap.c -o $(OUTPUT_FOLDER)/swap.o
	@$(LIN) $(LFLAGS) $(OUTPUT_FOLDER)/*.o -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generate elf32...
	@rm -f $(OUTPUT_FOLDER)/*.o

iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
	@$(II) $(IFLAGS) -o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/