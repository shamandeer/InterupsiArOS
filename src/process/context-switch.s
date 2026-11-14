global process_context_switch

process_context_switch: ; context switching low-level code typeshi
    ; TODO : Implement

    ; get address of ctx
    lea ecx, [esp+4]

    ; setup stack iret
    push dword 0x20 | 0x3 ; ss
    push dword [ecx+12] ; user esp
    push dword [ecx+64] ; eflags
    push dword 0x18 | 0x3 ; cs
    push dword [ecx+56] ; eip

    ; load all registers to ctx
    mov edi, [ecx] ; edi
    mov esi, [ecx+4] ; esi
    mov ebp, [ecx+8] ; ebp
    mov esp, [ecx+12] ; esp
    mov ebx, [ecx+16] ; ebx
    mov edx, [ecx+20] ; edx
    
    ; eax dan ecx skip dulu, mau handle 16 bit registers
    mov eax, [ecx+32]
    mov gs, ax ; gs
    mov eax, [ecx+36]
    mov fs, ax ; fs
    mov eax, [ecx+40]
    mov es, ax ; es
    mov eax, [ecx+44]
    mov ds, ax ; ds

    mov eax, [ecx+28] ; eax
    mov ecx, [ecx+24] ; end with ecx

    ; jump ke process
    iret