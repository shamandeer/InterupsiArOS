#include "header/shell/commands.h"

static bool is_dot_entry(const struct ShellDirEntry *entry) {
    return entry->name[0] == '.' && (entry->name_len == 1 || (entry->name_len == 2 && entry->name[1] == '.'));
}

void cmd_ls(int argc, char **argv, struct ShellIO *io) {
    if (argc > 2) {
        shell_error("ls", NULL, "too many arguments");
        return;
    }

    const char *path = argc == 2 ? argv[1] : ".";
    uint32_t inode;
    struct ShellDirIter it;
    struct ShellDirEntry entry;
    if (!shell_resolve_dir(path, &inode) || !shell_dir_open(&it, inode)) {
        shell_error("ls", path, "no such directory");
        return;
    }

    while (shell_dir_next(&it, &entry)) {
        if (is_dot_entry(&entry))
            continue;
        shell_io_write(io, entry.name, entry.name_len);
        if (entry.type == EXT2_FT_DIR)
            shell_io_putc(io, '/');
        shell_io_putc(io, '\n');
    }
}
