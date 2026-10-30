#include "header/shell/shell.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

#define CP_MAX_FILE_SIZE (64u * 1024u)
#define CP_DIR_BUF_SIZE  (DIRECT_BLOCKS * BLOCK_SIZE)

__attribute__((weak)) uint32_t shell_cwd_inode = ROOT_INODE;

static uint8_t cp_file_buf[CP_MAX_FILE_SIZE];
static uint8_t cp_dir_buf[CP_DIR_BUF_SIZE];

static uint32_t cp_strlen(const char *s) {
    uint32_t len = 0;
    while (s[len] != '\0')
        len++;
    return len;
}

static bool cp_is_dot_entry(const char *name, uint8_t len) {
    return (len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.');
}

static int8_t cp_fs_read(uint32_t parent_inode, const char *name, uint8_t name_len, void *buf, uint32_t buffer_size) {
    char name_copy[256];
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';

    struct EXT2DriverRequest req = {
        .buf          = buf,
        .name         = name_copy,
        .name_len     = name_len,
        .parent_inode = parent_inode,
        .buffer_size  = buffer_size,
        .is_directory = false,
    };
    int8_t retcode = -1;
    syscall(0, (uint32_t) &req, (uint32_t) &retcode, 0);
    return retcode;
}

static int8_t cp_fs_read_dir(uint32_t parent_inode, const char *name, uint8_t name_len, uint32_t *out_inode) {
    char name_copy[256];
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    memset(cp_dir_buf, 0, sizeof(cp_dir_buf));

    struct EXT2DriverRequest req = {
        .buf          = cp_dir_buf,
        .name         = name_copy,
        .name_len     = name_len,
        .parent_inode = parent_inode,
        .buffer_size  = sizeof(cp_dir_buf),
        .is_directory = true,
    };
    int8_t retcode = -1;
    syscall(1, (uint32_t) &req, (uint32_t) &retcode, 0);
    if (retcode == 0 && out_inode != NULL) {
        // First entry of block 0 is always '.', holding the directory's own inode
        *out_inode = ((struct EXT2DirectoryEntry *) cp_dir_buf)->inode;
    }
    return retcode;
}

static int8_t cp_fs_write(uint32_t parent_inode, const char *name, uint8_t name_len, void *buf, uint32_t size) {
    char name_copy[256];
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';

    struct EXT2DriverRequest req = {
        .buf          = buf,
        .name         = name_copy,
        .name_len     = name_len,
        .parent_inode = parent_inode,
        .buffer_size  = size,
        .is_directory = false,
    };
    int8_t retcode = -1;
    syscall(2, (uint32_t) &req, (uint32_t) &retcode, 0);
    return retcode;
}

static bool cp_resolve_path(
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
    uint32_t end = cp_strlen(path);
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
            if (cp_fs_read_dir(curr, path + start, (uint8_t) comp_len, &next_inode) != 0)
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

static int8_t cp_read_exact_file(uint32_t parent_inode, const char *name, uint8_t name_len, uint32_t *out_size) {
    int8_t ret = cp_fs_read(parent_inode, name, name_len, cp_file_buf, CP_MAX_FILE_SIZE);
    if (ret != 0)
        return ret;

    if (cp_fs_read(parent_inode, name, name_len, cp_file_buf, 0) == 0) {
        *out_size = 0;
        return 0;
    }

    uint32_t lo = 1;
    uint32_t hi = BLOCK_SIZE;
    while (hi < CP_MAX_FILE_SIZE) {
        if (cp_fs_read(parent_inode, name, name_len, cp_file_buf, hi) == 0)
            break;
        lo = hi + 1;
        hi = (hi * 2 > CP_MAX_FILE_SIZE) ? CP_MAX_FILE_SIZE : hi * 2;
    }

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (cp_fs_read(parent_inode, name, name_len, cp_file_buf, mid) == 0)
            hi = mid;
        else
            lo = mid + 1;
    }

    *out_size = lo;
    return 0;
}

void shell_cp(int argc, char **argv, struct ShellIO *io) {
    if (io == NULL)
        return;

    if (argv == NULL || argc < 3 || argv[1] == NULL || argv[2] == NULL) {
        shell_io_puts(io, "cp: missing file operand\n");
        return;
    }
    if (argc > 3) {
        shell_io_puts(io, "cp: too many arguments\n");
        return;
    }

    uint32_t src_parent = 0;
    char src_name[256];
    uint8_t src_len = 0;
    bool src_trailing = false;
    bool src_is_root = false;

    if (!cp_resolve_path(argv[1], &src_parent, src_name, &src_len, &src_trailing, &src_is_root)) {
        shell_io_puts(io, "cp: source file not found\n");
        return;
    }
    if (src_is_root || src_trailing || cp_is_dot_entry(src_name, src_len)) {
        shell_io_puts(io, "cp: source is a directory\n");
        return;
    }

    uint32_t file_size = 0;
    int8_t rret = cp_read_exact_file(src_parent, src_name, src_len, &file_size);
    if (rret == 3 || rret == 4) {
        shell_io_puts(io, "cp: source file not found\n");
        return;
    }
    if (rret == 1) {
        shell_io_puts(io, "cp: source is a directory\n");
        return;
    }
    if (rret == 2) {
        shell_io_puts(io, "cp: insufficient buffer capacity\n");
        return;
    }
    if (rret != 0) {
        shell_io_puts(io, "cp: failed to read source file\n");
        return;
    }
    if (file_size == 0) {
        shell_io_puts(io, "cp: cannot copy empty file\n");
        return;
    }

    uint32_t dst_parent = 0;
    char dst_name[256];
    uint8_t dst_len = 0;
    bool dst_trailing = false;
    bool dst_is_root = false;

    if (!cp_resolve_path(argv[2], &dst_parent, dst_name, &dst_len, &dst_trailing, &dst_is_root)) {
        shell_io_puts(io, "cp: destination directory not found\n");
        return;
    }

    uint32_t target_parent = 0;
    char target_name[256];
    uint8_t target_len = 0;

    if (dst_is_root) {
        target_parent = ROOT_INODE;
        memcpy(target_name, src_name, src_len + 1);
        target_len = src_len;
    } else {
        uint32_t existing_dir_inode = 0;
        int8_t dret = cp_fs_read_dir(dst_parent, dst_name, dst_len, &existing_dir_inode);
        if (dret == 0) {
            target_parent = existing_dir_inode;
            memcpy(target_name, src_name, src_len + 1);
            target_len = src_len;
        } else if (dst_trailing || cp_is_dot_entry(dst_name, dst_len)) {
            if (dret == 1)
                shell_io_puts(io, "cp: destination is not a directory\n");
            else
                shell_io_puts(io, "cp: destination directory not found\n");
            return;
        } else {
            target_parent = dst_parent;
            memcpy(target_name, dst_name, dst_len + 1);
            target_len = dst_len;
        }
    }

    if (cp_fs_read_dir(target_parent, target_name, target_len, NULL) == 0) {
        shell_io_puts(io, "cp: destination already exists\n");
        return;
    }

    int8_t wret = cp_fs_write(target_parent, target_name, target_len, cp_file_buf, file_size);
    if (wret == 1) {
        shell_io_puts(io, "cp: destination already exists\n");
        return;
    }
    if (wret == 2) {
        shell_io_puts(io, "cp: invalid destination directory\n");
        return;
    }
    if (wret != 0) {
        shell_io_puts(io, "cp: failed to write destination file\n");
        return;
    }

    shell_io_puts(io, "cp: copied '");
    shell_io_puts(io, argv[1]);
    shell_io_puts(io, "' to '");
    shell_io_puts(io, argv[2]);
    shell_io_puts(io, "'\n");
}
