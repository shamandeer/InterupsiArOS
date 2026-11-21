global _start
extern main

section .text
_start:
    call main
    ; main() return: keluar lewat syscall exit (eax = 10, ebx = return code main)
    mov ebx, eax
    mov eax, 10
    int 0x30
    jmp $
