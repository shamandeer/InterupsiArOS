#include "header/driver/disk.h"
#include "header/cpu/portio.h"

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_FLUSH_CACHE   0xE7
// enough for QEMU / real disk, prevents hanging forever when there is no disk attached
#define ATA_TIMEOUT 1000000u

static bool ATA_busy_wait(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++)
        if (!(in(0x1F7) & ATA_STATUS_BSY))
            return true;
    return false;
}

static bool ATA_DRQ_wait(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = in(0x1F7);
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF))
            return false;
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ))
            return true;
    }
    return false;
}

static void ATA_send_command(uint32_t logical_block_address, uint8_t block_count, uint8_t command) {
    out(0x1F6, 0xE0 | ((logical_block_address >> 24) & 0xF));
    out(0x1F2, block_count);
    out(0x1F3, (uint8_t) logical_block_address);
    out(0x1F4, (uint8_t) (logical_block_address >> 8));
    out(0x1F5, (uint8_t) (logical_block_address >> 16));
    out(0x1F7, command);
}

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    uint16_t *target = (uint16_t*) ptr;
    uint32_t i = 0;
    if (ATA_busy_wait()) {
        ATA_send_command(logical_block_address, block_count, ATA_CMD_READ_SECTORS);
        for (; i < block_count; i++) {
            if (!ATA_DRQ_wait())
                break;
            for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
                target[j] = in16(0x1F0);
            target += HALF_BLOCK_SIZE;
        }
    }

    // on error the rest of the buffer is zeroed instead of left as garbage
    for (; i < block_count; i++) {
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
            target[j] = 0;
        target += HALF_BLOCK_SIZE;
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    if (!ATA_busy_wait())
        return;

    ATA_send_command(logical_block_address, block_count, ATA_CMD_WRITE_SECTORS);
    for (uint32_t i = 0; i < block_count; i++) {
        if (!ATA_DRQ_wait())
            return;
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
            out16(0x1F0, ((uint16_t*) ptr)[HALF_BLOCK_SIZE*i + j]);
    }

    // make sure the data leaves the disk write cache
    ATA_busy_wait();
    out(0x1F7, ATA_CMD_FLUSH_CACHE);
    ATA_busy_wait();
}
