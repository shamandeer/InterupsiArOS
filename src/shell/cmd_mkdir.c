#include "header/shell/commands.h"

void cmd_mkdir(int argc, char **argv, struct ShellIO *io) {
    (void) io;
    if (argc < 2) {
        shell_error("mkdir", NULL, "missing operand");
        return;
    }

    for (int i = 1; i < argc; i++) {
        struct ShellPath dir;
        if (!shell_resolve_parent(argv[i], &dir)) {
            shell_error("mkdir", argv[i], "invalid path");
            continue;
        }

        struct EXT2DriverRequest request = {
            .buf = NULL,
            .name = (char *) dir.name,
            .name_len = dir.name_len,
            .parent_inode = dir.parent_inode,
            .buffer_size = 0,
            .is_directory = true,
        };
        int8_t code = shell_fs(SYS_FS_WRITE, &request);
        if (code == 1)
            shell_error("mkdir", argv[i], "already exists");
        else if (code != 0)
            shell_error("mkdir", argv[i], "cannot create directory");
    }
}
