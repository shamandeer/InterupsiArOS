#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include <stdint.h>
#include <stdbool.h>
#include "header/stdlib/string.h"
const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

#define ROOT_INODE 2u
#define GROUP0_FIRST_META 3u
#define GROUP_META_BLOCKS (2u + INODES_TABLE_BLOCK_COUNT)

static struct EXT2Superblock fs_superblock;
static struct EXT2BlockGroupDescriptorTable fs_group_descriptor_table;

/* [A] INITIALIZER */

char *get_entry_name(void *entry) {
    return (char*) entry + sizeof(struct EXT2DirectoryEntry);
}

uint16_t get_entry_record_len(uint8_t name_len) {
    return (uint16_t) ((sizeof(struct EXT2DirectoryEntry) + name_len + 3u) & ~3u);
}

uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1u) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1u) % INODES_PER_GROUP;
}

void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode) {
    struct BlockBuffer buffer;
    memset(&buffer, 0, sizeof(buffer));

    struct EXT2DirectoryEntry self;
    self.inode = inode;
    self.rec_len = get_entry_record_len(1);
    self.name_len = 1;
    self.file_type = EXT2_FT_DIR;
    memcpy(buffer.buf, &self, sizeof(self));
    memcpy(get_entry_name(buffer.buf), ".", 1);

    struct EXT2DirectoryEntry parent;
    parent.inode = parent_inode;
    parent.rec_len = (uint16_t) (BLOCK_SIZE - self.rec_len);
    parent.name_len = 2;
    parent.file_type = EXT2_FT_DIR;
    memcpy(buffer.buf + self.rec_len, &parent, sizeof(parent));
    memcpy(get_entry_name(buffer.buf + self.rec_len), "..", 2);

    node->i_mode = EXT2_S_IFDIR;
    node->i_size = BLOCK_SIZE;
    node->i_blocks = 1;
    write_blocks(&buffer, node->i_block[0], 1);
}

bool is_empty_storage(void) {
    struct BlockBuffer boot_sector;
    read_blocks(&boot_sector, BOOT_SECTOR, 1);
    return memcmp(boot_sector.buf, fs_signature, BLOCK_SIZE) != 0;
}

static void set_bit(struct BlockBuffer *bitmap, uint32_t index) {
    bitmap->buf[index / 8] |= (uint8_t) (1u << (index % 8));
}

void create_ext2(void) {
    struct BlockBuffer buffer;
    struct BlockBuffer zero;
    memset(&zero, 0, sizeof(zero));

    write_blocks(fs_signature, BOOT_SECTOR, 1);

    memset(&fs_group_descriptor_table, 0, sizeof(fs_group_descriptor_table));
    uint32_t root_block = GROUP0_FIRST_META + GROUP_META_BLOCKS;
    uint32_t total_free_blocks = 0;

    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        uint32_t base = (g == 0) ? GROUP0_FIRST_META : g * BLOCKS_PER_GROUP;
        uint32_t group_start = g * BLOCKS_PER_GROUP;
        uint32_t used_blocks = (base - group_start) + GROUP_META_BLOCKS;
        if (g == 0)
            used_blocks++;

        struct EXT2BlockGroupDescriptor *bgd = &fs_group_descriptor_table.table[g];
        bgd->bg_block_bitmap = base;
        bgd->bg_inode_bitmap = base + 1;
        bgd->bg_inode_table = base + 2;
        bgd->bg_free_blocks_count = (uint16_t) (BLOCKS_PER_GROUP - used_blocks);
        bgd->bg_free_inodes_count = (uint16_t) (INODES_PER_GROUP - (g == 0 ? 2u : 0u));
        bgd->bg_used_dirs_count = (g == 0) ? 1 : 0;
        total_free_blocks += bgd->bg_free_blocks_count;

        memset(&buffer, 0, sizeof(buffer));
        for (uint32_t b = 0; b < used_blocks; b++)
            set_bit(&buffer, b);
        write_blocks(&buffer, bgd->bg_block_bitmap, 1);

        memset(&buffer, 0, sizeof(buffer));
        if (g == 0) {
            set_bit(&buffer, 0);
            set_bit(&buffer, inode_to_local(ROOT_INODE));
        }
        write_blocks(&buffer, bgd->bg_inode_bitmap, 1);

        for (uint32_t t = 0; t < INODES_TABLE_BLOCK_COUNT; t++)
            write_blocks(&zero, bgd->bg_inode_table + t, 1);
    }

    memset(&buffer, 0, sizeof(buffer));
    memcpy(buffer.buf, &fs_group_descriptor_table, sizeof(fs_group_descriptor_table));
    write_blocks(&buffer, 2, 1);

    memset(&fs_superblock, 0, sizeof(fs_superblock));
    fs_superblock.s_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    fs_superblock.s_blocks_count = DISK_SPACE / BLOCK_SIZE;
    fs_superblock.s_free_blocks_count = total_free_blocks;
    fs_superblock.s_free_inodes_count = INODES_PER_GROUP * GROUPS_COUNT - 2u;
    fs_superblock.s_first_data_block = 1;
    fs_superblock.s_first_ino = 1;
    fs_superblock.s_blocks_per_group = BLOCKS_PER_GROUP;
    fs_superblock.s_frags_per_group = BLOCKS_PER_GROUP;
    fs_superblock.s_inodes_per_group = INODES_PER_GROUP;
    fs_superblock.s_magic = EXT2_SUPER_MAGIC;
    memset(&buffer, 0, sizeof(buffer));
    memcpy(buffer.buf, &fs_superblock, sizeof(fs_superblock));
    write_blocks(&buffer, 1, 1);

    struct EXT2Inode root;
    memset(&root, 0, sizeof(root));
    root.i_block[0] = root_block;
    init_directory_table(&root, ROOT_INODE, ROOT_INODE);

    uint32_t local = inode_to_local(ROOT_INODE);
    uint32_t table_block = fs_group_descriptor_table.table[inode_to_bgd(ROOT_INODE)].bg_inode_table + local / INODES_PER_TABLE;
    memset(&buffer, 0, sizeof(buffer));
    memcpy(buffer.buf + (local % INODES_PER_TABLE) * INODE_SIZE, &root, sizeof(root));
    write_blocks(&buffer, table_block, 1);
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    } else {
        struct BlockBuffer buffer;
        read_blocks(&buffer, 1, 1);
        memcpy(&fs_superblock, buffer.buf, sizeof(fs_superblock));
        read_blocks(&buffer, 2, 1);
        memcpy(&fs_group_descriptor_table, buffer.buf, sizeof(fs_group_descriptor_table));
    }
}

/* [B] CORE HELPERS */

#define DIRECT_BLOCKS 12u
#define POINTERS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))
#define MAX_NODE_BLOCKS (DIRECT_BLOCKS + POINTERS_PER_BLOCK + POINTERS_PER_BLOCK * POINTERS_PER_BLOCK)

// position while walking node data blocks (direct -> single -> double indirect)
struct BlockCursor {
    uint8_t *buf;
    uint32_t bytes_left;
    uint32_t blocks_left;
};

static bool test_bit(struct BlockBuffer *bitmap, uint32_t index) {
    return (bitmap->buf[index / 8] >> (index % 8)) & 1u;
}

static void clear_bit(struct BlockBuffer *bitmap, uint32_t index) {
    bitmap->buf[index / 8] &= (uint8_t) ~(1u << (index % 8));
}

static uint32_t inode_table_block(uint32_t inode) {
    return fs_group_descriptor_table.table[inode_to_bgd(inode)].bg_inode_table + inode_to_local(inode) / INODES_PER_TABLE;
}

static uint32_t inode_table_offset(uint32_t inode) {
    return (inode_to_local(inode) % INODES_PER_TABLE) * INODE_SIZE;
}

static bool is_inode_allocated(uint32_t inode) {
    if (inode == 0 || inode > INODES_PER_GROUP * GROUPS_COUNT)
        return false;

    struct BlockBuffer bitmap;
    read_blocks(&bitmap, fs_group_descriptor_table.table[inode_to_bgd(inode)].bg_inode_bitmap, 1);
    return test_bit(&bitmap, inode_to_local(inode));
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry*) ((uint8_t*) ptr + offset);
}

struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    return get_directory_entry(entry, entry->rec_len);
}

uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry *self = get_directory_entry(ptr, 0);
    struct EXT2DirectoryEntry *parent = get_next_directory_entry(self);
    return self->rec_len + parent->rec_len;
}

static bool is_entry_in_block(struct BlockBuffer *block, uint32_t offset) {
    if (offset + sizeof(struct EXT2DirectoryEntry) > BLOCK_SIZE)
        return false;

    struct EXT2DirectoryEntry *entry = get_directory_entry(block->buf, offset);
    return entry->rec_len != 0 && offset + entry->rec_len <= BLOCK_SIZE;
}

static bool is_entry_name(struct EXT2DirectoryEntry *entry, const char *name, uint8_t name_len) {
    return entry->inode != 0 && entry->name_len == name_len && memcmp(get_entry_name(entry), name, name_len) == 0;
}

static bool is_dot_entry(const char *name, uint8_t name_len) {
    return (name_len == 1 && name[0] == '.') || (name_len == 2 && name[0] == '.' && name[1] == '.');
}

static void fill_directory_entry(struct EXT2DirectoryEntry *entry, uint32_t inode, const char *name, uint8_t name_len, uint8_t file_type) {
    char *entry_name = get_entry_name(entry);
    entry->inode = inode;
    entry->name_len = name_len;
    entry->file_type = file_type;
    memcpy(entry_name, name, name_len);
    memset(entry_name + name_len, 0, entry->rec_len - sizeof(struct EXT2DirectoryEntry) - name_len);
}

void load_node(struct EXT2Inode *node, uint32_t inode) {
    struct BlockBuffer buffer;
    read_blocks(&buffer, inode_table_block(inode), 1);
    memcpy(node, buffer.buf + inode_table_offset(inode), sizeof(struct EXT2Inode));
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    struct BlockBuffer buffer;
    uint32_t block = inode_table_block(inode);
    read_blocks(&buffer, block, 1);
    memcpy(buffer.buf + inode_table_offset(inode), node, sizeof(struct EXT2Inode));
    write_blocks(&buffer, block, 1);
}

void commit_metadata(void) {
    struct BlockBuffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    memcpy(buffer.buf, &fs_superblock, sizeof(fs_superblock));
    write_blocks(&buffer, 1, 1);

    memset(&buffer, 0, sizeof(buffer));
    memcpy(buffer.buf, &fs_group_descriptor_table, sizeof(fs_group_descriptor_table));
    write_blocks(&buffer, 2, 1);
}

uint32_t allocate_node(bool is_directory) {
    struct BlockBuffer bitmap;
    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        struct EXT2BlockGroupDescriptor *bgd = &fs_group_descriptor_table.table[g];
        read_blocks(&bitmap, bgd->bg_inode_bitmap, 1);
        for (uint32_t local = 0; local < INODES_PER_GROUP; local++) {
            if (test_bit(&bitmap, local))
                continue;

            set_bit(&bitmap, local);
            write_blocks(&bitmap, bgd->bg_inode_bitmap, 1);
            bgd->bg_free_inodes_count--;
            fs_superblock.s_free_inodes_count--;
            if (is_directory)
                bgd->bg_used_dirs_count++;
            return g * INODES_PER_GROUP + local + 1;
        }
    }
    return 0;
}

void deallocate_node(uint32_t inode) {
    if (inode == ROOT_INODE || !is_inode_allocated(inode))
        return;

    struct EXT2Inode node;
    load_node(&node, inode);
    deallocate_blocks(node.i_block, node.i_blocks);

    struct EXT2BlockGroupDescriptor *bgd = &fs_group_descriptor_table.table[inode_to_bgd(inode)];
    struct BlockBuffer bitmap;
    read_blocks(&bitmap, bgd->bg_inode_bitmap, 1);
    clear_bit(&bitmap, inode_to_local(inode));
    write_blocks(&bitmap, bgd->bg_inode_bitmap, 1);
    bgd->bg_free_inodes_count++;
    fs_superblock.s_free_inodes_count++;
    if (node.i_mode & EXT2_S_IFDIR)
        bgd->bg_used_dirs_count--;

    memset(&node, 0, sizeof(node));
    sync_node(&node, inode);
}

uint32_t allocate_block(uint32_t prefered_bgd) {
    struct BlockBuffer bitmap;
    for (uint32_t i = 0; i < GROUPS_COUNT; i++) {
        uint32_t g = (prefered_bgd + i) % GROUPS_COUNT;
        struct EXT2BlockGroupDescriptor *bgd = &fs_group_descriptor_table.table[g];
        read_blocks(&bitmap, bgd->bg_block_bitmap, 1);
        for (uint32_t b = 0; b < BLOCKS_PER_GROUP; b++) {
            if (test_bit(&bitmap, b))
                continue;

            set_bit(&bitmap, b);
            write_blocks(&bitmap, bgd->bg_block_bitmap, 1);
            bgd->bg_free_blocks_count--;
            fs_superblock.s_free_blocks_count--;
            return g * BLOCKS_PER_GROUP + b;
        }
    }
    return 0;
}

void deallocate_block(uint32_t block) {
    if (block == 0 || block >= GROUPS_COUNT * BLOCKS_PER_GROUP)
        return;

    struct EXT2BlockGroupDescriptor *bgd = &fs_group_descriptor_table.table[block / BLOCKS_PER_GROUP];
    struct BlockBuffer bitmap;
    read_blocks(&bitmap, bgd->bg_block_bitmap, 1);
    if (!test_bit(&bitmap, block % BLOCKS_PER_GROUP))
        return;

    clear_bit(&bitmap, block % BLOCKS_PER_GROUP);
    write_blocks(&bitmap, bgd->bg_block_bitmap, 1);
    bgd->bg_free_blocks_count++;
    fs_superblock.s_free_blocks_count++;
}

// counted from the bitmap, free counter on bgd could be out of sync with the bitmap
static uint32_t count_free_blocks(void) {
    struct BlockBuffer bitmap;
    uint32_t free_blocks = 0;
    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        read_blocks(&bitmap, fs_group_descriptor_table.table[g].bg_block_bitmap, 1);
        for (uint32_t b = 0; b < BLOCKS_PER_GROUP; b++)
            if (!test_bit(&bitmap, b))
                free_blocks++;
    }
    return free_blocks;
}

static uint32_t count_pointer_blocks(uint32_t blocks) {
    if (blocks <= DIRECT_BLOCKS)
        return 0;

    blocks -= DIRECT_BLOCKS;
    if (blocks <= POINTERS_PER_BLOCK)
        return 1;

    blocks -= POINTERS_PER_BLOCK;
    return 2 + (blocks + POINTERS_PER_BLOCK - 1) / POINTERS_PER_BLOCK;
}

static uint32_t write_block_tree(struct BlockCursor *cursor, uint32_t depth, uint32_t prefered_bgd) {
    uint32_t block = allocate_block(prefered_bgd);
    if (depth == 0) {
        struct BlockBuffer data;
        uint32_t size = (cursor->bytes_left < BLOCK_SIZE) ? cursor->bytes_left : BLOCK_SIZE;
        memset(&data, 0, sizeof(data));
        if (cursor->buf != NULL) {
            memcpy(data.buf, cursor->buf, size);
            cursor->buf += size;
        }
        cursor->bytes_left -= size;
        cursor->blocks_left--;
        write_blocks(&data, block, 1);
        return block;
    }

    uint32_t pointers[POINTERS_PER_BLOCK];
    memset(pointers, 0, sizeof(pointers));
    for (uint32_t i = 0; i < POINTERS_PER_BLOCK && cursor->blocks_left > 0; i++)
        pointers[i] = write_block_tree(cursor, depth - 1, prefered_bgd);
    write_blocks(pointers, block, 1);
    return block;
}

static void read_block_tree(struct BlockCursor *cursor, uint32_t block, uint32_t depth) {
    if (depth == 0) {
        struct BlockBuffer data;
        uint32_t size = (cursor->bytes_left < BLOCK_SIZE) ? cursor->bytes_left : BLOCK_SIZE;
        read_blocks(&data, block, 1);
        memcpy(cursor->buf, data.buf, size);
        cursor->buf += size;
        cursor->bytes_left -= size;
        cursor->blocks_left--;
        return;
    }

    uint32_t pointers[POINTERS_PER_BLOCK];
    read_blocks(pointers, block, 1);
    for (uint32_t i = 0; i < POINTERS_PER_BLOCK && cursor->blocks_left > 0; i++)
        read_block_tree(cursor, pointers[i], depth - 1);
}

static void deallocate_block_tree(uint32_t block, uint32_t depth, uint32_t *blocks_left) {
    if (depth == 0) {
        (*blocks_left)--;
    } else if (block != 0) {
        uint32_t pointers[POINTERS_PER_BLOCK];
        read_blocks(pointers, block, 1);
        for (uint32_t i = 0; i < POINTERS_PER_BLOCK && *blocks_left > 0; i++)
            deallocate_block_tree(pointers[i], depth - 1, blocks_left);
    }
    deallocate_block(block);
}

void deallocate_blocks(void *loc, uint32_t blocks) {
    // i_block on packed EXT2Inode is not 4-byte aligned, copy it first
    uint32_t locations[DIRECT_BLOCKS + 3];
    memcpy(locations, loc, sizeof(locations));
    for (uint32_t i = 0; i < DIRECT_BLOCKS && blocks > 0; i++)
        deallocate_block_tree(locations[i], 0, &blocks);
    for (uint32_t depth = 1; depth <= 2 && blocks > 0; depth++)
        deallocate_block_tree(locations[DIRECT_BLOCKS + depth - 1], depth, &blocks);
}

bool allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd) {
    uint32_t blocks = node->i_blocks;
    if (blocks > MAX_NODE_BLOCKS || blocks + count_pointer_blocks(blocks) > count_free_blocks())
        return false;

    struct BlockCursor cursor = {
        .buf         = (uint8_t*) ptr,
        .bytes_left  = node->i_size,
        .blocks_left = blocks,
    };
    memset(node->i_block, 0, sizeof(node->i_block));
    for (uint32_t i = 0; i < DIRECT_BLOCKS && cursor.blocks_left > 0; i++)
        node->i_block[i] = write_block_tree(&cursor, 0, prefered_bgd);
    for (uint32_t depth = 1; depth <= 2 && cursor.blocks_left > 0; depth++)
        node->i_block[DIRECT_BLOCKS + depth - 1] = write_block_tree(&cursor, depth, prefered_bgd);
    return true;
}

void read_node_blocks(void *ptr, struct EXT2Inode *node) {
    struct BlockCursor cursor = {
        .buf         = (uint8_t*) ptr,
        .bytes_left  = node->i_size,
        .blocks_left = node->i_blocks,
    };
    for (uint32_t i = 0; i < DIRECT_BLOCKS && cursor.blocks_left > 0; i++)
        read_block_tree(&cursor, node->i_block[i], 0);
    for (uint32_t depth = 1; depth <= 2 && cursor.blocks_left > 0; depth++)
        read_block_tree(&cursor, node->i_block[DIRECT_BLOCKS + depth - 1], depth);
}

bool is_directory_empty(uint32_t inode) {
    struct EXT2Inode dir;
    struct BlockBuffer block;
    load_node(&dir, inode);
    for (uint32_t i = 0; i < dir.i_blocks && i < DIRECT_BLOCKS; i++) {
        read_blocks(&block, dir.i_block[i], 1);
        uint32_t offset = (i == 0) ? get_dir_first_child_offset(block.buf) : 0;
        while (is_entry_in_block(&block, offset)) {
            struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, offset);
            if (entry->inode != 0)
                return false;
            offset += entry->rec_len;
        }
    }
    return true;
}

bool load_directory_node(struct EXT2Inode *node, uint32_t inode) {
    if (!is_inode_allocated(inode))
        return false;

    load_node(node, inode);
    return (node->i_mode & EXT2_S_IFDIR) && node->i_blocks > 0 && node->i_block[0] != 0;
}

uint32_t find_directory_entry(uint32_t dir_inode, const char *name, uint8_t name_len) {
    struct EXT2Inode dir;
    struct BlockBuffer block;
    load_node(&dir, dir_inode);
    for (uint32_t i = 0; i < dir.i_blocks && i < DIRECT_BLOCKS; i++) {
        read_blocks(&block, dir.i_block[i], 1);
        uint32_t offset = 0;
        while (is_entry_in_block(&block, offset)) {
            struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, offset);
            if (is_entry_name(entry, name, name_len))
                return entry->inode;
            offset += entry->rec_len;
        }
    }
    return 0;
}

bool add_directory_entry(uint32_t dir_inode, uint32_t inode, const char *name, uint8_t name_len, uint8_t file_type) {
    struct EXT2Inode dir;
    struct BlockBuffer block;
    uint16_t needed = get_entry_record_len(name_len);
    load_node(&dir, dir_inode);

    for (uint32_t i = 0; i < dir.i_blocks && i < DIRECT_BLOCKS; i++) {
        read_blocks(&block, dir.i_block[i], 1);
        uint32_t offset = 0;
        while (is_entry_in_block(&block, offset)) {
            struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, offset);
            uint16_t used = (entry->inode == 0) ? 0 : get_entry_record_len(entry->name_len);
            if (entry->rec_len >= used + needed) {
                // take the unused tail of this entry as the new entry
                if (used != 0) {
                    struct EXT2DirectoryEntry *next = get_directory_entry(entry, used);
                    next->rec_len = (uint16_t) (entry->rec_len - used);
                    entry->rec_len = used;
                    entry = next;
                }
                fill_directory_entry(entry, inode, name, name_len, file_type);
                write_blocks(&block, dir.i_block[i], 1);
                return true;
            }
            offset += entry->rec_len;
        }
    }

    if (dir.i_blocks >= DIRECT_BLOCKS)
        return false;

    uint32_t new_block = allocate_block(inode_to_bgd(dir_inode));
    if (new_block == 0)
        return false;

    memset(&block, 0, sizeof(block));
    struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, 0);
    entry->rec_len = BLOCK_SIZE;
    fill_directory_entry(entry, inode, name, name_len, file_type);
    write_blocks(&block, new_block, 1);

    dir.i_block[dir.i_blocks] = new_block;
    dir.i_blocks++;
    dir.i_size += BLOCK_SIZE;
    sync_node(&dir, dir_inode);
    return true;
}

bool remove_directory_entry(uint32_t dir_inode, const char *name, uint8_t name_len) {
    if (is_dot_entry(name, name_len))
        return false;

    struct EXT2Inode dir;
    struct BlockBuffer block;
    load_node(&dir, dir_inode);
    for (uint32_t i = 0; i < dir.i_blocks && i < DIRECT_BLOCKS; i++) {
        read_blocks(&block, dir.i_block[i], 1);
        uint32_t offset = 0;
        struct EXT2DirectoryEntry *prev = NULL;
        while (is_entry_in_block(&block, offset)) {
            struct EXT2DirectoryEntry *entry = get_directory_entry(block.buf, offset);
            if (is_entry_name(entry, name, name_len)) {
                if (prev == NULL)
                    entry->inode = 0;
                else
                    prev->rec_len += entry->rec_len;
                write_blocks(&block, dir.i_block[i], 1);
                return true;
            }
            prev = entry;
            offset += entry->rec_len;
        }
    }
    return false;
}
