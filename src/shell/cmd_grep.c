#include "header/shell/commands.h"
#include "header/stdlib/string.h"

static bool contains(const char *line, uint32_t len, const char *pattern, uint32_t pattern_len) {
    for (uint32_t i = 0; i + pattern_len <= len; i++)
        if (memcmp(line + i, pattern, pattern_len) == 0)
            return true;
    return false;
}

void cmd_grep(int argc, char **argv, struct ShellIO *io) {
    if (argc < 2 || argc > 3 || (argc == 2 && io->in == NULL)) {
        shell_error("grep", NULL, "usage: grep PATTERN [FILE]");
        return;
    }

    const char *text = io->in;
    uint32_t len = io->in_len;
    if (argc == 3) {
        int8_t code = shell_read_file(argv[2], shell_file_buf, sizeof(shell_file_buf));
        if (code != 0) {
            shell_error("grep", argv[2], shell_read_strerror(code));
            return;
        }
        text = shell_file_buf;
        len = shell_strlen(shell_file_buf);
    }

    uint32_t pattern_len = shell_strlen(argv[1]);
    for (uint32_t start = 0; start < len;) {
        uint32_t end = start;
        while (end < len && text[end] != '\n')
            end++;
        if (contains(text + start, end - start, argv[1], pattern_len)) {
            shell_io_write(io, text + start, end - start);
            shell_io_putc(io, '\n');
        }
        start = end + 1;
    }
}
