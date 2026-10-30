// RAM-backed replacement for src/driver/disk.c, host tests only
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "header/driver/disk.h"

#define FAKE_DISK_SIZE (8u * 1024u * 1024u)

static uint8_t disk[FAKE_DISK_SIZE];

static void check_range(uint32_t lba, uint8_t count) {
    if ((uint64_t) (lba + (uint64_t) count) * BLOCK_SIZE > FAKE_DISK_SIZE) {
        fprintf(stderr, "fake_disk: out of range lba=%u count=%u\n", lba, count);
        exit(2);
    }
}

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    check_range(logical_block_address, block_count);
    memcpy(ptr, disk + (size_t) logical_block_address * BLOCK_SIZE, (size_t) block_count * BLOCK_SIZE);
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    check_range(logical_block_address, block_count);
    memcpy(disk + (size_t) logical_block_address * BLOCK_SIZE, ptr, (size_t) block_count * BLOCK_SIZE);
}

void fake_disk_reset(void) {
    memset(disk, 0, sizeof(disk));
}

uint8_t *fake_disk_raw(void) {
    return disk;
}

bool fake_disk_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    memset(disk, 0, sizeof(disk));
    size_t n = fread(disk, 1, sizeof(disk), f);
    fclose(f);
    return n >= 4u * 1024u * 1024u; // 4 MiB ext2 image is enough, swap area stays zero
}
