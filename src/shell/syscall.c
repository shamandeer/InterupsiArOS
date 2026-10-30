#include "header/shell/shell.h"

void syscall(uint32_t eax, uintptr_t ebx, uintptr_t ecx, uintptr_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}
