#include "header/shell/shell.h"
#include "header/stdlib/string.h"

#define COMPLETE_SCREEN_COLS 80u
#define COMPLETE_NAME_MAX    255u

// result of one pass over the candidates
struct CompleteState {
    const char *prefix;                  // part of the word every candidate must start with
    uint32_t prefix_len;
    bool print;                          // pass 2: print every candidate instead of counting
    uint32_t count;                      // number of candidates found
    char common[COMPLETE_NAME_MAX + 1];  // longest common prefix of all candidates
    uint32_t common_len;
    bool last_is_dir;                    // type of the last candidate, used when count == 1
    uint32_t column;                     // pass 2: current screen column
};

static bool complete_is_dot_entry(const struct ShellDirEntry *entry) {
    if (entry->name_len == 1 && entry->name[0] == '.') {
        return true;
    }
    if (entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.') {
        return true;
    }
    return false;
}

// pass 2: names separated by two spaces, a new row before going past the screen width
static void complete_print(struct CompleteState *st, const char *name, uint32_t name_len, bool is_dir) {
    uint32_t width = name_len;
    if (is_dir) {
        width++;
    }
    if (st->column > 0) {
        if (st->column + 2 + width > COMPLETE_SCREEN_COLS) {
            shell_putchar('\n', SHELL_COLOR_DEFAULT);
            st->column = 0;
        } else {
            shell_puts("  ", SHELL_COLOR_DEFAULT);
            st->column += 2;
        }
    }
    shell_write(name, name_len, SHELL_COLOR_DEFAULT);
    if (is_dir) {
        shell_putchar('/', SHELL_COLOR_DEFAULT);
    }
    st->column += width;
}

// called for every possible name, keeps only the ones that start with the prefix
static void complete_consider(struct CompleteState *st, const char *name, uint32_t name_len, bool is_dir) {
    if (name_len < st->prefix_len || name_len > COMPLETE_NAME_MAX) {
        return;
    }
    if (memcmp(name, st->prefix, st->prefix_len) != 0) {
        return;
    }

    if (st->print) {
        complete_print(st, name, name_len, is_dir);
        return;
    }

    if (st->count == 0) {
        // first candidate: the common prefix is the whole name
        memcpy(st->common, name, name_len);
        st->common_len = name_len;
    } else {
        // shrink the common prefix to the part this name shares with it
        uint32_t same = 0;
        while (same < st->common_len && same < name_len && st->common[same] == name[same]) {
            same++;
        }
        st->common_len = same;
    }
    st->count++;
    st->last_is_dir = is_dir;
}

static void complete_from_commands(struct CompleteState *st) {
    for (uint32_t i = 0; shell_command_name(i) != NULL; i++) {
        const char *name = shell_command_name(i);
        complete_consider(st, name, shell_strlen(name), false);
    }
}

// own function so its 6 KiB iterator only lives on the stack while the folder is read
static void complete_from_folder(struct CompleteState *st, uint32_t dir_inode) {
    struct ShellDirIter it;
    struct ShellDirEntry entry;
    // "." and ".." only when the user already typed a leading dot
    bool want_dots = st->prefix_len > 0 && st->prefix[0] == '.';

    if (!shell_dir_open(&it, dir_inode)) {
        return;
    }
    while (shell_dir_next(&it, &entry)) {
        if (!want_dots && complete_is_dot_entry(&entry)) {
            continue;
        }
        complete_consider(st, entry.name, entry.name_len, entry.type == EXT2_FT_DIR);
    }
}

static void complete_scan(struct CompleteState *st, bool is_command, uint32_t dir_inode) {
    if (is_command) {
        complete_from_commands(st);
    } else {
        complete_from_folder(st, dir_inode);
    }
}

// adds text to the line and echoes the same bytes, so memory and screen stay equal
static void complete_append(char *line, uint32_t *len, const char *text, uint32_t n) {
    if (n == 0) {
        return;
    }
    memcpy(line + *len, text, n);
    *len += n;
    shell_write(text, n, SHELL_COLOR_DEFAULT);
}

void shell_complete(char *line, uint32_t *len, uint32_t cap, bool show_all) {
    if (cap == 0) {
        return;
    }

    // 1. the word being typed: after the last space or '|' up to the end of the line
    uint32_t word_start = *len;
    while (word_start > 0 && line[word_start - 1] != ' ' && line[word_start - 1] != '|') {
        word_start--;
    }
    const char *word = line + word_start;
    uint32_t word_len = *len - word_start;

    // 2. split the word at its last '/': folder part [0, name_start), prefix [name_start, word_len)
    bool has_slash = false;
    uint32_t name_start = 0;
    for (uint32_t i = 0; i < word_len; i++) {
        if (word[i] == '/') {
            has_slash = true;
            name_start = i + 1;
        }
    }

    // 3. first word of a stage (start of line or right after '|') completes a command name
    uint32_t before = word_start;
    while (before > 0 && line[before - 1] == ' ') {
        before--;
    }
    bool first_word = before == 0 || line[before - 1] == '|';
    bool is_command = first_word && !has_slash;

    // 4. folder to read: the typed folder part, or the cwd
    uint32_t dir_inode = shell_cwd_inode();
    if (!is_command && has_slash) {
        char dir[SHELL_LINE_MAX];
        if (name_start >= sizeof(dir)) {
            return;
        }
        memcpy(dir, word, name_start);
        dir[name_start] = '\0';
        if (!shell_resolve_dir(dir, &dir_inode)) {
            return;
        }
    }

    // 5. pass 1: count the candidates and find their common prefix
    struct CompleteState st;
    memset(&st, 0, sizeof(st));
    st.prefix = word + name_start;
    st.prefix_len = word_len - name_start;
    complete_scan(&st, is_command, dir_inode);
    if (st.count == 0) {
        return;
    }

    // 6. complete as far as possible, all or nothing when it does not fit in cap - 1
    uint32_t extra = st.common_len - st.prefix_len;
    const char *rest = st.common + st.prefix_len;
    if (st.count == 1) {
        char suffix = ' ';
        if (st.last_is_dir) {
            suffix = '/';
        }
        if (*len + extra + 1 > cap - 1) {
            return;
        }
        complete_append(line, len, rest, extra);
        complete_append(line, len, &suffix, 1);
        return;
    }
    if (extra > 0) {
        if (*len + extra > cap - 1) {
            return;
        }
        complete_append(line, len, rest, extra);
        return;
    }

    // 7. nothing more to add: second Tab in a row lists every candidate, then redraws the line
    if (!show_all) {
        return;
    }
    shell_putchar('\n', SHELL_COLOR_DEFAULT);
    st.print = true;
    st.column = 0;
    complete_scan(&st, is_command, dir_inode);
    shell_putchar('\n', SHELL_COLOR_DEFAULT);
    shell_print_prompt();
    shell_write(line, *len, SHELL_COLOR_DEFAULT);
}
