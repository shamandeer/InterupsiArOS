#include "header/shell/shell.h"

int main(void) {
    const char message[] = "Hello World!\n";
    syscall(SYS_PUTS, (uintptr_t) message, sizeof(message) - 1, SHELL_COLOR_DEFAULT);
    return 0;
}
