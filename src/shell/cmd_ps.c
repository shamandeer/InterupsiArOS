#include "header/shell/commands.h"
#include "header/process/process-info.h"

#define PS_PID_WIDTH  7
#define PS_NAME_WIDTH 17

static const char *const state_names[] = {
    [PROCESS_INFO_STATE_READY]   = "READY",
    [PROCESS_INFO_STATE_WAITING] = "WAITING",
    [PROCESS_INFO_STATE_RUNNING] = "RUNNING",
};

static void pad(struct ShellIO *io, uint32_t written, uint32_t width) {
    do {
        shell_io_putc(io, ' ');
    } while (++written < width);
}

void cmd_ps(int argc, char **argv, struct ShellIO *io) {
    (void) argv;
    if (argc != 1) {
        shell_io_puts(io, "[ps] usage: ps\n");
        return;
    }

    struct ProcessInfo list[PROCESS_INFO_COUNT_MAX];
    uint32_t count = 0;
    syscall(SYS_PROCESS_INFO, (uintptr_t) list, PROCESS_INFO_COUNT_MAX, (uintptr_t) &count);

    shell_io_puts(io, "PID");
    pad(io, 3, PS_PID_WIDTH);
    shell_io_puts(io, "NAME");
    pad(io, 4, PS_NAME_WIDTH);
    shell_io_puts(io, "STATE\n");
    for (uint32_t i = 0; i < count && i < PROCESS_INFO_COUNT_MAX; i++) {
        pad(io, shell_io_putu(io, list[i].pid), PS_PID_WIDTH);

        list[i].name[PROCESS_INFO_NAME_LENGTH - 1] = '\0';
        shell_io_puts(io, list[i].name);
        pad(io, shell_strlen(list[i].name), PS_NAME_WIDTH);

        bool known = list[i].state < sizeof(state_names) / sizeof(state_names[0]);
        shell_io_puts(io, known ? state_names[list[i].state] : "?");
        shell_io_putc(io, '\n');
    }
}
