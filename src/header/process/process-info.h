#ifndef _PROCESS_INFO_H
#define _PROCESS_INFO_H

#include <stdint.h>

/**
 * Kontrak syscall process antara kernel dan program user.
 * Sengaja dipisah dari process.h supaya program user tidak ikut meng-include paging/PCB kernel.
 * Kesesuaian nilai dengan process.h dicek _Static_assert di interrupt.c.
 */

#define PROCESS_INFO_COUNT_MAX    16 // = PROCESS_COUNT_MAX
#define PROCESS_INFO_NAME_LENGTH  32 // = PROCESS_NAME_LENGTH_MAX

// Nilai ProcessInfo.state, urutan sama dengan enum PROCESS_STATE
#define PROCESS_INFO_STATE_READY   0
#define PROCESS_INFO_STATE_WAITING 1
#define PROCESS_INFO_STATE_RUNNING 2

// Hasil SYS_KILL
#define PROCESS_KILL_SUCCESS    0
#define PROCESS_KILL_NOT_FOUND  1
#define PROCESS_KILL_FORBIDDEN  2 // PID 0 (init / shell) tidak boleh di-kill

// Disalin kernel ke buffer user oleh SYS_PROCESS_INFO (command ps)
struct ProcessInfo {
    uint32_t pid;
    char     name[PROCESS_INFO_NAME_LENGTH];
    uint8_t  state;
};

#endif
