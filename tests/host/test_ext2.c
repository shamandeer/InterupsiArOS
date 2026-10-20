// host tests for ext2 CRUD (milestone 2 checklist), run with: make test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "header/filesystem/ext2.h"

void fake_disk_reset(void);
uint8_t *fake_disk_raw(void);
bool fake_disk_load(const char *path);

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) passes++; \
    else { fails++; printf("  FAIL %s:%d  %s\n", __func__, __LINE__, #cond); } \
} while (0)

/* ---------- disk inspection (always from disk, never from ext2.c RAM) ---------- */

static uint32_t count_zero_bits(uint32_t block, uint32_t bits) {
    struct BlockBuffer b;
    uint32_t n = 0;
    read_blocks(&b, block, 1);
    for (uint32_t i = 0; i < bits; i++)
        n += !((b.buf[i / 8] >> (i % 8)) & 1u);
    return n;
}

static struct EXT2BlockGroupDescriptor disk_bgd(uint32_t g) {
    struct BlockBuffer b;
    struct EXT2BlockGroupDescriptor d;
    read_blocks(&b, 2, 1);
    memcpy(&d, b.buf + g * sizeof(d), sizeof(d));
    return d;
}

static uint32_t bitmap_free_blocks(void) {
    uint32_t n = 0;
    for (uint32_t g = 0; g < GROUPS_COUNT; g++)
        n += count_zero_bits(disk_bgd(g).bg_block_bitmap, BLOCKS_PER_GROUP);
    return n;
}

// free counters in superblock and every BGD must match the bitmaps on disk
static bool counters_match_bitmaps(void) {
    struct BlockBuffer b;
    struct EXT2Superblock sb;
    read_blocks(&b, 1, 1);
    memcpy(&sb, b.buf, sizeof(sb));
    uint32_t free_blocks = 0, free_inodes = 0;
    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        struct EXT2BlockGroupDescriptor d = disk_bgd(g);
        uint32_t fb = count_zero_bits(d.bg_block_bitmap, BLOCKS_PER_GROUP);
        uint32_t fi = count_zero_bits(d.bg_inode_bitmap, INODES_PER_GROUP);
        if (fb != d.bg_free_blocks_count || fi != d.bg_free_inodes_count)
            return false;
        free_blocks += fb;
        free_inodes += fi;
    }
    return free_blocks == sb.s_free_blocks_count && free_inodes == sb.s_free_inodes_count;
}

/* ---------- request helpers ---------- */

static struct EXT2DriverRequest make_request(uint32_t parent, const char *name, void *buf, uint32_t size, bool is_dir) {
    struct EXT2DriverRequest r = {
        .buf = buf, .name = (char*) name, .name_len = (uint8_t) strlen(name),
        .parent_inode = parent, .buffer_size = size, .is_directory = is_dir,
    };
    return r;
}

static int8_t write_file(uint32_t parent, const char *name, void *buf, uint32_t size) {
    struct EXT2DriverRequest r = make_request(parent, name, buf, size, false);
    return write(&r);
}

static int8_t write_folder(uint32_t parent, const char *name) {
    struct EXT2DriverRequest r = make_request(parent, name, NULL, 0, true);
    return write(&r);
}

static int8_t read_file(uint32_t parent, const char *name, void *buf, uint32_t size) {
    return read(make_request(parent, name, buf, size, false));
}

static int8_t read_folder(uint32_t parent, const char *name, void *buf, uint32_t size) {
    struct EXT2DriverRequest r = make_request(parent, name, buf, size, true);
    return read_directory(&r);
}

static int8_t delete_entry(uint32_t parent, const char *name, bool is_dir) {
    return delete(make_request(parent, name, NULL, 0, is_dir));
}

static void fill_pattern(uint8_t *buf, uint32_t size, uint32_t seed) {
    for (uint32_t i = 0; i < size; i++)
        buf[i] = (uint8_t) (i * 7u + seed * 13u + (i / BLOCK_SIZE) * 31u);
}

/* ---------- tests ---------- */

static void test_init(void) {
    fake_disk_reset();
    initialize_filesystem_ext2();
    struct EXT2Inode root;
    CHECK(!is_empty_storage());
    CHECK(load_directory_node(&root, ROOT_INODE));
    CHECK(find_directory_entry(ROOT_INODE, ".", 1) == ROOT_INODE);
    CHECK(find_directory_entry(ROOT_INODE, "..", 2) == ROOT_INODE);
    CHECK(is_directory_empty(ROOT_INODE));
    CHECK(counters_match_bitmaps());
}

static void test_roundtrip(void) {
    // 1 block, exact multiple, direct limit, single indirect limit, double indirect
    uint32_t sizes[] = {1, 512, 513, 6144, 6145, 71680, 71681, 300000};
    for (uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        char name[16];
        uint32_t size = sizes[i];
        uint8_t *src = malloc(size), *dst = malloc(size + 16);
        snprintf(name, sizeof(name), "file%u", i);
        fill_pattern(src, size, i + 1);
        memset(dst, 0xAA, size + 16);

        uint32_t data_blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        uint32_t free_before = bitmap_free_blocks();
        CHECK(write_file(ROOT_INODE, name, src, size) == 0);
        CHECK(free_before - bitmap_free_blocks() >= data_blocks);
        CHECK(read_file(ROOT_INODE, name, dst, size) == 0);
        CHECK(memcmp(src, dst, size) == 0);
        CHECK(dst[size] == 0xAA); // nothing written past buffer_size
        free(src);
        free(dst);
    }
    CHECK(counters_match_bitmaps());
}

static void test_error_codes(void) {
    uint8_t buf[2048], tiny[4];
    CHECK(write_folder(ROOT_INODE, "folder1") == 0);
    uint32_t folder1 = find_directory_entry(ROOT_INODE, "folder1", 7);
    uint32_t file0 = find_file_entry(ROOT_INODE, "file0", 5);
    CHECK(folder1 != 0 && file0 != 0);
    CHECK(find_directory_entry(folder1, "..", 2) == ROOT_INODE);

    // write: 1 exists, 2 parent invalid, -1 invalid name
    CHECK(write_file(ROOT_INODE, "file0", buf, 10) == 1);
    CHECK(write_folder(ROOT_INODE, "folder1") == 1);
    CHECK(write_file(ROOT_INODE, ".", buf, 10) == 1);
    CHECK(write_file(999, "x", buf, 10) == 2);
    CHECK(write_file(file0, "x", buf, 10) == 2);
    CHECK(write_file(ROOT_INODE, "a/b", buf, 10) == -1);
    CHECK(write_file(ROOT_INODE, "", buf, 10) == -1);
    {
        char with_null[3] = {'a', '\0', 'b'};
        struct EXT2DriverRequest r = make_request(ROOT_INODE, "a", buf, 10, false);
        r.name = with_null;
        r.name_len = 3;
        CHECK(write(&r) == -1);
    }

    // read: 1 not a file, 2 buffer, 3 not found, 4 parent invalid
    CHECK(read_file(ROOT_INODE, "folder1", buf, sizeof(buf)) == 1);
    CHECK(read_file(ROOT_INODE, "file2", tiny, sizeof(tiny)) == 2);
    CHECK(read_file(ROOT_INODE, "nope", buf, sizeof(buf)) == 3);
    CHECK(read_file(999, "file0", buf, sizeof(buf)) == 4);

    // read_directory: 1 not a folder, 2 not found, 3 parent invalid
    CHECK(read_folder(ROOT_INODE, "folder1", buf, sizeof(buf)) == 0);
    CHECK(read_folder(ROOT_INODE, "file0", buf, sizeof(buf)) == 1);
    CHECK(read_folder(ROOT_INODE, "nope", buf, sizeof(buf)) == 2);
    CHECK(read_folder(999, "folder1", buf, sizeof(buf)) == 3);
    CHECK(read_folder(ROOT_INODE, "folder1", tiny, sizeof(tiny)) == -1);

    // delete: 1 not found, 2 not empty, 3 parent invalid, root can not be deleted
    CHECK(write_file(folder1, "isi", buf, 100) == 0);
    CHECK(delete_entry(ROOT_INODE, "nope", false) == 1);
    CHECK(delete_entry(ROOT_INODE, "folder1", true) == 2);
    CHECK(delete_entry(999, "file0", false) == 3);
    CHECK(delete_entry(ROOT_INODE, ".", true) != 0);
    CHECK(delete_entry(ROOT_INODE, "..", true) != 0);
    CHECK(read_folder(ROOT_INODE, ".", buf, sizeof(buf)) == 0);

    // NULL name must not crash
    struct EXT2DriverRequest r = make_request(ROOT_INODE, "x", buf, sizeof(buf), false);
    r.name = NULL;
    CHECK(read(r) == -1 && read_directory(&r) == -1 && delete(r) == -1 && write(&r) == -1);
}

static void test_delete_and_first_fit(void) {
    uint8_t buf[512] = {0};
    struct EXT2Inode node;
    load_node(&node, find_file_entry(ROOT_INODE, "file1", 5));
    uint32_t freed = node.i_block[0];
    CHECK(delete_entry(ROOT_INODE, "file1", false) == 0);
    CHECK(read_file(ROOT_INODE, "file1", buf, sizeof(buf)) == 3);
    CHECK(write_file(ROOT_INODE, "reuse", buf, 100) == 0);
    load_node(&node, find_file_entry(ROOT_INODE, "reuse", 5));
    CHECK(node.i_block[0] == freed);

    // double indirect file gives back data + pointer blocks
    uint32_t free_before = bitmap_free_blocks();
    CHECK(delete_entry(ROOT_INODE, "file7", false) == 0);
    uint32_t data_blocks = (300000 + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint32_t pointer_blocks = 2 + (data_blocks - 140 + 127) / 128;
    CHECK(bitmap_free_blocks() - free_before == data_blocks + pointer_blocks);
    CHECK(counters_match_bitmaps());
}

static void test_same_name_file_and_folder(void) {
    uint8_t buf[128] = {1, 2, 3};
    CHECK(write_file(ROOT_INODE, "x", buf, 3) == 0);
    CHECK(write_folder(ROOT_INODE, "x") == 0);
    uint32_t file_x = find_file_entry(ROOT_INODE, "x", 1);

    CHECK(delete_entry(ROOT_INODE, "x", true) == 0);
    CHECK(find_directory_entry(ROOT_INODE, "x", 1) == 0);
    CHECK(find_file_entry(ROOT_INODE, "x", 1) == file_x);
    CHECK(read_file(ROOT_INODE, "x", buf, sizeof(buf)) == 0 && buf[2] == 3);

    CHECK(write_folder(ROOT_INODE, "x") == 0);
    CHECK(delete_entry(ROOT_INODE, "x", false) == 0);
    CHECK(find_file_entry(ROOT_INODE, "x", 1) == 0);
    CHECK(find_directory_entry(ROOT_INODE, "x", 1) != 0);
    CHECK(delete_entry(ROOT_INODE, "x", true) == 0);
    CHECK(counters_match_bitmaps());
}

static void test_directory_grow_and_shrink(void) {
    uint8_t one = 1, buf[BLOCK_SIZE * DIRECT_BLOCKS];
    char name[48];
    CHECK(write_folder(ROOT_INODE, "big") == 0);
    uint32_t big = find_directory_entry(ROOT_INODE, "big", 3);
    for (int i = 0; i < 80; i++) {
        snprintf(name, sizeof(name), "entry_with_a_long_name_%02d", i);
        CHECK(write_file(big, name, &one, 1) == 0);
    }
    struct EXT2Inode dir;
    load_node(&dir, big);
    uint32_t grown = dir.i_blocks;
    CHECK(grown > 1 && dir.i_size == grown * BLOCK_SIZE);
    CHECK(read_folder(ROOT_INODE, "big", buf, BLOCK_SIZE) == -1);
    CHECK(read_folder(ROOT_INODE, "big", buf, sizeof(buf)) == 0);

    for (int i = 0; i < 80; i++) {
        snprintf(name, sizeof(name), "entry_with_a_long_name_%02d", i);
        CHECK(delete_entry(big, name, false) == 0);
    }
    load_node(&dir, big);
    CHECK(dir.i_blocks == 1 && dir.i_size == BLOCK_SIZE);
    CHECK(is_directory_empty(big));
    CHECK(delete_entry(ROOT_INODE, "big", true) == 0);
    CHECK(counters_match_bitmaps());
}

static void test_mount(void) {
    uint8_t *raw = fake_disk_raw();
    uint8_t buf[6145], expected[6145];
    fill_pattern(expected, sizeof(expected), 5);

    // persistent after re-mount
    initialize_filesystem_ext2();
    CHECK(read_file(ROOT_INODE, "file4", buf, sizeof(buf)) == 0);
    CHECK(memcmp(buf, expected, sizeof(buf)) == 0);

    // stale counters (crash before commit) are recounted on mount
    struct EXT2BlockGroupDescriptor *bgd = (struct EXT2BlockGroupDescriptor*) (raw + 2 * BLOCK_SIZE);
    bgd->bg_free_blocks_count = 1;
    initialize_filesystem_ext2();
    commit_metadata();
    CHECK(counters_match_bitmaps());

    // broken superblock is treated as empty storage
    struct EXT2Superblock *sb = (struct EXT2Superblock*) (raw + BLOCK_SIZE);
    sb->s_magic = 0;
    initialize_filesystem_ext2();
    CHECK(read_file(ROOT_INODE, "file4", buf, sizeof(buf)) == 3);
    CHECK(is_directory_empty(ROOT_INODE));
}

static void test_sample_image(const char *path) {
    if (!fake_disk_load(path)) {
        printf("  skip sample image (%s not found)\n", path);
        return;
    }
    initialize_filesystem_ext2();
    const uint32_t sample_root = 1, folder1 = 2, folder2 = 6;
    const char *files[][3] = {
        {"r", "laur", "Grievous Lady"}, {"r", "sasakure", "TONDEMO-WONDERZ"},
        {"f", "tpazolite", "Oshama Scramble!"}, {"f", "xi", "Freedom Dive"},
    };
    uint8_t buf[512];
    for (int i = 0; i < 4; i++) {
        memset(buf, 0, sizeof(buf));
        uint32_t parent = files[i][0][0] == 'r' ? sample_root : folder1;
        CHECK(read_file(parent, files[i][1], buf, sizeof(buf)) == 0);
        CHECK(strcmp((char*) buf, files[i][2]) == 0);
    }
    CHECK(read_folder(sample_root, "folder1", buf, sizeof(buf)) == 0);
    CHECK(read_folder(folder1, "nestedf1", buf, sizeof(buf)) == 0);
    CHECK(write_file(folder2, "halo", "hai", 3) == 0);
    CHECK(read_file(folder2, "halo", buf, sizeof(buf)) == 0 && memcmp(buf, "hai", 3) == 0);
    CHECK(delete_entry(folder2, "halo", false) == 0);
    CHECK(is_directory_empty(folder2));
}

int main(int argc, char **argv) {
    test_init();
    test_roundtrip();
    test_error_codes();
    test_delete_and_first_fit();
    test_same_name_file_and_folder();
    test_directory_grow_and_shrink();
    test_mount();
    test_sample_image(argc > 1 ? argv[1] : ".kit/ch2/sample-image.bin");

    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}
