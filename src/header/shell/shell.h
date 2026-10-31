#ifndef _SHELL_H
#define _SHELL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/filesystem/ext2.h"

#define SHELL_LINE_MAX       256
#define SHELL_PATH_MAX       256
#define SHELL_MAX_ARGS       16
#define SHELL_MAX_STAGES     8
#define SHELL_PIPE_BUF_SIZE  0x10000
#define SHELL_FILE_BUF_SIZE  0x10000
#define SHELL_DIR_BUF_SIZE   (DIRECT_BLOCKS * BLOCK_SIZE)

#define SHELL_COLOR_DEFAULT  0xF
#define SHELL_COLOR_ERROR    0xC
#define SHELL_COLOR_USER     0xA
#define SHELL_COLOR_PATH     0x9

enum SyscallNumber {
    SYS_FS_READ           = 0,
    SYS_FS_READ_DIRECTORY = 1,
    SYS_FS_WRITE          = 2,
    SYS_FS_DELETE         = 3,
    SYS_GETCHAR           = 4,
    SYS_PUTCHAR           = 5,
    SYS_PUTS              = 6,
    SYS_KEYBOARD_ACTIVATE = 7,
    SYS_CLEAR_SCREEN      = 8,
    SYS_SET_CURSOR        = 9,
    SYS_EXIT              = 10,
};

struct ShellIO {
    const char *in;     // stdin from the previous pipe stage, NULL if there is none
    uint32_t in_len;
    char *out;
    uint32_t out_len;
    uint32_t out_cap;
};

typedef void (*command_fn)(int argc, char **argv, struct ShellIO *io);

struct ShellStage {
    int argc;
    char *argv[SHELL_MAX_ARGS + 1];
};

struct ShellDirEntry {
    uint32_t inode;
    uint8_t type;
    uint16_t name_len;
    const char *name;
};

// holds the whole directory table, so keep it off other iterators' buffers and on the caller's stack
struct ShellDirIter {
    uint8_t buf[SHELL_DIR_BUF_SIZE];
    uint32_t offset;
};

struct ShellPath {
    uint32_t parent_inode;
    const char *name;
    uint8_t name_len;
};

extern char shell_file_buf[SHELL_FILE_BUF_SIZE];


/* syscall.c */

void syscall(uint32_t eax, uintptr_t ebx, uintptr_t ecx, uintptr_t edx);


/* util.c */

uint32_t shell_strlen(const char *s);
int shell_strcmp(const char *a, const char *b);
void shell_write(const char *data, uint32_t len, uint8_t color);
void shell_puts(const char *s, uint8_t color);
void shell_putchar(char c, uint8_t color);
void shell_error(const char *cmd, const char *arg, const char *msg);
void shell_io_write(struct ShellIO *io, const char *data, uint32_t len);
void shell_io_putc(struct ShellIO *io, char c);
void shell_io_puts(struct ShellIO *io, const char *s);


/* fs.c */

int8_t shell_fs(uint32_t service, struct EXT2DriverRequest *request);
bool shell_dir_open(struct ShellDirIter *it, uint32_t dir_inode);
bool shell_dir_next(struct ShellDirIter *it, struct ShellDirEntry *entry);
uint32_t shell_dir_find(uint32_t dir_inode, const char *name, uint32_t name_len, uint8_t type);
bool shell_resolve_dir(const char *path, uint32_t *inode);
bool shell_resolve_parent(const char *path, struct ShellPath *out);
int8_t shell_read_file(const char *path, char *buf, uint32_t cap);
const char *shell_read_strerror(int8_t code);


/* parser.c */

// returns the stage count (0 for a blank line), or -1 with *error set
int shell_parse(char *line, struct ShellStage *stages, uint32_t max_stages, const char **error);


/* shell.c */

uint32_t shell_cwd_inode(void);
bool shell_set_cwd(uint32_t inode, const char *name);
void shell_print_prompt(void);
void shell_read_line(char *line, uint32_t cap);
void shell_execute_line(char *line);
void shell_run(void);

/* cmd_cat.c */

#endif
