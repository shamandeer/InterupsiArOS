#include "header/shell/shell.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

#define RM_DIR_BUF_SIZE (DIRECT_BLOCKS * BLOCK_SIZE)
#define RM_MAX_DEPTH    32u

static uint8_t rm_dir_buf[RM_DIR_BUF_SIZE];

static uint32_t rm_strlen(const char *s) {
    uint32_t len = 0;
    while (s[len] != '\0')
        len++;
    return len;
}

static bool rm_streq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i])
            return false;
        i++;
    }
    return a[i] == b[i];
}

static bool rm_is_dot_entry(const char *name, uint16_t len) {
    return (len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.');
}

static int8_t rm_fs_read_dir(uint32_t parent_inode, const char *name, uint8_t name_len, uint32_t *out_inode) {
    char name_copy[256];
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    memset(rm_dir_buf, 0, sizeof(rm_dir_buf));

    struct EXT2DriverRequest req = {
        .buf          = rm_dir_buf,
        .name         = name_copy,
        .name_len     = name_len,
        .parent_inode = parent_inode,
        .buffer_size  = sizeof(rm_dir_buf),
        .is_directory = true,
    };
    int8_t retcode = -1;
    syscall(1, (uint32_t) &req, (uint32_t) &retcode, 0);
    if (retcode == 0 && out_inode != NULL) {
        // First entry of block 0 is '.', storing the directory's own inode
        *out_inode = ((struct EXT2DirectoryEntry *) rm_dir_buf)->inode;
    }
    return retcode;
}

static int8_t rm_fs_delete(uint32_t parent_inode, const char *name, uint8_t name_len, bool is_directory) {
    char name_copy[256];
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';

    struct EXT2DriverRequest req = {
        .buf          = NULL,
        .name         = name_copy,
        .name_len     = name_len,
        .parent_inode = parent_inode,
        .buffer_size  = 0,
        .is_directory = is_directory,
    };
    int8_t retcode = -1;
    syscall(3, (uint32_t) &req, (uint32_t) &retcode, 0);
    return retcode;
}

static bool rm_resolve_path(
    const char *path,
    uint32_t *out_parent,
    char *out_name,
    uint8_t *out_name_len,
    bool *out_trailing_slash,
    bool *out_is_root
) {
    if (path == NULL || path[0] == '\0')
        return false;

    uint32_t curr = (path[0] == '/') ? ROOT_INODE : shell_cwd_inode;
    uint32_t end = rm_strlen(path);
    bool trailing = false;

    while (end > 0 && path[end - 1] == '/') {
        trailing = true;
        end--;
    }

    if (end == 0) {
        *out_parent = ROOT_INODE;
        out_name[0] = '.';
        out_name[1] = '\0';
        *out_name_len = 1;
        *out_trailing_slash = true;
        *out_is_root = true;
        return true;
    }

    *out_is_root = false;
    *out_trailing_slash = trailing;

    uint32_t i = 0;
    while (i < end) {
        while (i < end && path[i] == '/')
            i++;
        uint32_t start = i;
        while (i < end && path[i] != '/')
            i++;
        uint32_t comp_len = i - start;
        if (comp_len == 0 || comp_len > 255)
            return false;

        uint32_t next = i;
        while (next < end && path[next] == '/')
            next++;

        if (next < end) {
            uint32_t next_inode = 0;
            if (rm_fs_read_dir(curr, path + start, (uint8_t) comp_len, &next_inode) != 0)
                return false;
            curr = next_inode;
        } else {
            memcpy(out_name, path + start, comp_len);
            out_name[comp_len] = '\0';
            *out_name_len = (uint8_t) comp_len;
            *out_parent = curr;
            return true;
        }
    }
    return false;
}

static bool rm_delete_dir_recursive(uint32_t parent_inode, const char *dir_name, uint8_t dir_name_len, uint32_t depth) {
    if (depth > RM_MAX_DEPTH)
        return false;

    uint32_t dir_inode = 0;
    if (rm_fs_read_dir(parent_inode, dir_name, dir_name_len, &dir_inode) != 0)
        return false;
    if (dir_inode == ROOT_INODE)
        return false;

    while (true) {
        bool found_subdir = false;
        char subdir_name[256];
        uint8_t subdir_len = 0;

        for (uint32_t b = 0; b < DIRECT_BLOCKS; b++) {
            uint8_t *block = rm_dir_buf + b * BLOCK_SIZE;
            uint32_t offset = 0;

            while (offset + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
                struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (block + offset);
                if (entry->rec_len < sizeof(struct EXT2DirectoryEntry) + entry->name_len
                        || (entry->rec_len % 4) != 0
                        || offset + entry->rec_len > BLOCK_SIZE)
                    break;

                if (entry->inode != 0 && entry->name_len > 0 && entry->name_len <= 255) {
                    // Entry name sits immediately past packed 9-byte EXT2DirectoryEntry header
                    const char *ename = (const char *) (block + offset + sizeof(struct EXT2DirectoryEntry));
                    if (!rm_is_dot_entry(ename, entry->name_len)) {
                        if (entry->file_type == EXT2_FT_DIR) {
                            if (!found_subdir) {
                                memcpy(subdir_name, ename, entry->name_len);
                                subdir_name[entry->name_len] = '\0';
                                subdir_len = (uint8_t) entry->name_len;
                                found_subdir = true;
                            }
                        } else {
                            if (rm_fs_delete(dir_inode, ename, (uint8_t) entry->name_len, false) != 0)
                                return false;
                        }
                    }
                }
                offset += entry->rec_len;
            }
        }

        if (!found_subdir)
            break;

        if (!rm_delete_dir_recursive(dir_inode, subdir_name, subdir_len, depth + 1))
            return false;

        if (rm_fs_read_dir(dir_inode, ".", 1, NULL) != 0)
            return false;
    }

    return rm_fs_delete(parent_inode, dir_name, dir_name_len, true) == 0;
}

void shell_rm(int argc, char **argv, struct ShellIO *io) {
    if (io == NULL)
        return;

    if (argv == NULL || argc < 2) {
        shell_io_puts(io, "rm: missing operand\n");
        return;
    }

    bool recursive = false;
    const char *target_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i] == NULL || argv[i][0] == '\0') {
            shell_io_puts(io, "rm: invalid operand\n");
            return;
        }
        if (rm_streq(argv[i], "-r") || rm_streq(argv[i], "-R")) {
            recursive = true;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            shell_io_puts(io, "rm: invalid option\n");
            return;
        } else if (target_path == NULL) {
            target_path = argv[i];
        } else {
            shell_io_puts(io, "rm: too many arguments\n");
            return;
        }
    }

    if (target_path == NULL) {
        shell_io_puts(io, "rm: missing operand\n");
        return;
    }

    uint32_t parent_inode = 0;
    char target_name[256];
    uint8_t target_len = 0;
    bool trailing_slash = false;
    bool is_root = false;

    if (!rm_resolve_path(target_path, &parent_inode, target_name, &target_len, &trailing_slash, &is_root)) {
        shell_io_puts(io, "rm: target not found\n");
        return;
    }
    if (is_root || rm_is_dot_entry(target_name, target_len)) {
        shell_io_puts(io, "rm: cannot remove '/', '.', or '..'\n");
        return;
    }

    if (!recursive) {
        if (!trailing_slash) {
            int8_t fret = rm_fs_delete(parent_inode, target_name, target_len, false);
            if (fret == 0) {
                shell_io_puts(io, "rm: removed '");
                shell_io_puts(io, target_path);
                shell_io_puts(io, "'\n");
                return;
            }
        }

        int8_t dret = rm_fs_delete(parent_inode, target_name, target_len, true);
        if (dret == 0) {
            shell_io_puts(io, "rm: removed '");
            shell_io_puts(io, target_path);
            shell_io_puts(io, "'\n");
            return;
        }
        if (dret == 2) {
            shell_io_puts(io, "rm: directory not empty (use -r)\n");
            return;
        }
        if (trailing_slash && rm_fs_read_dir(parent_inode, target_name, target_len, NULL) == 1) {
            shell_io_puts(io, "rm: not a directory\n");
            return;
        }
        shell_io_puts(io, "rm: target not found\n");
        return;
    }

    uint32_t dir_inode = 0;
    int8_t check_dir = rm_fs_read_dir(parent_inode, target_name, target_len, &dir_inode);
    if (check_dir == 1) {
        if (trailing_slash) {
            shell_io_puts(io, "rm: not a directory\n");
            return;
        }
        if (rm_fs_delete(parent_inode, target_name, target_len, false) == 0) {
            shell_io_puts(io, "rm: removed '");
            shell_io_puts(io, target_path);
            shell_io_puts(io, "'\n");
        } else {
            shell_io_puts(io, "rm: failed to remove file\n");
        }
        return;
    }
    if (check_dir == 2 || check_dir == 3) {
        shell_io_puts(io, "rm: target not found\n");
        return;
    }
    if (check_dir != 0 || dir_inode == ROOT_INODE) {
        shell_io_puts(io, "rm: cannot remove directory\n");
        return;
    }

    if (!rm_delete_dir_recursive(parent_inode, target_name, target_len, 0)) {
        shell_io_puts(io, "rm: failed to remove directory recursively\n");
        return;
    }

    shell_io_puts(io, "rm: removed '");
    shell_io_puts(io, target_path);
    shell_io_puts(io, "'\n");
}
