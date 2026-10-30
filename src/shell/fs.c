#include "header/shell/shell.h"
#include "header/stdlib/string.h"

char shell_file_buf[SHELL_FILE_BUF_SIZE];

int8_t shell_fs(uint32_t service, struct EXT2DriverRequest *request) {
    int8_t code = -1;
    syscall(service, (uintptr_t) request, (uintptr_t) &code, 0);
    return code;
}

bool shell_dir_open(struct ShellDirIter *it, uint32_t dir_inode) {
    memset(it->buf, 0, sizeof(it->buf));
    it->offset = 0;
    struct EXT2DriverRequest request = {
        .buf = it->buf,
        .name = ".",
        .name_len = 1,
        .parent_inode = dir_inode,
        .buffer_size = sizeof(it->buf),
        .is_directory = true,
    };
    return shell_fs(SYS_FS_READ_DIRECTORY, &request) == 0;
}

bool shell_dir_next(struct ShellDirIter *it, struct ShellDirEntry *entry) {
    const uint32_t header = sizeof(struct EXT2DirectoryEntry);
    while (it->offset + header <= sizeof(it->buf)) {
        const struct EXT2DirectoryEntry *raw = (const struct EXT2DirectoryEntry *) (it->buf + it->offset);
        if (raw->rec_len == 0 || it->offset + header + raw->name_len > sizeof(it->buf))
            return false;
        const char *name = (const char *) raw + header;
        it->offset += raw->rec_len;
        if (raw->inode == 0)
            continue;
        entry->inode = raw->inode;
        entry->type = raw->file_type;
        entry->name_len = raw->name_len;
        entry->name = name;
        return true;
    }
    return false;
}

uint32_t shell_dir_find(uint32_t dir_inode, const char *name, uint32_t name_len, uint8_t type) {
    struct ShellDirIter it;
    struct ShellDirEntry entry;
    if (name_len == 0 || name_len > UINT8_MAX || !shell_dir_open(&it, dir_inode))
        return 0;
    while (shell_dir_next(&it, &entry))
        if (entry.type == type && entry.name_len == name_len && memcmp(entry.name, name, name_len) == 0)
            return entry.inode;
    return 0;
}

static bool resolve_dir(const char *path, uint32_t len, uint32_t *inode) {
    uint32_t dir = len > 0 && path[0] == '/' ? ROOT_INODE : shell_cwd_inode();
    for (uint32_t i = 0; i < len;) {
        while (i < len && path[i] == '/')
            i++;
        uint32_t start = i;
        while (i < len && path[i] != '/')
            i++;
        if (i == start)
            break;
        dir = shell_dir_find(dir, path + start, i - start, EXT2_FT_DIR);
        if (dir == 0)
            return false;
    }
    *inode = dir;
    return true;
}

bool shell_resolve_dir(const char *path, uint32_t *inode) {
    return resolve_dir(path, shell_strlen(path), inode);
}

bool shell_resolve_parent(const char *path, struct ShellPath *out) {
    uint32_t end = shell_strlen(path);
    while (end > 1 && path[end - 1] == '/')
        end--;
    uint32_t start = end;
    while (start > 0 && path[start - 1] != '/')
        start--;
    uint32_t name_len = end - start;
    if (name_len == 0 || name_len > UINT8_MAX || !resolve_dir(path, start, &out->parent_inode))
        return false;
    out->name = path + start;
    out->name_len = (uint8_t) name_len;
    return true;
}

// buf is zero-filled first, so the file length is shell_strlen(buf) (text files only)
int8_t shell_read_file(const char *path, char *buf, uint32_t cap) {
    struct ShellPath file;
    if (!shell_resolve_parent(path, &file))
        return 3;
    memset(buf, 0, cap);
    struct EXT2DriverRequest request = {
        .buf = buf,
        .name = (char *) file.name,
        .name_len = file.name_len,
        .parent_inode = file.parent_inode,
        .buffer_size = cap - 1,
    };
    return shell_fs(SYS_FS_READ, &request);
}

const char *shell_read_strerror(int8_t code) {
    switch (code) {
        case 1:  return "not a file";
        case 2:  return "file too large";
        case 3:
        case 4:  return "no such file";
        default: return "cannot read file";
    }
}
