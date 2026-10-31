#include "header/shell/commands.h"
#include "header/shell/shell.h"

void cmd_cat(int argc, char **argv, struct ShellIO *io) {
    if (argc < 2) {
        if (io->in) {
            shell_io_puts(io, io->in);
        }
        else {
            shell_error("cat", NULL, "missing operand");
        }
        return;
    }

    const char *path = argv[1];
    int8_t code = shell_read_file(argv[1], shell_file_buf, sizeof(shell_file_buf));
    if (code) {
        shell_error("cat", argv[1], shell_read_strerror(code));
        return;
    }
    shell_io_puts(io, shell_file_buf);
}