#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/shell/shell.h"

#define VERIFY_BUF_SIZE (32u * 1024u)

static uint8_t verify_buf[VERIFY_BUF_SIZE];
static uint8_t dir_check_buf[DIRECT_BLOCKS * BLOCK_SIZE];

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : : "r"(edx));
    // Load eax last so compiler register moves into ebx/ecx/edx do not clobber it
    __asm__ volatile("mov %0, %%eax" : : "r"(eax));
    __asm__ volatile("int $0x30");
}

static void user_puts(const char *s, uint8_t color) {
    uint32_t len = 0;
    while (s[len] != '\0')
        len++;
    syscall(6, (uint32_t) s, len, color);
}

int main(void) {
    char out_buf[256];
    struct ShellIO io = {
        .in      = NULL,
        .in_len  = 0,
        .out     = out_buf,
        .out_len = 0,
        .out_cap = sizeof(out_buf),
    };

    syscall(8, 0, 0, 0);
    user_puts("=== Ring 3 Shell Command Test (cp & rm) ===\n", 0x0B);

    char *cp_argv[] = {"cp", "shell", "copy_file"};
    io.out_len = 0;
    shell_cp(3, cp_argv, &io);
    syscall(6, (uint32_t) io.out, io.out_len, 0x0F);

    struct EXT2DriverRequest check_req = {
        .buf          = verify_buf,
        .name         = "copy_file",
        .name_len     = 9,
        .parent_inode = ROOT_INODE,
        .buffer_size  = sizeof(verify_buf),
        .is_directory = false,
    };
    int8_t retcode = -1;
    syscall(0, (uint32_t) &check_req, (uint32_t) &retcode, 0);
    if (retcode == 0)
        user_puts("[PASS] syscall read verified 'copy_file' exists\n", 0x0A);
    else
        user_puts("[FAIL] syscall read failed on 'copy_file'\n", 0x0C);

    char *rm_argv[] = {"rm", "copy_file"};
    io.out_len = 0;
    shell_rm(2, rm_argv, &io);
    syscall(6, (uint32_t) io.out, io.out_len, 0x0F);

    retcode = 0;
    syscall(0, (uint32_t) &check_req, (uint32_t) &retcode, 0);
    if (retcode != 0)
        user_puts("[PASS] 'copy_file' removed (read retcode != 0)\n", 0x0A);
    else
        user_puts("[FAIL] 'copy_file' still readable after rm\n", 0x0C);

    struct EXT2DriverRequest mkdir_req = {
        .buf          = NULL,
        .name         = "test_dir",
        .name_len     = 8,
        .parent_inode = ROOT_INODE,
        .buffer_size  = 0,
        .is_directory = true,
    };
    int8_t mk_ret = -1;
    syscall(2, (uint32_t) &mkdir_req, (uint32_t) &mk_ret, 0);

    char *cp_dir_argv[] = {"cp", "shell", "test_dir"};
    io.out_len = 0;
    shell_cp(3, cp_dir_argv, &io);
    syscall(6, (uint32_t) io.out, io.out_len, 0x0F);

    char *rm_nonrec_argv[] = {"rm", "test_dir"};
    io.out_len = 0;
    shell_rm(2, rm_nonrec_argv, &io);
    syscall(6, (uint32_t) io.out, io.out_len, 0x0E);

    char *rm_rec_argv[] = {"rm", "-r", "test_dir"};
    io.out_len = 0;
    shell_rm(3, rm_rec_argv, &io);
    syscall(6, (uint32_t) io.out, io.out_len, 0x0F);

    struct EXT2DriverRequest dir_req = {
        .buf          = dir_check_buf,
        .name         = "test_dir",
        .name_len     = 8,
        .parent_inode = ROOT_INODE,
        .buffer_size  = sizeof(dir_check_buf),
        .is_directory = true,
    };
    retcode = 0;
    syscall(1, (uint32_t) &dir_req, (uint32_t) &retcode, 0);
    if (retcode != 0)
        user_puts("[PASS] 'test_dir' recursively deleted\n", 0x0A);
    else
        user_puts("[FAIL] 'test_dir' still exists after rm -r\n", 0x0C);

    syscall(7, 0, 0, 0);

    char c = 0;
    while (true) {
        syscall(4, (uint32_t) &c, 0, 0);
        if (c != 0)
            syscall(5, (uint32_t) c, 0x0F, 0);
    }

    return 0;
}
