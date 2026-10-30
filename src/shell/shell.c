#include "header/shell/shell.h"
#include "header/shell/commands.h"
#include "header/stdlib/string.h"

struct ShellCommand {
    const char *name;
    command_fn run;
};

static const struct ShellCommand commands[] = {
    {"cd",    cmd_cd},
    {"ls",    cmd_ls},
    {"mkdir", cmd_mkdir},
    {"grep",  cmd_grep},
};

static uint32_t cwd_inode = ROOT_INODE;
static char cwd_path[SHELL_PATH_MAX] = "/";
static char pipe_buf[2][SHELL_PIPE_BUF_SIZE];

uint32_t shell_cwd_inode(void) {
    return cwd_inode;
}

// name is a path, absolute or relative to the current cwd, false if the result does not fit
bool shell_set_cwd(uint32_t inode, const char *name) {
    char next[SHELL_PATH_MAX];
    uint32_t len = 0;

    if (name[0] != '/') {
        len = shell_strlen(cwd_path);
        if (len == 1)
            len = 0;
        memcpy(next, cwd_path, len);
    }
    for (const char *p = name; *p;) {
        while (*p == '/')
            p++;
        const char *part = p;
        while (*p && *p != '/')
            p++;
        uint32_t part_len = (uint32_t) (p - part);

        if (part_len == 0 || (part_len == 1 && part[0] == '.'))
            continue;
        if (part_len == 2 && part[0] == '.' && part[1] == '.') {
            while (len > 0 && next[len - 1] != '/')
                len--;
            if (len > 0)
                len--;
            continue;
        }
        if (len + part_len + 2 > SHELL_PATH_MAX)
            return false;
        next[len++] = '/';
        memcpy(next + len, part, part_len);
        len += part_len;
    }
    if (len == 0)
        next[len++] = '/';
    next[len] = '\0';

    memcpy(cwd_path, next, len + 1);
    cwd_inode = inode;
    return true;
}

void shell_print_prompt(void) {
    shell_puts("InterupsiArOS", SHELL_COLOR_USER);
    shell_puts(":", SHELL_COLOR_DEFAULT);
    shell_puts(cwd_path, SHELL_COLOR_PATH);
    shell_puts("$ ", SHELL_COLOR_DEFAULT);
}

void shell_read_line(char *line, uint32_t cap) {
    uint32_t len = 0;
    while (true) {
        char c = 0;
        syscall(SYS_GETCHAR, (uintptr_t) &c, 0, 0);
        if (c == '\n')
            break;
        if (c == '\b') {
            if (len > 0) {
                len--;
                shell_puts("\b \b", SHELL_COLOR_DEFAULT);
            }
        } else if (c >= ' ' && c <= '~' && len + 1 < cap) {
            line[len++] = c;
            shell_putchar(c, SHELL_COLOR_DEFAULT);
        }
    }
    shell_putchar('\n', SHELL_COLOR_DEFAULT);
    line[len] = '\0';
}

static command_fn find_command(const char *name) {
    for (uint32_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
        if (shell_strcmp(commands[i].name, name) == 0)
            return commands[i].run;
    return NULL;
}

void shell_execute_line(char *line) {
    struct ShellStage stages[SHELL_MAX_STAGES];
    command_fn run[SHELL_MAX_STAGES];
    const char *error;

    int count = shell_parse(line, stages, SHELL_MAX_STAGES, &error);
    if (count < 0) {
        shell_error("sh", NULL, error);
        return;
    }
    for (int i = 0; i < count; i++) {
        run[i] = find_command(stages[i].argv[0]);
        if (run[i] == NULL) {
            shell_error(stages[i].argv[0], NULL, "command not found");
            return;
        }
    }

    struct ShellIO io = {0};
    for (int i = 0; i < count; i++) {
        io.in = i > 0 ? pipe_buf[(i - 1) % 2] : NULL;
        io.in_len = io.out_len;
        io.out = pipe_buf[i % 2];
        io.out_len = 0;
        io.out_cap = SHELL_PIPE_BUF_SIZE;
        io.out[0] = '\0';
        run[i](stages[i].argc, stages[i].argv, &io);
    }

    if (io.out_len > 0) {
        shell_write(io.out, io.out_len, SHELL_COLOR_DEFAULT);
        if (io.out[io.out_len - 1] != '\n')
            shell_putchar('\n', SHELL_COLOR_DEFAULT);
    }
}

void shell_run(void) {
    char line[SHELL_LINE_MAX];

    syscall(SYS_KEYBOARD_ACTIVATE, 0, 0, 0);
    while (true) {
        shell_print_prompt();
        shell_read_line(line, sizeof(line));
        shell_execute_line(line);
    }
}
