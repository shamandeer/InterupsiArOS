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