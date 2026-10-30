#include "header/shell/shell.h"

static bool is_space(char c) {
    return c == ' ' || c == '\t';
}

// splits in place, quotes are removed; false when there are too many arguments
static bool tokenize(char *s, struct ShellStage *stage) {
    stage->argc = 0;
    while (true) {
        while (is_space(*s))
            s++;
        if (*s == '\0') {
            stage->argv[stage->argc] = NULL;
            return true;
        }
        if (stage->argc == SHELL_MAX_ARGS)
            return false;

        char *w = s, quote = 0;
        stage->argv[stage->argc++] = w;
        for (; *s && (quote || !is_space(*s)); s++) {
            if (quote) {
                if (*s == quote)
                    quote = 0;
                else
                    *w++ = *s;
            } else if (*s == '"' || *s == '\'') {
                quote = *s;
            } else {
                *w++ = *s;
            }
        }
        if (*s)
            s++; // w may alias the separator, so step over it before terminating
        *w = '\0';
    }
}

int shell_parse(char *line, struct ShellStage *stages, uint32_t max_stages, const char **error) {
    uint32_t count = 0;
    char quote = 0;
    char *segment = line;

    for (char *p = line;; p++) {
        if (*p == '"' || *p == '\'') {
            if (!quote)
                quote = *p;
            else if (quote == *p)
                quote = 0;
            continue;
        }
        if (*p != '\0' && (*p != '|' || quote))
            continue;
        if (*p == '\0' && quote) {
            *error = "unterminated quote";
            return -1;
        }

        bool last = *p == '\0';
        *p = '\0';
        if (count == max_stages) {
            *error = "too many commands in pipeline";
            return -1;
        }
        if (!tokenize(segment, &stages[count])) {
            *error = "too many arguments";
            return -1;
        }
        if (stages[count].argc == 0) {
            if (last && count == 0)
                return 0;
            *error = "empty command in pipeline";
            return -1;
        }
        count++;
        if (last)
            return (int) count;
        segment = p + 1;
    }
}
