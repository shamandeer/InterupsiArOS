#include "header/shell/commands.h"
#include "header/stdlib/string.h"

// prints "mv: <before>'<path>'<after>" in red, followed by a newline
static void mv_error(const char *before, const char *path, const char *after) {
    shell_puts("mv: ", SHELL_COLOR_ERROR);
    shell_puts(before, SHELL_COLOR_ERROR);
    shell_puts("'", SHELL_COLOR_ERROR);
    shell_puts(path, SHELL_COLOR_ERROR);
    shell_puts("'", SHELL_COLOR_ERROR);
    shell_puts(after, SHELL_COLOR_ERROR);
    shell_putchar('\n', SHELL_COLOR_ERROR);
}

static bool mv_is_dot_name(const char *name, uint8_t len) {
    if (len == 1 && name[0] == '.') {
        return true;
    }
    if (len == 2 && name[0] == '.' && name[1] == '.') {
        return true;
    }
    return false;
}

// "/", "//", ... all point to root
static bool mv_is_root_path(const char *path) {
    if (path[0] == '\0') {
        return false;
    }
    for (uint32_t i = 0; path[i] != '\0'; i++) {
        if (path[i] != '/') {
            return false;
        }
    }
    return true;
}

static bool mv_ends_with_slash(const char *path) {
    uint32_t len = shell_strlen(path);
    return len > 0 && path[len - 1] == '/';
}

// out = dir_path + "/" + name, used to show the full target when moving into a folder
static void mv_join_path(char *out, const char *dir_path, const char *name, uint8_t name_len) {
    uint32_t len = shell_strlen(dir_path);
    memcpy(out, dir_path, len);
    if (len == 0 || out[len - 1] != '/') {
        out[len] = '/';
        len++;
    }
    memcpy(out + len, name, name_len);
    out[len + name_len] = '\0';
}

void cmd_mv(int argc, char **argv, struct ShellIO *io) {
    (void) io;
    if (argc < 3) {
        shell_error("mv", NULL, "missing operand");
        return;
    }
    if (argc > 3) {
        shell_error("mv", NULL, "too many arguments");
        return;
    }
    const char *src_path = argv[1];
    const char *dst_path = argv[2];

    // 1. source: parent folder + name
    struct ShellPath src;
    if (mv_is_root_path(src_path)) {
        mv_error("cannot move ", src_path, ": invalid source");
        return;
    }
    if (!shell_resolve_parent(src_path, &src)) {
        mv_error("cannot stat ", src_path, ": no such file or directory");
        return;
    }
    if (mv_is_dot_name(src.name, src.name_len)) {
        mv_error("cannot move ", src_path, ": invalid source");
        return;
    }

    // 2. source type: trailing '/' means folder, otherwise a file wins over a folder with the same name
    bool has_file = false;
    if (!mv_ends_with_slash(src_path)) {
        has_file = shell_dir_find(src.parent_inode, src.name, src.name_len, EXT2_FT_REG_FILE) != 0;
    }
    bool is_directory;
    if (has_file) {
        is_directory = false;
    } else if (shell_dir_find(src.parent_inode, src.name, src.name_len, EXT2_FT_DIR) != 0) {
        is_directory = true;
    } else {
        mv_error("cannot stat ", src_path, ": no such file or directory");
        return;
    }

    struct EXT2DriverRequest src_request = {
        .buf          = NULL,
        .name         = (char *) src.name,
        .name_len     = src.name_len,
        .parent_inode = src.parent_inode,
        .buffer_size  = 0,
        .is_directory = is_directory,
    };

    // 3. destination: an existing folder means "move into it, keep the name", otherwise a new path (rename)
    struct EXT2DriverRequest dst_request = src_request;
    char target[SHELL_LINE_MAX + UINT8_MAX + 2];
    uint32_t dst_dir;
    if (shell_resolve_dir(dst_path, &dst_dir)) {
        dst_request.parent_inode = dst_dir;
        mv_join_path(target, dst_path, src.name, src.name_len);
    } else {
        struct ShellPath dst;
        if (!shell_resolve_parent(dst_path, &dst)) {
            mv_error("invalid target ", dst_path, "");
            return;
        }
        dst_request.parent_inode = dst.parent_inode;
        dst_request.name = (char *) dst.name;
        dst_request.name_len = dst.name_len;
        memcpy(target, dst_path, shell_strlen(dst_path) + 1);
    }

    // 4. kernel does the actual move, success prints nothing
    int8_t code = -1;
    syscall(SYS_FS_MOVE, (uintptr_t) &src_request, (uintptr_t) &dst_request, (uintptr_t) &code);
    switch (code) {
        case 0:
            break;
        case 1:
            mv_error("cannot stat ", src_path, ": no such file or directory");
            break;
        case 2:
        case 5:
            mv_error("invalid target ", dst_path, "");
            break;
        case 3:
            mv_error("target ", target, " already exists");
            break;
        case 4:
            mv_error("cannot move ", src_path, " into itself");
            break;
        case 6:
            mv_error("cannot move ", src_path, ": target directory is full");
            break;
        default:
            mv_error("cannot move ", src_path, "");
            break;
    }
}
