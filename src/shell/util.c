#include "header/shell/shell.h"
#include "header/stdlib/string.h"

uint32_t shell_strlen(const char *s) {
    uint32_t len = 0;
    while (s[len])
        len++;
    return len;
}

int shell_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (uint8_t) *a - (uint8_t) *b;
}

void shell_write(const char *data, uint32_t len, uint8_t color) {
    syscall(SYS_PUTS, (uintptr_t) data, len, color);
}

void shell_puts(const char *s, uint8_t color) {
    shell_write(s, shell_strlen(s), color);
}

void shell_putchar(char c, uint8_t color) {
    syscall(SYS_PUTCHAR, (uint8_t) c, color, 0);
}

void shell_error(const char *cmd, const char *arg, const char *msg) {
    shell_puts(cmd, SHELL_COLOR_ERROR);
    shell_puts(": ", SHELL_COLOR_ERROR);
    if (arg) {
        shell_puts(arg, SHELL_COLOR_ERROR);
        shell_puts(": ", SHELL_COLOR_ERROR);
    }
    shell_puts(msg, SHELL_COLOR_ERROR);
    shell_putchar('\n', SHELL_COLOR_ERROR);
}

// output is truncated when full, out[out_len] always stays '\0'
void shell_io_write(struct ShellIO *io, const char *data, uint32_t len) {
    if (io->out_len + 1 >= io->out_cap)
        return;
    uint32_t room = io->out_cap - io->out_len - 1;
    if (len > room)
        len = room;
    memcpy(io->out + io->out_len, data, len);
    io->out_len += len;
    io->out[io->out_len] = '\0';
}

void shell_io_putc(struct ShellIO *io, char c) {
    shell_io_write(io, &c, 1);
}
