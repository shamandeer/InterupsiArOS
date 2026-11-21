#include "header/shell/commands.h"
#include "header/process/process-info.h"

// false for an empty string, non-digits or a value above UINT32_MAX
static bool parse_pid(const char *s, uint32_t *out) {
    uint32_t value = 0;
    if (*s == '\0')
        return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return false;
        uint32_t digit = (uint32_t) (*s - '0');
        if (value > (UINT32_MAX - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

void cmd_kill(int argc, char **argv, struct ShellIO *io) {
    uint32_t pid;
    if (argc != 2) {
        shell_io_puts(io, "[kill] usage: kill <pid>\n");
        return;
    }
    if (!parse_pid(argv[1], &pid)) {
        shell_io_puts(io, "[kill] PID '");
        shell_io_puts(io, argv[1]);
        shell_io_puts(io, "' tidak valid\n");
        return;
    }

    int8_t result = -1;
    syscall(SYS_KILL, pid, (uintptr_t) &result, 0);

    if (result == PROCESS_KILL_FORBIDDEN) {
        shell_io_puts(io, "[kill] tidak bisa menghentikan init process\n");
        return;
    }
    shell_io_puts(io, "[kill] PID ");
    shell_io_putu(io, pid);
    if (result == PROCESS_KILL_SUCCESS)
        shell_io_puts(io, " dihentikan\n");
    else if (result == PROCESS_KILL_NOT_FOUND)
        shell_io_puts(io, " tidak ditemukan\n");
    else
        shell_io_puts(io, " gagal dihentikan\n");
}
