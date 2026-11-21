#include "header/shell/commands.h"

// indexed by PROCESS_CREATE_* in process.h
static const char *const exec_errors[] = {
    [1] = "jumlah process sudah maksimum",
    [2] = "entrypoint tidak valid",
    [3] = "memori tidak cukup",
    [4] = "gagal membaca file",
};

void cmd_exec(int argc, char **argv, struct ShellIO *io) {
    if (argc != 2) {
        shell_io_puts(io, "[exec] usage: exec <path>\n");
        return;
    }

    struct ShellPath file;
    if (!shell_resolve_parent(argv[1], &file)
            || shell_dir_find(file.parent_inode, file.name, file.name_len, EXT2_FT_REG_FILE) == 0) {
        shell_io_puts(io, "[exec] file '");
        shell_io_puts(io, argv[1]);
        shell_io_puts(io, "' tidak ditemukan\n");
        return;
    }

    struct EXT2DriverRequest request = {
        .name         = (char *) file.name,
        .name_len     = file.name_len,
        .parent_inode = file.parent_inode,
    };
    int32_t retcode = -1;
    uint32_t pid = 0;
    syscall(SYS_EXEC, (uintptr_t) &request, (uintptr_t) &retcode, (uintptr_t) &pid);

    if (retcode == 0) {
        shell_io_puts(io, "[exec] ");
        shell_io_write(io, file.name, file.name_len);
        shell_io_puts(io, " berjalan dengan PID ");
        shell_io_putu(io, pid);
        shell_io_putc(io, '\n');
        return;
    }
    bool known = retcode > 0 && (uint32_t) retcode < sizeof(exec_errors) / sizeof(exec_errors[0]);
    shell_io_puts(io, "[exec] gagal: ");
    shell_io_puts(io, known ? exec_errors[retcode] : "error tidak dikenal");
    shell_io_putc(io, '\n');
}
