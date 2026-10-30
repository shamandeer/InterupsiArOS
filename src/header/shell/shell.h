#ifndef _SHELL_H
#define _SHELL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct ShellIO {
    const char *in;
    uint32_t in_len;
    char *out;
    uint32_t out_len;
    uint32_t out_cap;
};

extern uint32_t shell_cwd_inode;

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

void shell_cp(int argc, char **argv, struct ShellIO *io);
void shell_rm(int argc, char **argv, struct ShellIO *io);

static inline void shell_io_write(struct ShellIO *io, const char *data, uint32_t len) {
    if (io == NULL || io->out == NULL || data == NULL || io->out_len >= io->out_cap)
        return;
    uint32_t avail = io->out_cap - io->out_len;
    uint32_t count = (len < avail) ? len : avail;
    for (uint32_t i = 0; i < count; i++)
        io->out[io->out_len + i] = data[i];
    io->out_len += count;
    if (io->out_len < io->out_cap)
        io->out[io->out_len] = '\0';
}

static inline void shell_io_puts(struct ShellIO *io, const char *str) {
    if (str == NULL)
        return;
    uint32_t len = 0;
    while (str[len] != '\0')
        len++;
    shell_io_write(io, str, len);
}

#endif
