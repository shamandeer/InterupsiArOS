// host tests for ext2 write(); build with tests/host/build.sh, run from repo root
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "header/filesystem/ext2.h"

void fake_disk_reset(void);
bool fake_disk_load(const char *path);
bool fake_disk_dump(const char *path);
// non-static in ext2.c but missing from ext2.h
uint32_t find_file_entry(uint32_t file_inode, const char *name, uint8_t name_len);

#define ROOT 2u
#define PPB (BLOCK_SIZE / 4u) // pointers per block
#define SAMPLE_IMAGE "tests/host/sample-image-rev.bin"
#define TREE_DUMP "tests/host/out-tree.bin"

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; printf("  PASS %s:%d  %s\n", __func__, __LINE__, #cond); } \
    else      { fails++;  printf("  FAIL %s:%d  %s\n", __func__, __LINE__, #cond); } \
} while (0)

/* ---------- disk inspection (always from disk, never from ext2.c RAM) ---------- */

static struct EXT2Superblock disk_sb(void) {
    struct BlockBuffer b;
    struct EXT2Superblock sb;
    read_blocks(&b, 1, 1);
    memcpy(&sb, b.buf, sizeof(sb));
    return sb;
}

static struct EXT2BlockGroupDescriptor disk_bgd(uint32_t g) {
    struct BlockBuffer b;
    struct EXT2BlockGroupDescriptor d;
    read_blocks(&b, 2, 1);
    memcpy(&d, b.buf + g * sizeof(d), sizeof(d));
    return d;
}

static uint32_t count_zero_bits(uint32_t block, uint32_t bits) {
    struct BlockBuffer b;
    uint32_t n = 0;
    read_blocks(&b, block, 1);
    for (uint32_t i = 0; i < bits; i++)
        n += !((b.buf[i / 8] >> (i % 8)) & 1u);
    return n;
}

static uint32_t bitmap_free_blocks(void) {
    uint32_t n = 0;
    for (uint32_t g = 0; g < GROUPS_COUNT; g++)
        n += count_zero_bits(disk_bgd(g).bg_block_bitmap, BLOCKS_PER_GROUP);
    return n;
}

// free counters in superblock and every BGD must match the bitmaps on disk
static bool check_counters(void) {
    struct EXT2Superblock sb = disk_sb();
    uint32_t free_blocks = 0, free_inodes = 0;
    bool ok = true;
    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        struct EXT2BlockGroupDescriptor d = disk_bgd(g);
        uint32_t fb = count_zero_bits(d.bg_block_bitmap, BLOCKS_PER_GROUP);
        uint32_t fi = count_zero_bits(d.bg_inode_bitmap, INODES_PER_GROUP);
        if (fb != d.bg_free_blocks_count || fi != d.bg_free_inodes_count) {
            printf("    group %u: bitmap free b/i = %u/%u, bgd says %u/%u\n",
                   g, fb, fi, d.bg_free_blocks_count, d.bg_free_inodes_count);
            ok = false;
        }
        free_blocks += fb;
        free_inodes += fi;
    }
    if (free_blocks != sb.s_free_blocks_count || free_inodes != sb.s_free_inodes_count) {
        printf("    superblock free b/i = %u/%u, bitmaps say %u/%u\n",
               sb.s_free_blocks_count, sb.s_free_inodes_count, free_blocks, free_inodes);
        ok = false;
    }
    return ok;
}

// superblock + bgd table + every bitmap: everything a leak would change
struct Meta {
    struct BlockBuffer sb, bgd, bitmaps[GROUPS_COUNT * 2];
};

static void snapshot(struct Meta *m) {
    read_blocks(&m->sb, 1, 1);
    read_blocks(&m->bgd, 2, 1);
    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        struct EXT2BlockGroupDescriptor d = disk_bgd(g);
        read_blocks(&m->bitmaps[g * 2], d.bg_block_bitmap, 1);
        read_blocks(&m->bitmaps[g * 2 + 1], d.bg_inode_bitmap, 1);
    }
}

static bool same_meta(const struct Meta *before) {
    struct Meta now;
    snapshot(&now);
    return memcmp(before, &now, sizeof(now)) == 0;
}

/* ---------- request / read helpers ---------- */

static int8_t do_write(uint32_t parent, const char *name, void *buf, uint32_t size) {
    struct EXT2DriverRequest r = {
        .buf = buf, .name = (char*) name, .name_len = (uint8_t) strlen(name),
        .parent_inode = parent, .buffer_size = size, .is_directory = false,
    };
    return ext2_write(&r);
}

static int8_t do_mkdir(uint32_t parent, const char *name) {
    return do_write(parent, name, NULL, 0);
}

// load_node + read_node_blocks, no read() from part C
static struct EXT2Inode read_back(uint32_t inode, void *out) {
    struct EXT2Inode node;
    load_node(&node, inode);
    read_node_blocks(out, &node);
    return node;
}

// k-th data block number of a node (direct / single / double indirect)
static uint32_t data_block(const struct EXT2Inode *node, uint32_t k) {
    uint32_t ptr[PPB];
    if (k < 12)
        return node->i_block[k];
    k -= 12;
    if (k < PPB) {
        read_blocks(ptr, node->i_block[12], 1);
        return ptr[k];
    }
    k -= PPB;
    read_blocks(ptr, node->i_block[13], 1);
    read_blocks(ptr, ptr[k / PPB], 1);
    return ptr[k % PPB];
}

static uint32_t pointer_blocks(uint32_t n) {
    if (n <= 12) return 0;
    n -= 12;
    if (n <= PPB) return 1;
    n -= PPB;
    return 2 + (n + PPB - 1) / PPB;
}

static uint8_t pattern(uint32_t i) { return (uint8_t) ((i * 7u + 3u) & 0xFFu); }

static uint8_t *make_pattern(uint32_t size) {
    uint8_t *p = malloc(size ? size : 1);
    for (uint32_t i = 0; i < size; i++)
        p[i] = pattern(i);
    return p;
}

static struct EXT2DirectoryEntry *entry_at(struct BlockBuffer *b, uint32_t off) {
    return (struct EXT2DirectoryEntry*) (b->buf + off);
}

static void fresh(void) {
    fake_disk_reset();
    initialize_filesystem_ext2();
}

/* ---------- cases ---------- */

static void test_a_roundtrip(void) {
    const uint32_t sizes[] = {1, 511, 512, 513, 2048, 12 * 512, 12 * 512 + 1,
                              (12 + 128) * 512, (12 + 128) * 512 + 1};
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        fresh();
        uint32_t size = sizes[s];
        uint32_t blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        uint8_t *src = make_pattern(size);
        uint8_t *dst = calloc(1, size);
        char name[16];
        snprintf(name, sizeof(name), "f%u", size);
        printf(" size %u (%u data + %u pointer blocks)\n", size, blocks, pointer_blocks(blocks));

        uint32_t free_before = bitmap_free_blocks();
        CHECK(do_write(ROOT, name, src, size) == 0);
        uint32_t inode = find_file_entry(ROOT, name, (uint8_t) strlen(name));
        CHECK(inode != 0);

        struct EXT2Inode node = read_back(inode, dst);
        CHECK(memcmp(src, dst, size) == 0);
        CHECK(node.i_mode == EXT2_S_IFREG);
        CHECK(node.i_size == size);
        CHECK(node.i_blocks == blocks);
        CHECK(free_before - bitmap_free_blocks() == blocks + pointer_blocks(blocks));

        // raw blocks: byte 0 and last byte of every block, zero padding after i_size
        bool edges = true, padding = true;
        for (uint32_t k = 0; k < blocks; k++) {
            struct BlockBuffer raw;
            read_blocks(&raw, data_block(&node, k), 1);
            uint32_t start = k * BLOCK_SIZE;
            uint32_t len = (size - start < BLOCK_SIZE) ? size - start : BLOCK_SIZE;
            edges &= raw.buf[0] == pattern(start) && raw.buf[len - 1] == pattern(start + len - 1);
            for (uint32_t i = len; i < BLOCK_SIZE; i++)
                padding &= raw.buf[i] == 0;
        }
        CHECK(edges);
        CHECK(padding);
        CHECK(check_counters());
        free(src);
        free(dst);
    }
}

static void test_b_folders(void) {
    fresh();
    uint16_t dirs0 = disk_bgd(0).bg_used_dirs_count;

    CHECK(do_mkdir(ROOT, "folder1") == 0);
    uint32_t f1 = find_directory_entry(ROOT, "folder1", 7);
    CHECK(f1 != 0);
    CHECK(disk_bgd(inode_to_bgd(f1)).bg_used_dirs_count == dirs0 + 1);

    CHECK(do_mkdir(f1, "nestedf1") == 0);
    uint32_t n1 = find_directory_entry(f1, "nestedf1", 8);
    CHECK(n1 != 0);
    CHECK(disk_bgd(inode_to_bgd(n1)).bg_used_dirs_count == dirs0 + 2);

    const uint32_t self[2] = {f1, n1}, parent[2] = {ROOT, f1};
    for (int i = 0; i < 2; i++) {
        struct EXT2Inode node;
        struct BlockBuffer b;
        load_node(&node, self[i]);
        CHECK(node.i_mode == EXT2_S_IFDIR);
        CHECK(node.i_size == BLOCK_SIZE && node.i_blocks == 1);
        read_blocks(&b, node.i_block[0], 1);
        struct EXT2DirectoryEntry *dot = entry_at(&b, 0);
        struct EXT2DirectoryEntry *dotdot = entry_at(&b, dot->rec_len);
        CHECK(dot->inode == self[i] && dot->name_len == 1 && get_entry_name(dot)[0] == '.');
        CHECK(dotdot->inode == parent[i] && dotdot->name_len == 2 && memcmp(get_entry_name(dotdot), "..", 2) == 0);
        CHECK(dot->file_type == EXT2_FT_DIR && dotdot->file_type == EXT2_FT_DIR);
        // folder1 also holds nestedf1, so sum the whole entry chain
        uint32_t total = 0;
        for (uint32_t off = 0; off < BLOCK_SIZE && entry_at(&b, off)->rec_len != 0; off += entry_at(&b, off)->rec_len)
            total += entry_at(&b, off)->rec_len;
        CHECK(total == BLOCK_SIZE);
    }
    CHECK(check_counters());
}

static void test_c_duplicates(void) {
    fresh();
    char data[] = "hello";
    CHECK(do_write(ROOT, "a", data, 5) == 0);
    CHECK(do_write(ROOT, "a", data, 5) == 1);
    CHECK(do_mkdir(ROOT, "d") == 0);
    CHECK(do_mkdir(ROOT, "d") == 1);
    CHECK(do_write(ROOT, "x", data, 5) == 0);
    CHECK(do_mkdir(ROOT, "x") == 0);
    CHECK(find_file_entry(ROOT, "x", 1) != 0 && find_directory_entry(ROOT, "x", 1) != 0);
    CHECK(find_file_entry(ROOT, "x", 1) != find_directory_entry(ROOT, "x", 1));

    struct Meta m;
    snapshot(&m);
    CHECK(do_write(ROOT, ".", data, 5) == 1);
    CHECK(do_write(ROOT, "..", data, 5) == 1);
    CHECK(do_mkdir(ROOT, ".") == 1);
    CHECK(do_mkdir(ROOT, "..") == 1);
    CHECK(same_meta(&m));
    CHECK(check_counters());
}

static void test_d_invalid_parent(void) {
    fresh();
    char data[] = "hello";
    CHECK(do_write(ROOT, "file", data, 5) == 0);
    uint32_t file = find_file_entry(ROOT, "file", 4);
    CHECK(file != 0);

    struct Meta m;
    snapshot(&m);
    CHECK(do_write(0, "n", data, 5) == 2);
    CHECK(do_write(999, "n", data, 5) == 2);
    CHECK(do_write(50, "n", data, 5) == 2);   // not allocated
    CHECK(do_write(file, "n", data, 5) == 2); // a regular file
    CHECK(do_mkdir(0, "n") == 2);
    CHECK(do_mkdir(file, "n") == 2);
    CHECK(same_meta(&m));
    CHECK(check_counters());
}

static void test_e_validation(void) {
    fresh();
    char data[] = "hello";
    struct Meta m;
    snapshot(&m);
    CHECK(ext2_write(NULL) == -1);
    struct EXT2DriverRequest r = {.buf = data, .name = NULL, .name_len = 1, .parent_inode = ROOT, .buffer_size = 5};
    CHECK(ext2_write(&r) == -1);
    r.name = "a";
    r.name_len = 0;
    CHECK(ext2_write(&r) == -1);
    CHECK(do_write(ROOT, "a", NULL, 5) == -1);
    CHECK(same_meta(&m));
    CHECK(check_counters());
}

static void test_f_disk_full(void) {
    fresh();
    uint32_t free_blocks = bitmap_free_blocks();
    uint32_t size = (free_blocks + 1) * BLOCK_SIZE;
    uint8_t *big = make_pattern(size);
    struct Meta m;
    snapshot(&m);
    CHECK(do_write(ROOT, "big", big, size) == -1);
    CHECK(same_meta(&m));
    CHECK(find_file_entry(ROOT, "big", 3) == 0);
    CHECK(check_counters());
    free(big);
}

static void test_g_inode_exhaustion(void) {
    fresh();
    char data[] = "x";
    char name[16];
    uint32_t created = 0;
    int8_t ret = 0;
    struct Meta m;
    for (uint32_t d = 0; ret != -1 && d < 16; d++) {
        snprintf(name, sizeof(name), "d%u", d);
        snapshot(&m);
        if ((ret = do_mkdir(ROOT, name)) != 0)
            break;
        created++;
        uint32_t dir = find_directory_entry(ROOT, name, (uint8_t) strlen(name));
        for (uint32_t f = 0; f < 300; f++) {
            snprintf(name, sizeof(name), "f%03u", f);
            snapshot(&m);
            if ((ret = do_write(dir, name, data, 1)) != 0)
                break;
            created++;
        }
    }
    printf(" created %u inodes before failure\n", created);
    CHECK(ret == -1);
    CHECK(disk_sb().s_free_inodes_count == 0);
    CHECK(created == INODES_PER_GROUP * GROUPS_COUNT - 2);
    CHECK(same_meta(&m));
    CHECK(check_counters());
}

static void test_h_parent_full(void) {
    fresh();
    char data[] = "x";
    char name[16];
    CHECK(do_mkdir(ROOT, "pf") == 0);
    uint32_t dir = find_directory_entry(ROOT, "pf", 2);
    int8_t ret = 0;
    uint32_t n = 0;
    struct Meta m;
    for (; n < 1000; n++) {
        snprintf(name, sizeof(name), "n%03u", n);
        snapshot(&m);
        if ((ret = do_write(dir, name, data, 1)) != 0)
            break;
    }
    struct EXT2Inode node;
    load_node(&node, dir);
    printf(" %u entries fit, dir i_blocks=%u\n", n, node.i_blocks);
    CHECK(ret == -1);
    CHECK(node.i_blocks == 12);
    CHECK(same_meta(&m));
    CHECK(check_counters());
}

static void test_i_folder_rollback(void) {
    fresh();
    // fill every free block with files (data + pointer blocks fit exactly)
    char name[16];
    for (uint32_t k = 0; bitmap_free_blocks() > 0 && k < 32; k++) {
        uint32_t free_blocks = bitmap_free_blocks(), n = free_blocks;
        while (n + pointer_blocks(n) > free_blocks)
            n--;
        uint8_t *buf = make_pattern(n * BLOCK_SIZE);
        snprintf(name, sizeof(name), "fill%u", k);
        CHECK(do_write(ROOT, name, buf, n * BLOCK_SIZE) == 0);
        free(buf);
    }
    CHECK(bitmap_free_blocks() == 0);

    struct Meta m;
    snapshot(&m);
    uint16_t dirs = disk_bgd(0).bg_used_dirs_count;
    CHECK(do_mkdir(ROOT, "nofit") == -1);
    CHECK(disk_bgd(0).bg_used_dirs_count == dirs);
    CHECK(same_meta(&m));
    CHECK(check_counters());
}

static void write_text(uint32_t parent, const char *name, const char *text) {
    CHECK(do_write(parent, name, (void*) text, (uint32_t) strlen(text)) == 0);
}

static void test_j_book_tree(void) {
    fresh();
    CHECK(do_mkdir(ROOT, "folder1") == 0);
    uint32_t f1 = find_directory_entry(ROOT, "folder1", 7);
    CHECK(do_mkdir(f1, "nestedf1") == 0);
    write_text(f1, "tpazolite", "tpazolite: hello from folder1\n");
    write_text(f1, "xi", "xi: hello from folder1\n");
    CHECK(do_mkdir(ROOT, "folder2") == 0);
    write_text(ROOT, "laur", "laur: hello from root\n");
    write_text(ROOT, "sasakure", "sasakure: hello from root\n");

    struct EXT2Inode node;
    load_node(&node, f1);
    printf(" folder1 = inode %u, block %u\n", f1, node.i_block[0]);
    CHECK(check_counters());
    CHECK(fake_disk_dump(TREE_DUMP));
}

static void test_k_sample_image(void) {
    fake_disk_reset();
    if (!fake_disk_load(SAMPLE_IMAGE)) {
        printf(" cannot load %s\n", SAMPLE_IMAGE);
        CHECK(false);
        return;
    }
    struct BlockBuffer before[3], after[3];
    for (uint32_t i = 0; i < 3; i++)
        read_blocks(&before[i], i, 1);
    initialize_filesystem_ext2();
    for (uint32_t i = 0; i < 3; i++)
        read_blocks(&after[i], i, 1);
    CHECK(memcmp(before, after, sizeof(before)) == 0); // not reformatted
    CHECK(check_counters());                            // image consistent with our layout?

    // which inode does the image use as root? read "." of root block 21 directly
    struct BlockBuffer b;
    read_blocks(&b, 21, 1);
    uint32_t image_root = entry_at(&b, 0)->inode;
    printf(" image root inode = %u, ext2.c ROOT_INODE = %u\n", image_root, ROOT);
    CHECK(image_root == ROOT);

    uint32_t f1 = find_directory_entry(image_root, "folder1", 7);
    printf(" folder1 via image root %u -> inode %u\n", image_root, f1);
    CHECK(f1 != 0);
    if (f1 == 0)
        return; // find_file_entry(0, ...) would underflow inode_to_bgd(0)

    const uint32_t parents[2] = {image_root, f1};
    for (int i = 0; i < 2; i++) {
        uint8_t *src = make_pattern(1500), dst[1500];
        int8_t ret = do_write(parents[i], "newfile", src, 1500);
        printf(" write to parent %u -> %d\n", parents[i], ret);
        CHECK(ret == 0);
        uint32_t inode = (ret == 0) ? find_file_entry(parents[i], "newfile", 7) : 0;
        CHECK(inode != 0);
        if (inode != 0) {
            read_back(inode, dst);
            CHECK(memcmp(src, dst, 1500) == 0);
        }
        free(src);
    }
    CHECK(check_counters());
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"a roundtrip", test_a_roundtrip},
        {"b folders", test_b_folders},
        {"c duplicates", test_c_duplicates},
        {"d invalid parent", test_d_invalid_parent},
        {"e validation", test_e_validation},
        {"f disk full", test_f_disk_full},
        {"g inode exhaustion", test_g_inode_exhaustion},
        {"h parent full", test_h_parent_full},
        {"i folder rollback", test_i_folder_rollback},
        {"j book tree", test_j_book_tree},
        {"k sample image", test_k_sample_image},
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = fails;
        printf("[%s]\n", tests[i].name);
        tests[i].fn();
        printf("=> %s\n\n", fails == before ? "OK" : "FAILED");
    }
    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}
