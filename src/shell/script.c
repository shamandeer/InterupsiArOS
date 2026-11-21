#include "header/shell/shell.h"
#include "header/stdlib/string.h"


static char script_buf[SHELL_SCRIPT_DEPTH_MAX][SHELL_SCRIPT_BUF_SIZE];
static uint32_t script_depth = 0;

static bool is_space(char c) {
    return c == ' ' || c == '\t';
}

// writes "line <n>: line too long" into msg
static void format_too_long(char *msg, uint32_t line_no) {
    char digits[10];
    uint32_t digit_count = 0;
    uint32_t len = 0;

    do {
        digits[digit_count] = (char) ('0' + line_no % 10);
        digit_count++;
        line_no = line_no / 10;
    } while (line_no > 0);

    memcpy(msg, "line ", 5);
    len = 5;
    while (digit_count > 0) {
        digit_count--;
        msg[len] = digits[digit_count];
        len++;
    }
    memcpy(msg + len, ": line too long", 16);
}


void shell_run_script(const char *path) {
    if (script_depth >= SHELL_SCRIPT_DEPTH_MAX) {
        shell_error("sh", path, "maximum script depth exceeded");
        return;
    }

    char *text = script_buf[script_depth];
    int8_t code = shell_read_file(path, text, SHELL_SCRIPT_BUF_SIZE);
    if (code != 0) {
        shell_error("sh", path, shell_read_strerror(code));
        return;
    }

    script_depth++;

    uint32_t start = 0;
    uint32_t line_no = 0;
    while (text[start] != '\0') {
        uint32_t end = start;
        while (text[end] != '\0' && text[end] != '\n') {
            end++;
        }
        line_no++;

        uint32_t len = end - start;
        if (len > 0 && text[end - 1] == '\r') {
            len--;
        }

        uint32_t first = 0;
        while (first < len && is_space(text[start + first])) {
            first++;
        }

        bool skip = first == len || text[start + first] == '#';
        if (!skip && len > SHELL_LINE_MAX - 1) {
            char msg[48];
            format_too_long(msg, line_no);
            shell_error("sh", path, msg);
            skip = true;
        }

        if (!skip) {
        
            char line[SHELL_LINE_MAX];
            memcpy(line, text + start, len);
            line[len] = '\0';
            shell_execute_line(line);
        }

        if (text[end] == '\0') {
            break;
        }
        start = end + 1;
    }

    script_depth--;
}
