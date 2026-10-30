#include "header/shell/commands.h"

void cmd_cd(int argc, char **argv, struct ShellIO *io) {
    (void) io;
    if (argc > 2) {
        shell_error("cd", NULL, "too many arguments");
        return;
    }

    const char *path = argc == 2 ? argv[1] : "/";
    uint32_t inode;
    if (!shell_resolve_dir(path, &inode))
        shell_error("cd", path, "no such directory");
    else if (!shell_set_cwd(inode, path))
        shell_error("cd", path, "path too long");
}
