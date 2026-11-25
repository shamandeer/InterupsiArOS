// host tests for the user shell, kernel syscalls are emulated on top of ext2 + fake disk, run with: make test-shell
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "header/shell/shell.h"
#include "header/process/process-info.h"

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

// emulated process table: shell is PID 0 and RUNNING, exec appends READY processes
static struct ProcessInfo procs[PROCESS_INFO_COUNT_MAX];
static uint32_t proc_count, next_pid;

static void reset_processes(void) {
    memset(procs, 0, sizeof(procs));
    strcpy(procs[0].name, "shell");
    procs[0].state = PROCESS_INFO_STATE_RUNNING;
    proc_count = 1;
    next_pid = 1;
}

static void emulate_exec(const struct EXT2DriverRequest *request, int32_t *retcode, uint32_t *pid) {
    if (proc_count == PROCESS_INFO_COUNT_MAX) {
        *retcode = 1;
        return;
    }
    struct ProcessInfo *p = &procs[proc_count++];
    memset(p, 0, sizeof(*p));
    memcpy(p->name, request->name, request->name_len);
    p->pid = next_pid++;
    p->state = PROCESS_INFO_STATE_READY;
    *pid = p->pid;
    *retcode = 0;
}

static int8_t emulate_kill(uint32_t pid) {
    if (pid == 0)
        return PROCESS_KILL_FORBIDDEN;
    for (uint32_t i = 0; i < proc_count; i++) {
        if (procs[i].pid == pid) {
            memmove(&procs[i], &procs[i + 1], (proc_count - i - 1) * sizeof(procs[0]));
            proc_count--;
            return PROCESS_KILL_SUCCESS;
        }
    }
    return PROCESS_KILL_NOT_FOUND;
}

void syscall(uint32_t eax, uintptr_t ebx, uintptr_t ecx, uintptr_t edx) {
    switch (eax) {
        case SYS_EXEC:
            emulate_exec((const struct EXT2DriverRequest *) ebx, (int32_t *) ecx, (uint32_t *) edx);
            break;
        case SYS_KILL:
            *(int8_t *) ecx = emulate_kill((uint32_t) ebx);
            break;
        case SYS_PROCESS_INFO: {
            uint32_t n = proc_count < ecx ? proc_count : (uint32_t) ecx;
            memcpy((void *) ebx, procs, n * sizeof(procs[0]));
            *(uint32_t *) edx = n;
            break;
        }
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
        case SYS_FS_MOVE:
            *(int8_t *) edx = move(*(struct EXT2DriverRequest *) ebx, *(struct EXT2DriverRequest *) ecx);
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

// like put_file, but inside the folder at dir (resolved from the current cwd)
static void put_file_in(const char *dir, const char *name, const char *content) {
    uint32_t parent = 0;
    if (!shell_resolve_dir(dir, &parent)) {
        printf("  FAIL put_file_in: no folder %s\n", dir);
        fails++;
        return;
    }
    struct EXT2DriverRequest request = {
        .buf = (void *) content, .name = (char *) name, .name_len = (uint8_t) strlen(name),
        .parent_inode = parent, .buffer_size = (uint32_t) strlen(content),
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

static void test_process_commands(void) {
    reset_shell();
    reset_processes();
    put_file("clock", "\xEB\xFE");
    CHECK_STR(run("mkdir bin"), "");

    CHECK_STR(run("exec clock"), "[exec] clock berjalan dengan PID 1\n");
    CHECK_STR(run("ps"),
        "PID    NAME             STATE\n"
        "0      shell            RUNNING\n"
        "1      clock            READY\n");
    CHECK_STR(run("ps | grep clock"), "1      clock            READY\n");
    CHECK_STR(run("kill 1"), "[kill] PID 1 dihentikan\n");
    CHECK_STR(run("ps"), "PID    NAME             STATE\n0      shell            RUNNING\n");
    CHECK_STR(run("kill 1"), "[kill] PID 1 tidak ditemukan\n");
    CHECK_STR(run("kill 0"), "[kill] tidak bisa menghentikan init process\n");
    CHECK_STR(run("kill 1x"), "[kill] PID '1x' tidak valid\n");
    CHECK_STR(run("kill 4294967296"), "[kill] PID '4294967296' tidak valid\n");
    CHECK_STR(run("kill"), "[kill] usage: kill <pid>\n");
    CHECK_STR(run("ps x"), "[ps] usage: ps\n");

    CHECK_STR(run("exec tidakada"), "[exec] file 'tidakada' tidak ditemukan\n");
    CHECK_STR(run("exec bin"), "[exec] file 'bin' tidak ditemukan\n");
    CHECK_STR(run("exec"), "[exec] usage: exec <path>\n");
    CHECK_STR(run("cd bin"), "");
    CHECK_STR(run("exec ../clock"), "[exec] clock berjalan dengan PID 2\n");

    for (int i = 2; i < PROCESS_INFO_COUNT_MAX; i++)
        run("exec /clock");
    CHECK_STR(run("exec /clock"), "[exec] gagal: jumlah process sudah maksimum\n");
}

static void test_find(void) {
    reset_shell();
    CHECK_STR(run("mkdir docs"), "");
    CHECK_STR(run("mkdir docs/notes"), "");
    CHECK_STR(run("mkdir work"), "");
    CHECK_STR(run("mkdir work/notes"), "");
    CHECK_STR(run("mkdir work/deep"), "");
    CHECK_STR(run("mkdir work/deep/notes"), "");
    put_file("notes", "root file\n");
    put_file_in("/docs", "todo.txt", "x\n");
    put_file_in("/work/deep", "notes", "deep file\n");

    // one result
    CHECK_STR(run("find todo.txt"), "/docs/todo.txt\n");
    // a folder matches with a trailing '/'
    CHECK_STR(run("find deep"), "/work/deep/\n");
    // many results at different depths, DFS in entry order, file and folder with the same name
    CHECK_STR(run("find notes"),
        "/docs/notes/\n"
        "/work/notes/\n"
        "/work/deep/notes/\n"
        "/work/deep/notes\n"
        "/notes\n");
    // pipeline
    CHECK_STR(run("find notes | grep deep"), "/work/deep/notes/\n/work/deep/notes\n");
    // always searches from root and prints absolute paths, whatever the cwd is
    CHECK_STR(run("cd work/deep"), "");
    CHECK_STR(run("find todo.txt"), "/docs/todo.txt\n");
    CHECK_STR(run("cd /"), "");

    // errors, . and .. are never matched
    CHECK_STR(run("find nope"), "find: 'nope' not found\n");
    CHECK_STR(run("find .."), "find: '..' not found\n");
    CHECK_STR(run("find nope | grep x"), "find: 'nope' not found\n");
    CHECK_STR(run("find"), "find: missing operand\n");
    CHECK_STR(run("find a b"), "find: too many arguments\n");

    // 10 levels deep
    reset_shell();
    char path[256] = "";
    char command[300];
    for (int i = 1; i <= 10; i++) {
        size_t len = strlen(path);
        snprintf(path + len, sizeof(path) - len, "/l%d", i);
        snprintf(command, sizeof(command), "mkdir %s", path);
        CHECK_STR(run(command), "");
    }
    put_file_in(path, "target", "x\n");
    char expected[300];
    snprintf(expected, sizeof(expected), "%s/target\n", path);
    CHECK_STR(run("find target"), expected);
    snprintf(expected, sizeof(expected), "%s/\n", path);
    CHECK_STR(run("find l10"), expected);

    // deeper than the limit: 34 nested folders "x", the subtree below level 33 is skipped with one warning
    reset_shell();
    for (int i = 0; i < 34; i++) {
        CHECK_STR(run("mkdir x"), "");
        CHECK_STR(run("cd x"), "");
    }
    put_file_in(".", "leaf", "x\n");
    CHECK_STR(run("cd /"), "");
    CHECK_STR(run("find leaf"),
        "find: folders deeper than 32 levels are skipped\n"
        "find: 'leaf' not found\n");
    // folders up to level 33 are still matched (seen inside level 32)
    const char *out = run("find x | grep x");
    size_t lines = 0;
    for (const char *p = out; *p; p++) {
        if (*p == '\n') {
            lines++;
        }
    }
    CHECK(strncmp(out, "find: folders deeper than 32 levels are skipped\n", 48) == 0);
    CHECK(lines == 1 + 33);
}

static void test_mv(void) {
    reset_shell();
    put_file("a.txt", "alpha\n");
    CHECK_STR(run("mkdir d"), "");
    CHECK_STR(run("mkdir e"), "");

    // rename a file
    CHECK_STR(run("mv a.txt b.txt"), "");
    CHECK_STR(run("cat b.txt"), "alpha\n");
    CHECK_STR(run("cat a.txt"), "cat: a.txt: no such file\n");

    // file into a folder, keeps its name
    CHECK_STR(run("mv b.txt d"), "");
    CHECK_STR(run("ls d"), "b.txt\n");
    CHECK_STR(run("cat d/b.txt"), "alpha\n");

    // relative path with .. and absolute paths
    CHECK_STR(run("cd d"), "");
    CHECK_STR(run("mv b.txt ../e/c.txt"), "");
    CHECK_STR(run("ls"), "");
    CHECK_STR(run("cat /e/c.txt"), "alpha\n");
    CHECK_STR(run("mv /e/c.txt /d"), "");
    CHECK_STR(run("mv ./c.txt ./c2.txt"), "");
    CHECK_STR(run("ls"), "c2.txt\n");
    CHECK_STR(run("cd /"), "");

    // rename a folder
    // new entries take the first free slot (left by the moved a.txt), so f comes before d
    CHECK_STR(run("mv e f"), "");
    CHECK_STR(run("ls"), "f/\nd/\n");

    // folder with content into another folder, then use it from the new place
    CHECK_STR(run("mkdir d/sub"), "");
    put_file_in("/d/sub", "inner.txt", "inside\n");
    CHECK_STR(run("mv d f"), "");
    CHECK_STR(run("ls"), "f/\n");
    CHECK_STR(run("ls f"), "d/\n");
    CHECK_STR(run("ls f/d"), "sub/\nc2.txt\n");
    CHECK_STR(run("cat f/d/sub/inner.txt"), "inside\n");
    CHECK_STR(run("cd f/d/sub"), "");
    CHECK_STR(prompt(), "InterupsiArOS:/f/d/sub$ ");
    CHECK_STR(run("cat inner.txt"), "inside\n");
    CHECK_STR(run("cd .."), "");
    CHECK_STR(prompt(), "InterupsiArOS:/f/d$ ");
    CHECK_STR(run("ls"), "sub/\nc2.txt\n");
    CHECK_STR(run("cd ../.."), "");
    CHECK_STR(prompt(), "InterupsiArOS:/$ ");

    // folder into itself or a descendant is rejected, also mv a a and mv a .
    CHECK_STR(run("mv f f"), "mv: cannot move 'f' into itself\n");
    CHECK_STR(run("mv f ."), "mv: target './f' already exists\n");
    CHECK_STR(run("mv f/ f/d/sub"), "mv: cannot move 'f/' into itself\n");
    CHECK_STR(run("mv f/d f/d"), "mv: cannot move 'f/d' into itself\n");
    CHECK_STR(run("mv f/d f/d/sub/new"), "mv: cannot move 'f/d' into itself\n");
    CHECK_STR(run("cd f/d"), "");
    CHECK_STR(run("mv . sub"), "mv: cannot move '.': invalid source\n");
    CHECK_STR(run("mv .. sub"), "mv: cannot move '..': invalid source\n");
    CHECK_STR(run("cd /"), "");
    // structure untouched
    CHECK_STR(run("ls"), "f/\n");
    CHECK_STR(run("ls f"), "d/\n");
    CHECK_STR(run("ls f/d"), "sub/\nc2.txt\n");
    CHECK_STR(run("ls f/d/sub"), "inner.txt\n");
    CHECK_STR(run("cd f/d/sub"), "");
    CHECK_STR(run("cd ../../.."), "");
    CHECK_STR(prompt(), "InterupsiArOS:/$ ");

    // target already exists (rename and move into a folder)
    put_file("x.txt", "1\n");
    put_file("y.txt", "2\n");
    CHECK_STR(run("mv x.txt y.txt"), "mv: target 'y.txt' already exists\n");
    CHECK_STR(run("mv x.txt x.txt"), "mv: target 'x.txt' already exists\n");
    CHECK_STR(run("mkdir g"), "");
    CHECK_STR(run("mkdir f/g"), "");
    CHECK_STR(run("mv g f"), "mv: target 'f/g' already exists\n");
    CHECK_STR(run("mv g f/"), "mv: target 'f/g' already exists\n");
    CHECK_STR(run("cat y.txt"), "2\n");

    // source not found, invalid target, invalid source
    CHECK_STR(run("mv nope z"), "mv: cannot stat 'nope': no such file or directory\n");
    CHECK_STR(run("mv nodir/a z"), "mv: cannot stat 'nodir/a': no such file or directory\n");
    CHECK_STR(run("mv x.txt/ z"), "mv: cannot stat 'x.txt/': no such file or directory\n");
    CHECK_STR(run("mv x.txt nodir/z"), "mv: invalid target 'nodir/z'\n");
    CHECK_STR(run("mv x.txt f/.."), "mv: target 'f/../x.txt' already exists\n");
    CHECK_STR(run("mv / f"), "mv: cannot move '/': invalid source\n");

    // operands
    CHECK_STR(run("mv"), "mv: missing operand\n");
    CHECK_STR(run("mv x.txt"), "mv: missing operand\n");
    CHECK_STR(run("mv a b c"), "mv: too many arguments\n");
    CHECK_STR(run("ls"), "f/\nx.txt\ny.txt\ng/\n");

    // file and folder with the same name
    reset_shell();
    put_file("x", "file x\n");
    CHECK_STR(run("mkdir x"), "");
    CHECK_STR(run("mkdir t"), "");
    CHECK_STR(run("mv x t"), "");              // the file is picked
    CHECK_STR(run("ls t"), "x\n");
    CHECK_STR(run("ls"), "x/\nt/\n");
    CHECK_STR(run("mv x t"), "");              // only the folder is left, so it moves
    CHECK_STR(run("ls t"), "x\nx/\n");
    CHECK_STR(run("ls"), "t/\n");
    CHECK_STR(run("mv t/x/ y"), "");           // trailing '/' picks the folder
    CHECK_STR(run("ls t"), "x\n");
    CHECK_STR(run("mv t/x y"), "");            // file x goes into folder y
    CHECK_STR(run("ls y"), "x\n");
    CHECK_STR(run("cat y/x"), "file x\n");
    CHECK_STR(run("mv y/x y/x"), "mv: target 'y/x' already exists\n");
    CHECK_STR(run("mkdir y/x"), "");
    CHECK_STR(run("mv y/x y/x"), "");          // file y/x into folder y/x
    CHECK_STR(run("ls y"), "x/\n");
    CHECK_STR(run("cat y/x/x"), "file x\n");
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
    test_process_commands();
    test_find();
    test_mv();
    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}
