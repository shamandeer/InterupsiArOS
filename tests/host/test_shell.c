// host tests for the user shell, kernel syscalls are emulated on top of ext2 + fake disk, run with: make test-shell
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "header/shell/shell.h"

void fake_disk_reset(void);

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) passes++; \
    else { fails++; printf("  FAIL %s:%d  %s\n", __func__, __LINE__, #cond); } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    if (strcmp(actual, expected) == 0) passes++; \
    else { fails++; printf("  FAIL %s:%d  got \"%s\" expected \"%s\"\n", __func__, __LINE__, actual, expected); } \
} while (0)

/* ---------- emulated kernel ---------- */

static char screen[1 << 18];
static size_t screen_len;
static const char *keys;

static void screen_append(const char *data, size_t len) {
    memcpy(screen + screen_len, data, len);
    screen_len += len;
    screen[screen_len] = '\0';
}

void syscall(uint32_t eax, uintptr_t ebx, uintptr_t ecx, uintptr_t edx) {
    (void) edx;
    switch (eax) {
        case SYS_FS_READ:
            *(int8_t *) ecx = read(*(struct EXT2DriverRequest *) ebx);
            break;
        case SYS_FS_READ_DIRECTORY:
            *(int8_t *) ecx = read_directory((struct EXT2DriverRequest *) ebx);
            break;
        case SYS_FS_WRITE:
            *(int8_t *) ecx = write((struct EXT2DriverRequest *) ebx);
            break;
        case SYS_FS_DELETE:
            *(int8_t *) ecx = delete(*(struct EXT2DriverRequest *) ebx);
            break;
        case SYS_GETCHAR:
            if (*keys == '\0') {
                printf("  FAIL key script exhausted\n");
                exit(1);
            }
            *(char *) ebx = *keys++;
            break;
        case SYS_PUTCHAR: {
            char c = (char) ebx;
            screen_append(&c, 1);
            break;
        }
        case SYS_PUTS:
            screen_append((const char *) ebx, ecx);
            break;
    }
}

/* ---------- helpers ---------- */

static void reset_shell(void) {
    fake_disk_reset();
    initialize_filesystem_ext2();
    shell_set_cwd(ROOT_INODE, "/");
    screen_len = 0;
    screen[0] = '\0';
}

static const char *run(const char *command) {
    char line[SHELL_LINE_MAX];
    strcpy(line, command);
    screen_len = 0;
    screen[0] = '\0';
    shell_execute_line(line);
    return screen;
}

static const char *prompt(void) {
    screen_len = 0;
    screen[0] = '\0';
    shell_print_prompt();
    return screen;
}

static void put_file(const char *name, const char *content) {
    struct EXT2DriverRequest request = {
        .buf = (void *) content, .name = (char *) name, .name_len = (uint8_t) strlen(name),
        .parent_inode = ROOT_INODE, .buffer_size = (uint32_t) strlen(content),
    };
    write(&request);
}

static int parse(const char *text, struct ShellStage *stages, const char **error) {
    static char line[SHELL_LINE_MAX];
    strcpy(line, text);
    return shell_parse(line, stages, SHELL_MAX_STAGES, error);
}

/* ---------- tests ---------- */

static void test_parser(void) {
    struct ShellStage st[SHELL_MAX_STAGES];
    const char *err = "";

    CHECK(parse("", st, &err) == 0);
    CHECK(parse("   \t ", st, &err) == 0);

    CHECK(parse("ls", st, &err) == 1);
    CHECK(st[0].argc == 1);
    CHECK_STR(st[0].argv[0], "ls");
    CHECK(st[0].argv[1] == NULL);

    CHECK(parse("  grep   foo\tfile.txt ", st, &err) == 1);
    CHECK(st[0].argc == 3);
    CHECK_STR(st[0].argv[1], "foo");
    CHECK_STR(st[0].argv[2], "file.txt");

    CHECK(parse("cat a.txt|grep foo | grep bar", st, &err) == 3);
    CHECK_STR(st[0].argv[0], "cat");
    CHECK_STR(st[1].argv[0], "grep");
    CHECK_STR(st[2].argv[1], "bar");

    CHECK(parse("grep \"two words\" 'it'\"'\"'s' x", st, &err) == 1);
    CHECK(st[0].argc == 4);
    CHECK_STR(st[0].argv[1], "two words");
    CHECK_STR(st[0].argv[2], "it's");
    CHECK_STR(st[0].argv[3], "x");

    CHECK(parse("grep \"a|b\" f | cat", st, &err) == 2);
    CHECK_STR(st[0].argv[1], "a|b");

    CHECK(parse("grep \"\" f", st, &err) == 1);
    CHECK(st[0].argc == 3);
    CHECK_STR(st[0].argv[1], "");

    CHECK(parse("ls |", st, &err) == -1);
    CHECK(parse("| ls", st, &err) == -1);
    CHECK(parse("ls | | cat", st, &err) == -1);
    CHECK(parse("echo \"oops", st, &err) == -1);
    CHECK_STR(err, "unterminated quote");
    CHECK(parse("a b c d e f g h i j k l m n o p q", st, &err) == -1);
    CHECK(parse("a|a|a|a|a|a|a|a|a", st, &err) == -1);
    CHECK(parse("a|a|a|a|a|a|a|a", st, &err) == 8);
}

static void test_dispatch(void) {
    reset_shell();
    CHECK_STR(run(""), "");
    CHECK_STR(run("   "), "");
    CHECK_STR(run("nope"), "nope: command not found\n");
    CHECK_STR(run("ls | nope"), "nope: command not found\n");
    CHECK_STR(run("ls |"), "sh: empty command in pipeline\n");
    CHECK_STR(run("ls"), "");
}

static void test_mkdir_ls_cd(void) {
    reset_shell();
    CHECK_STR(prompt(), "InterupsiArOS:/$ ");

    CHECK_STR(run("mkdir docs"), "");
    CHECK_STR(run("mkdir docs"), "mkdir: docs: already exists\n");
    CHECK_STR(run("mkdir"), "mkdir: missing operand\n");
    CHECK_STR(run("mkdir a b"), "");
    CHECK_STR(run("ls"), "docs/\na/\nb/\n");

    CHECK_STR(run("cd docs"), "");
    CHECK_STR(prompt(), "InterupsiArOS:/docs$ ");
    CHECK_STR(run("ls"), "");
    CHECK_STR(run("mkdir sub"), "");
    CHECK_STR(run("mkdir sub/deep"), "");
    CHECK_STR(run("ls"), "sub/\n");
    CHECK_STR(run("ls sub"), "deep/\n");
    CHECK_STR(run("ls /"), "docs/\na/\nb/\n");

    CHECK_STR(run("cd sub/deep"), "");
    CHECK_STR(prompt(), "InterupsiArOS:/docs/sub/deep$ ");
    CHECK_STR(run("cd .."), "");
    CHECK_STR(prompt(), "InterupsiArOS:/docs/sub$ ");
    CHECK_STR(run("cd ../.."), "");
    CHECK_STR(prompt(), "InterupsiArOS:/$ ");
    CHECK_STR(run("cd .."), "");
    CHECK_STR(prompt(), "InterupsiArOS:/$ ");

    CHECK_STR(run("cd docs/sub"), "");
    CHECK_STR(run("cd /a"), "");
    CHECK_STR(prompt(), "InterupsiArOS:/a$ ");
    CHECK_STR(run("cd"), "");
    CHECK_STR(prompt(), "InterupsiArOS:/$ ");

    CHECK_STR(run("cd docs/"), "");
    CHECK_STR(run("cd ./sub/../sub"), "");
    CHECK_STR(prompt(), "InterupsiArOS:/docs/sub$ ");
    CHECK_STR(run("mkdir ../up"), "");
    CHECK_STR(run("ls .."), "sub/\nup/\n");

    CHECK_STR(run("cd missing"), "cd: missing: no such directory\n");
    CHECK_STR(prompt(), "InterupsiArOS:/docs/sub$ ");
    CHECK_STR(run("cd a b"), "cd: too many arguments\n");
    CHECK_STR(run("ls nope"), "ls: nope: no such directory\n");
    CHECK_STR(run("mkdir /"), "mkdir: /: invalid path\n");
    CHECK_STR(run("mkdir nope/x"), "mkdir: nope/x: invalid path\n");
}

static void test_cd_file_is_rejected(void) {
    reset_shell();
    put_file("note.txt", "hello\n");
    CHECK_STR(run("cd note.txt"), "cd: note.txt: no such directory\n");
    CHECK_STR(prompt(), "InterupsiArOS:/$ ");
}

static void test_grep_and_pipeline(void) {
    reset_shell();
    put_file("a.txt", "alpha\nbeta\ngamma\nalphabet\nlast without newline");
    CHECK_STR(run("mkdir bdir"), "");
    CHECK_STR(run("mkdir other"), "");

    CHECK_STR(run("grep alpha a.txt"), "alpha\nalphabet\n");
    CHECK_STR(run("grep \"without newline\" a.txt"), "last without newline\n");
    CHECK_STR(run("grep zzz a.txt"), "");
    CHECK_STR(run("grep a /a.txt"), "alpha\nbeta\ngamma\nalphabet\nlast without newline\n");
    CHECK_STR(run("grep alpha nofile"), "grep: nofile: no such file\n");
    CHECK_STR(run("grep alpha bdir"), "grep: bdir: not a file\n");
    CHECK_STR(run("grep"), "grep: usage: grep PATTERN [FILE]\n");
    CHECK_STR(run("grep alpha"), "grep: usage: grep PATTERN [FILE]\n");

    CHECK_STR(run("ls | grep dir"), "bdir/\n");
    CHECK_STR(run("ls | grep t | grep o"), "other/\n");
    CHECK_STR(run("ls | grep t | grep t | grep t | grep o"), "other/\n");
    CHECK_STR(run("ls | grep \"\""), "a.txt\nbdir/\nother/\n");
    CHECK_STR(run("ls | grep nomatch | grep a"), "");
    CHECK_STR(run("ls | grep a | grep alpha a.txt"), "alpha\nalphabet\n");
}

static void test_cp_rm(void) {
    reset_shell();
    put_file("a.txt", "alpha\nbeta\n");
    CHECK_STR(run("mkdir d"), "");

    CHECK_STR(run("cp a.txt b.txt"), "cp: copied 'a.txt' to 'b.txt'\n");
    CHECK_STR(run("grep beta b.txt"), "beta\n");
    CHECK_STR(run("cp a.txt d"), "cp: copied 'a.txt' to 'd'\n");
    CHECK_STR(run("grep alpha d/a.txt"), "alpha\n");
    CHECK_STR(run("cp a.txt b.txt"), "cp: destination already exists\n");
    CHECK_STR(run("cp nofile x"), "cp: source file not found\n");
    CHECK_STR(run("cp a.txt"), "cp: missing file operand\n");

    CHECK_STR(run("rm b.txt"), "rm: removed 'b.txt'\n");
    CHECK_STR(run("grep beta b.txt"), "grep: b.txt: no such file\n");
    CHECK_STR(run("rm d"), "rm: directory not empty (use -r)\n");
    CHECK_STR(run("rm -r d"), "rm: removed 'd'\n");
    CHECK_STR(run("ls"), "a.txt\n");
    CHECK_STR(run("rm"), "rm: missing operand\n");
    CHECK_STR(run("rm nofile"), "rm: target not found\n");
}

static void test_large_file(void) {
    reset_shell();
    static char big[40000], expected[128];
    for (size_t i = 0; i < sizeof(big) - 1; i++)
        big[i] = i % 100 == 99 ? '\n' : 'x';
    memcpy(big + 300 * 100, "NEEDLE", 6);
    big[sizeof(big) - 1] = '\0';
    put_file("big.txt", big);

    memcpy(expected, big + 300 * 100, 100);
    expected[100] = '\0';
    CHECK_STR(run("grep NEEDLE big.txt"), expected);
}

static void test_read_line(void) {
    char line[SHELL_LINE_MAX];

    keys = "ls\n";
    screen_len = 0;
    shell_read_line(line, sizeof(line));
    CHECK_STR(line, "ls");
    CHECK_STR(screen, "ls\n");

    keys = "lx\bs -l\n";
    screen_len = 0;
    shell_read_line(line, sizeof(line));
    CHECK_STR(line, "ls -l");
    CHECK_STR(screen, "lx\b \bs -l\n");

    keys = "\b\b\n";
    screen_len = 0;
    shell_read_line(line, sizeof(line));
    CHECK_STR(line, "");
    CHECK_STR(screen, "\n");

    keys = "abcdefgh\n";
    shell_read_line(line, 4);
    CHECK_STR(line, "abc");
}

static void test_cwd_overflow(void) {
    reset_shell();
    char name[200], expected[256];
    memset(name, 'd', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    snprintf(expected, sizeof(expected), "InterupsiArOS:/%s$ ", name);

    CHECK(shell_set_cwd(ROOT_INODE, name));
    CHECK(!shell_set_cwd(ROOT_INODE, name));
    CHECK_STR(prompt(), expected);
}

static void test_script(void) {
    // 1. basic script: mkdir + ls
    reset_shell();
    put_file("setup.sh", "mkdir one\nmkdir two\nls\n");
    CHECK_STR(run("sh setup.sh"), "setup.sh\none/\ntwo/\n");

    // 2. ./name.sh and /name.sh give the same result as sh name.sh
    reset_shell();
    put_file("setup.sh", "mkdir one\nmkdir two\nls\n");
    CHECK_STR(run("./setup.sh"), "setup.sh\none/\ntwo/\n");
    reset_shell();
    put_file("setup.sh", "mkdir one\nmkdir two\nls\n");
    CHECK_STR(run("/setup.sh"), "setup.sh\none/\ntwo/\n");

    // 3. comments, shebang, blank lines, whitespace-only lines and CRLF are handled
    reset_shell();
    put_file("c.sh", "#!/bin/sh\n# a comment\n\n   \t \n  # indented comment\nmkdir x\r\n\r\nls\r\n");
    CHECK_STR(run("sh c.sh"), "c.sh\nx/\n");

    // 4. cd inside the script changes the shell's cwd after it ends
    reset_shell();
    put_file("go.sh", "mkdir docs\ncd docs\nmkdir inside\n");
    CHECK_STR(run("sh go.sh"), "");
    CHECK_STR(prompt(), "InterupsiArOS:/docs$ ");
    CHECK_STR(run("ls"), "inside/\n");
    // the script path resolves from the cwd, and its lines run relative to the cwd at that moment
    CHECK_STR(run("sh ../go.sh"), "");
    CHECK_STR(prompt(), "InterupsiArOS:/docs/docs$ ");

    // 5. pipeline inside a script
    reset_shell();
    put_file("a.txt", "alpha\nbeta\nalphabet\n");
    put_file("p.sh", "cat a.txt | grep alpha\nls | grep txt\n");
    CHECK_STR(run("sh p.sh"), "alpha\nalphabet\na.txt\n");

    // 6. an unknown command shows its error and the script goes on
    reset_shell();
    put_file("e.sh", "mkdir before\nnope arg\nmkdir after\nls\n");
    CHECK_STR(run("sh e.sh"), "nope: command not found\ne.sh\nbefore/\nafter/\n");

    // 7. a script calling another script
    reset_shell();
    put_file("outer.sh", "mkdir o1\nsh inner.sh\n./inner2.sh\nls\n");
    put_file("inner.sh", "mkdir i1\n");
    put_file("inner2.sh", "mkdir i2\n");
    CHECK_STR(run("sh outer.sh"), "outer.sh\ninner.sh\ninner2.sh\no1/\ni1/\ni2/\n");

    // 8. a script calling itself stops at the depth limit instead of hanging
    reset_shell();
    put_file("self.sh", "sh self.sh\n");
    CHECK_STR(run("sh self.sh"), "sh: self.sh: maximum script depth exceeded\n");
    // depth counter is back to 0 afterwards, so another script still runs
    put_file("ok.sh", "mkdir ok\n");
    CHECK_STR(run("sh ok.sh"), "");
    CHECK_STR(run("ls ok"), "");

    // 9. error cases
    reset_shell();
    put_file("a.sh", "mkdir ran\n");
    CHECK_STR(run("mkdir d"), "");
    CHECK_STR(run("sh nope.sh"), "sh: nope.sh: no such file\n");
    CHECK_STR(run("./nope.sh"), "sh: ./nope.sh: no such file\n");
    CHECK_STR(run("sh d"), "sh: d: not a file\n");
    CHECK_STR(run("sh"), "sh: missing operand\n");
    CHECK_STR(run("sh a.sh | grep x"), "sh: cannot be used in a pipeline\n");
    CHECK_STR(run("ls | ./a.sh"), "sh: cannot be used in a pipeline\n");
    CHECK_STR(run("ls | sh a.sh | grep x"), "sh: cannot be used in a pipeline\n");
    CHECK_STR(run("ls"), "a.sh\nd/\n");

    // line longer than SHELL_LINE_MAX - 1 is reported and skipped, the rest still runs
    static char long_script[SHELL_LINE_MAX + 64];
    strcpy(long_script, "mkdir first\nmkdir ");
    size_t used = strlen(long_script);
    memset(long_script + used, 'z', SHELL_LINE_MAX);
    strcpy(long_script + used + SHELL_LINE_MAX, "\nmkdir last\n");
    put_file("long.sh", long_script);
    CHECK_STR(run("sh long.sh"), "sh: long.sh: line 2: line too long\n");
    CHECK_STR(run("ls"), "a.sh\nd/\nlong.sh\nfirst/\nlast/\n");

    // a line of exactly SHELL_LINE_MAX - 1 characters still runs
    char exact[SHELL_LINE_MAX + 8];
    memset(exact, ' ', SHELL_LINE_MAX - 1);
    memcpy(exact + SHELL_LINE_MAX - 1 - 2, "ls", 2);
    strcpy(exact + SHELL_LINE_MAX - 1, "\n");
    put_file("exact.sh", exact);
    CHECK_STR(run("sh exact.sh"), "a.sh\nd/\nlong.sh\nfirst/\nlast/\nexact.sh\n");

    // 10. cat inside a script does not overwrite the rest of the script
    reset_shell();
    static char text[2000];
    memset(text, 'q', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    put_file("q.txt", text);
    put_file("cat.sh", "cat q.txt\nmkdir after\nls after\ngrep q q.txt | grep zzz\nmkdir end\n");
    char expected[sizeof(text) + 8];
    snprintf(expected, sizeof(expected), "%s\n", text);
    CHECK_STR(run("sh cat.sh"), expected);
    CHECK_STR(run("ls"), "q.txt\ncat.sh\nafter/\nend/\n");
}

int main(void) {
    test_parser();
    test_dispatch();
    test_mkdir_ls_cd();
    test_cd_file_is_rejected();
    test_grep_and_pipeline();
    test_cp_rm();
    test_large_file();
    test_read_line();
    test_cwd_overflow();
    test_script();
    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}
