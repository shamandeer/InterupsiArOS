#ifndef _EXT2_H
#define _EXT2_H

#include "header/driver/disk.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


/* -- IF2130 File System constants -- */
#define BOOT_SECTOR 0 // legacy from FAT32 filesystem IF2130 OS
#define DISK_SPACE 4194304u // 4MB disk space (because our disk or storage.bin is 4MB)
#define EXT2_SUPER_MAGIC 0xEF53 // this indicating that the filesystem used by OS is ext2
#define INODE_SIZE sizeof(struct EXT2Inode) // size of inode
#define INODES_PER_TABLE (BLOCK_SIZE / INODE_SIZE) // number of inode per block (512 / )
#define GROUPS_COUNT ((BLOCK_SIZE / sizeof(struct EXT2BlockGroupDescriptor)) / 2u) // number of groups in the filesystem
#define BLOCKS_PER_GROUP (DISK_SPACE / BLOCK_SIZE / GROUPS_COUNT) // number of blocks per group
#define INODES_TABLE_BLOCK_COUNT 16u 
#define INODES_PER_GROUP (INODES_PER_TABLE * INODES_TABLE_BLOCK_COUNT) // number of inodes per group



/**
 * inodes constant 
 * - reference: https://www.nongnu.org/ext2-doc/ext2.html#inode-table
 */
#define EXT2_S_IFREG 0x8000 // regular file 
#define EXT2_S_IFDIR 0x4000 // directory


/* FILE TYPE CONSTANT*/
/**
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#linked-directories
 * - Table 4.2. Defined Inode File Type Values
 */

#define EXT2_FT_UNKNOWN 0 // Unknown File Type
#define EXT2_FT_REG_FILE 1 // Regular File
#define EXT2_FT_DIR 2 // Directory
#define EXT2_FT_NEXT 3 // Character Special File

/**
 * EXT2DriverRequest
 * Derived dand modified from FAT32DriverRequest legacy IF2130 OS 
 */
struct EXT2DriverRequest
{
    void *buf; 
    char *name; 
    uint8_t name_len; 
    uint32_t parent_inode; 
    uint32_t buffer_size; 

    bool is_directory; 
}__attribute__((packed));

/**
 * EXT2Superblock: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#superblock
 */
struct EXT2Superblock
{
    uint32_t s_inodes_count;        // 32bit value indicating the total number of inodes, both used and free, in the file system 
    uint32_t s_blocks_count;        // 32bit value indicating the total number of blocks in the system including all used, free and reserved 

    uint32_t s_r_blocks_count;      // 32bit value indicating the total number of blocks reserved for the usage of the super user. {maybe not used because there is no superuser in our system} 
    uint32_t s_free_blocks_count;   // 32bit value indicating the total number of free blocks, including the number of reserved blocks 
    uint32_t s_free_inodes_count;   // 32bit value indicating the total number of free inodes. This is a sum of all free inodes of all the block groups.
    uint32_t s_first_data_block;    // 32bit value identifying the first data block, in other word the id of the block containing the superblock structure.
    uint32_t s_first_ino;           // 32bit value indicating the first inode that can be used. Set this to 1, indicating root inode (maybe)

    uint32_t s_blocks_per_group;    
    /** 32bit value indicating the total number of blocks per group. 
     *  This value in combination with s_first_data_block can be used to determine the block groups boundaries. 
     *  Due to volume size boundaries, the last block group might have a smaller number of blocks than what is specified in this field. */

    uint32_t s_frags_per_group; 
    /**
     * 32bit value indicating the total number of fragments per group. It is also used to determine the size of the block bitmap of each block group.
     */

    uint32_t s_inodes_per_group; 
    /**
     * 32bit value indicating the total number of inodes per group. This is also used to determine the size of the inode bitmap of each block group. 
     * Note that you cannot have more than (block size in bytes * 8) inodes per group as the inode bitmap must fit within a single block. 
     * This value must be a perfect multiple of the number of inodes that can fit in a block ((1024<<s_log_block_size)/s_inode_size).
     */

    uint16_t s_magic; // 16bit value indicating the file system type. For ext2, this value is 0xEF53.(DEFINE as EXT2_SUPER_MAGIC)

    uint8_t s_prealloc_blocks; // 8bit value indicating the number of blocks to preallocate for files.
    uint8_t s_prealloc_dir_blocks; // 8bit value indicating the number of blocks to preallocate for directories.


}__attribute__((packed));


/**
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#block-group-descriptor-table
 */
struct EXT2BlockGroupDescriptor
{
    /**
     * 32bit block id of the first block of the “block bitmap” for the group represented.
     * The actual block bitmap is located within its own allocated blocks starting at the block ID specified by this value.    
     */
    uint32_t bg_block_bitmap; 

    /**
     * 32bit block id of the first block of the “inode bitmap” for the group represented.
     */
    uint32_t bg_inode_bitmap;

    /**
     * 16bit value indicating the total number of free blocks for the represented group.
     */
    uint32_t bg_inode_table;

    uint16_t bg_free_blocks_count;

    /**
     * 16bit value indicating the total number of free inodes for the represented group.
     */
    uint16_t bg_free_inodes_count;

    /**
     * 16bit value indicating the number of inodes allocated to directories for the represented group.
     */
    uint16_t bg_used_dirs_count;

    /**
     * 16bit value used for padding the structure on a 32bit boundary.
     */
    uint16_t bg_pad;

    /**
     * 12 bytes of reserved space for future revisions.
     */
    uint32_t bg_reserved[3]; // 12 bytes of reserved space for future revisions. 
}__attribute__((packed));

/**
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#block-group-descriptor-table
 */
struct EXT2BlockGroupDescriptorTable
{
    struct EXT2BlockGroupDescriptor table[GROUPS_COUNT]; // can be change with fixed size array
};


/**
 * EXT2Inode
 * Inode stands for index node, it is a data structure in a Unix-style file system that describes a file-system object such as a file or a directory.
 */

struct EXT2Inode
{
    uint16_t i_mode; // 16bit value indicating the file type and the access rights.
    uint32_t i_size; // 32bit value indicating the size of the file in bytes.
    uint32_t i_blocks; // 32bit value indicating the number of blocks used by the file.

    /**
     * 15 x 32bit block numbers pointing to the blocks containing the data for this inode
     * 
     * - The first 12 blocks are direct blocks
     * - The 13th entry in this array is the block number of the first indirect block which is a block containing an array of block ID containing the data
     * Therefore, the 13th block of the file will be the first block ID contained in the indirect block. With a 1KiB block size, blocks 13 to 268 of the file data are contained in this indirect block.
     * - The 14th entry in this array is the block number of the first doubly-indirect block
     * - The 15th entry in this array is the block number of the triply-indirect block
     * 
     * maybe this video will help
     * - https://www.youtube.com/watch?v=tMVj22EWg6A
     *  
     */
    uint32_t i_block[15];

}__attribute__((packed));

struct EXT2InodeTable
{
    struct EXT2Inode table[INODES_PER_GROUP]; // can be change with fixed size array
};

/**
 * EXT2DirectoryEntry
 * Linked List Directory
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#linked-directories
 */

struct EXT2DirectoryEntry
{
    uint32_t inode; // 32bit value indicating the inode number of the file entry. A value of 0 indicate that the entry is not used.
    /**
     * 16bit unsigned displacement to the next directory entry from the start of the current directory entry.
     * This field must have a value at least equal to the length of the current record.
     * The directory entries must be aligned on 4 bytes boundaries and there cannot be any directory entry spanning multiple data blocks.
     * If an entry cannot completely fit in one block, it must be pushed to the next data block and the rec_len of the previous entry properly adjusted.
     */
    uint16_t rec_len; 

    /**
     * 8bit value indicating the length of the file name.
     */
    uint16_t name_len;

    /**
     * 8bit unsigned value used to indicate file type.
     */
    uint8_t file_type;

}__attribute__((packed));

/**
 *  REGULAR function
 */

/**
 * get the name of the entry
 * @param entry the directory entry
 * @return the name of the entry
 */
char *get_entry_name(void *entry);

/**
 * get the directory entry from the buffer
 * @param ptr the buffer that contains the directory table
 * @param offset the offset of the entry 
 * @return the directory entry
 */
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset);

/**
 * get the next directory entry from the current entry
 * @param entry the current entry
 * @return the next directory entry
 */
struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry);

/**
 * get the record length of the entry
 * @param name_len the length of the name of the entry
 * @return the record length of the entry
 */
uint16_t get_entry_record_len(uint8_t name_len);

/**
 * get the offset of the first child of the directory
 * @param ptr the buffer that contains the directory table
 * @return the offset of the first child of the directory
 */
uint32_t get_dir_first_child_offset(void *ptr);


/* =================== MAIN FUNCTION OF EXT32 FILESYSTEM ============================*/

/**
 * @brief get bgd index from inode, inode will starts at index 1
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return bgd index (0 to GROUP_COUNT - 1)
 */
uint32_t inode_to_bgd(uint32_t inode);

/**
 * @brief get inode local index in the corrresponding bgd
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return local index
 */
uint32_t inode_to_local(uint32_t inode);

/**
 * @brief create a new directory using given node
 * first item of directory table is its node location (name will be .)
 * second item of directory is its parent location (name will be ..)
 * @param node pointer of inode
 * @param inode inode that already allocated
 * @param parent_inode inode of parent directory (if root directory, the parent is itself)
 */
void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode);
/**
 * @brief check whether filesystem signature is missing or not in boot sector
 *
 * @return true if memcmp(boot_sector, fs_signature) returning inequality
 */
bool is_empty_storage(void);

/**
 * @brief create a new EXT2 filesystem. Will write fs_signature into boot sector,
 * initialize super block, bgd table, block and inode bitmap, and create root directory
 */
void create_ext2(void);

/**
 * @brief Initialize file system driver state, if is_empty_storage() then create_ext2()
 * Else, read and cache super block (located at block 1) and bgd table (located at block 2) into state
 */
void initialize_filesystem_ext2(void);

/**
 * @brief check whether a directory table has children or not
 * @param inode of a directory table
 * @return true if first_child_entry->inode = 0
 */
bool is_directory_empty(uint32_t inode);

/**
 * @brief load a directory inode and validate it, used for checking parent_inode on CRUD request
 * @param node buffer for the loaded inode
 * @param inode inode number of the directory
 * @return false if inode is out of range, not allocated in inode bitmap, not a directory, or has no data block
 */
bool load_directory_node(struct EXT2Inode *node, uint32_t inode);

/**
 * @brief search an entry by name inside a directory (compare name_len first, then memcmp)
 * @param dir_inode inode of the directory, must be valid (see load_directory_node)
 * @param name entry name, does not need to be null-terminated
 * @param name_len length of the name
 * @return inode of the entry, 0 if not found
 */
uint32_t find_directory_entry(uint32_t dir_inode, const char *name, uint8_t name_len);

/**
 * @brief add a new entry to a directory. Entry is placed on the first slot that fits
 * by splitting rec_len of an existing entry. If every block is full, a new block is allocated
 * for the directory (direct block only, so a directory can have at most 12 blocks)
 * Disk effect: directory block, and if it grows also block bitmap & directory inode
 * @param dir_inode inode of the directory
 * @param inode inode of the new entry
 * @param name entry name, does not need to be null-terminated
 * @param name_len length of the name
 * @param file_type EXT2_FT_REG_FILE or EXT2_FT_DIR
 * @return false if directory already has 12 blocks or there is no free block
 */
bool add_directory_entry(uint32_t dir_inode, uint32_t inode, const char *name, uint8_t name_len, uint8_t file_type);

/**
 * @brief remove an entry from a directory, its rec_len is merged into the previous entry
 * (or inode set to 0 if it is the first entry of a block). Entry . and .. can not be removed
 * Disk effect: directory block
 * @param dir_inode inode of the directory
 * @param name entry name, does not need to be null-terminated
 * @param name_len length of the name
 * @return true if entry found and removed
 */
bool remove_directory_entry(uint32_t dir_inode, const char *name, uint8_t name_len);



/* =============================== CRUD FUNC ======================================== */

/**
 * @brief EXT2 Folder / Directory read
 * @param request buf point to struct EXT2 Directory
 * @return Error code: 0 success - 1 not a folder - 2 not found - 3 parent folder invalid - -1 unknown
 */
int8_t read_directory(struct EXT2DriverRequest *prequest);

/**
 * @brief EXT2 read, read a file from file system
 * @param request All attribute will be used except is_dir for read, buffer_size will limit reading count
 * @return Error code: 0 success - 1 not a file - 2 not enough buffer - 3 not found - 4 parent folder invalid - -1 unknown
 */
int8_t read(struct EXT2DriverRequest request);

/**
 * @brief EXT2 write, write a file or a folder to file system
 *
 * @param All attribute will be used for write except is_dir, buffer_size == 0 then create a folder / directory. It is possible that exist file with name same as a folder
 * @return Error code: 0 success - 1 file/folder already exist - 2 invalid parent folder - -1 unknown
 */
int8_t write(struct EXT2DriverRequest *request);

/**
 * @brief EXT2 delete, delete a file or empty directory in file system
 *  @param request buf and buffer_size is unused, is_dir == true means delete folder (possible file with name same as folder)
 * @return Error code: 0 success - 1 not found - 2 folder is not empty - 3 parent folder invalid -1 unknown
 */
int8_t delete(struct EXT2DriverRequest request);

/* =============================== MEMORY ==========================================*/

/**
 * NOTE: every helper below writes bitmap / inode table / data block directly to the disk,
 * but free counters on superblock & bgd table are only updated in memory.
 * Call commit_metadata() at the end of a CRUD operation.
 */

/**
 * @brief get a free inode from inode bitmap (first fit), mark it as used
 * Disk effect: inode bitmap
 * @param is_directory true if the inode will be used for a directory (bg_used_dirs_count)
 * @return new inode, 0 if there is no free inode
 */
uint32_t allocate_node(bool is_directory);

/**
 * @brief deallocate node from the disk, will also deallocate its used blocks
 * also all of the blocks of indirect blocks if necessary. Entry on the parent directory is not removed
 * Disk effect: block bitmap, inode bitmap, inode table (entry is zeroed)
 * @param inode that needs to be deallocated
 */
void deallocate_node(uint32_t inode);

/**
 * @brief deallocate node blocks
 * Disk effect: block bitmap
 * @param loc node->i_block
 * @param blocks number of data blocks (node->i_blocks)
 */
void deallocate_blocks(void *loc, uint32_t blocks);

/**
 * @brief get a free block from block bitmap using first fit, search starts from prefered_bgd
 * Disk effect: block bitmap
 * @param prefered_bgd bgd index where the search starts
 * @return block number, 0 if disk is full
 */
uint32_t allocate_block(uint32_t prefered_bgd);

/**
 * @brief mark a block as free on block bitmap, block 0 is ignored
 * Disk effect: block bitmap
 * @param block block number
 */
void deallocate_block(uint32_t block);

/**
 * @brief write node->block in the given node, will allocate
 * exactly node->i_blocks data blocks (first fit), if first 12 item of node->i_block
 * is not enough, will use indirect blocks. Unused pointer will be 0
 * node->i_size bytes are copied from ptr, the rest of the last block is filled with 0
 * Node is not synced, call sync_node() after this
 * Disk effect: block bitmap, data blocks, indirect blocks
 * @param ptr the buffer that needs to be written, NULL will write zeroed blocks
 * @param node pointer of the node, i_blocks and i_size must be set
 * @param prefered_bgd it is located at the node inode bgd
 * @return false if there is not enough free block (nothing is allocated)
 *
 * @attention only implement until doubly indirect block, if you want to implement triply indirect block please increase the storage size to at least 256MB
 */
bool allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd);

/**
 * @brief read all data of a node in order (direct, then indirect) into ptr
 * @param ptr destination buffer, size must be at least node->i_size
 * @param node pointer of the node
 */
void read_node_blocks(void *ptr, struct EXT2Inode *node);

/**
 * @brief load the node from the disk
 * @param node buffer for the loaded node
 * @param inode location of the node
 */
void load_node(struct EXT2Inode *node, uint32_t inode);

/**
 * @brief update the node to the disk
 * Disk effect: inode table
 * @param node pointer of node
 * @param inode location of the node
 */
void sync_node(struct EXT2Inode *node, uint32_t inode);

/**
 * @brief write superblock (block 1) and bgd table (block 2) from memory to the disk
 * Disk effect: superblock, bgd table
 */
void commit_metadata(void);

#endif
