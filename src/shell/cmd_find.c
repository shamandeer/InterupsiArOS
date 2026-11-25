#include "header/shell/commands.h"
#include "header/stdlib/string.h"

#define FIND_MAX_DEPTH 32u  // folder levels below root that find still walks into
#define FIND_NAME_MAX  255u

// one folder that is being walked
struct FindFrame {
    uint32_t inode;     // inode of the folder
    uint32_t offset;    // offset of the next entry to read in that folder
    uint32_t path_len;  // length of find_path up to and including this folder's trailing '/'
};

// static on purpose: the user stack is only 32 KiB and one folder buffer is already 6 KiB.
// No recursion, the DFS uses this explicit stack and one shared folder buffer
static struct FindFrame find_stack[FIND_MAX_DEPTH + 1];
static struct ShellDirIter find_it;
// "/" + FIND_MAX_DEPTH folders "name/" + one last name, so it never overflows
static char find_path[1 + (FIND_MAX_DEPTH + 1) * (FIND_NAME_MAX + 1)];

static bool find_is_dot_entry(const struct ShellDirEntry *entry) {
    if (entry->name_len == 1 && entry->name[0] == '.') {
        return true;
    }
    if (entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.') {
        return true;
    }
    return false;
}

void cmd_find(int argc, char **argv, struct ShellIO *io) {
    if (argc < 2) {
        shell_error("find", NULL, "missing operand");
        return;
    }
    if (argc > 2) {
        shell_error("find", NULL, "too many arguments");
        return;
    }
    const char *target = argv[1];
    uint32_t target_len = shell_strlen(target);

    uint32_t found = 0;
    bool depth_exceeded = false;

    // start at root
    find_path[0] = '/';
    find_stack[0].inode = ROOT_INODE;
    find_stack[0].offset = 0;
    find_stack[0].path_len = 1;
    uint32_t depth = 1;

    while (depth > 0) {
        struct FindFrame *frame = &find_stack[depth - 1];
        bool entered_child = false;

        // the buffer is shared by every level: read this folder again and continue where it stopped
        if (shell_dir_open(&find_it, frame->inode)) {
            find_it.offset = frame->offset;
            struct ShellDirEntry entry;
            while (shell_dir_next(&find_it, &entry)) {
                if (find_is_dot_entry(&entry)) {
                    continue;
                }

                // find_path = path of this folder + entry name
                uint32_t name_end = frame->path_len + entry.name_len;
                memcpy(find_path + frame->path_len, entry.name, entry.name_len);
                bool is_dir = entry.type == EXT2_FT_DIR;

                if (entry.name_len == target_len && memcmp(entry.name, target, target_len) == 0) {
                    shell_io_write(io, find_path, name_end);
                    if (is_dir) {
                        shell_io_putc(io, '/');
                    }
                    shell_io_putc(io, '\n');
                    found++;
                }

                if (!is_dir) {
                    continue;
                }
                if (depth > FIND_MAX_DEPTH) {
                    depth_exceeded = true;
                    continue;
                }

                // remember where to continue in this folder, then walk into the child
                frame->offset = find_it.offset;
                find_path[name_end] = '/';
                struct FindFrame *child = &find_stack[depth];
                child->inode = entry.inode;
                child->offset = 0;
                child->path_len = name_end + 1;
                depth++;
                entered_child = true;
                break;
            }
        }

        // folder done (or unreadable), go back up to its parent
        if (!entered_child) {
            depth--;
        }
    }

    if (depth_exceeded) {
        shell_error("find", NULL, "folders deeper than 32 levels are skipped");
    }
    if (found == 0) {
        shell_puts("find: '", SHELL_COLOR_ERROR);
        shell_puts(target, SHELL_COLOR_ERROR);
        shell_puts("' not found\n", SHELL_COLOR_ERROR);
    }
}
