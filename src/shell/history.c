#include "header/shell/shell.h"
#include "header/stdlib/string.h"

void shell_history_add(struct ShellHistory *h, const char* line, uint32_t *len) {
    if (*len == 0) return;
    uint32_t idx = h->next;
    if (h->count < SHELL_HISTORY_MAX) {
        (h->count)++;
    }
    h->next = (h->next + 1) % SHELL_HISTORY_MAX;
    uint32_t n = (*len < SHELL_LINE_HISTORY_MAX - 1) ? *len : SHELL_LINE_HISTORY_MAX - 1;
    memcpy(h->history[idx], line, n);
    h->history[idx][n] = '\0';
};

uint32_t logical_to_physical_history_index(uint32_t next, uint32_t count, uint32_t pos) {
    uint32_t oldest = (next + SHELL_HISTORY_MAX - count) % SHELL_HISTORY_MAX;
    return (oldest + pos) % SHELL_HISTORY_MAX;
}

const char *get_history_pos(struct ShellHistory *h) {
    uint32_t phys_idx = logical_to_physical_history_index(h->next, h->count, h->pos);
    return h->history[phys_idx];
}

void navigate_up(struct ShellHistory *h, char *line, uint32_t *len) {
    if (h->count == 0 || h->pos == 0) return;
    if (h->is_draft) {
        uint32_t n = *len < sizeof(h->draft) - 1 ? *len : sizeof(h->draft) - 1;
        memcpy(h->draft, line, n);
        h->draft[n] = '\0';
        h->pos = h->count;
        h->is_draft = false;
    }
    (h->pos)--;
    shell_replace_line(line, len, get_history_pos(h));
}

void navigate_down(struct ShellHistory *h, char *line, uint32_t *len) {
    if (h->count == 0 || h->is_draft) return;
    (h->pos)++;
    if (h->pos == h->count) {
        h->is_draft = true;
        shell_replace_line(line, len, h->draft);
    }
    else {
        shell_replace_line(line, len, get_history_pos(h));
    }
}
