#include "alefs.h"
#include <string.h>
#include <errno.h>

static int bitmap_load(struct alefs_dev *dev, uint8_t *bitmap)
{
    uint64_t start = dev->sb.bitmap_start;
    for (uint64_t i = 0; i < ALEFS_BITMAP_BLOCKS; i++) {
        int ret = alefs_dev_read(dev, start + i, bitmap + i * ALEFS_BLOCK_SIZE);
        if (ret) return ret;
    }
    return 0;
}

static int bitmap_save(struct alefs_dev *dev, const uint8_t *bitmap)
{
    uint64_t start = dev->sb.bitmap_start;
    for (uint64_t i = 0; i < ALEFS_BITMAP_BLOCKS; i++) {
        int ret = alefs_dev_write(dev, start + i, bitmap + i * ALEFS_BLOCK_SIZE);
        if (ret) return ret;
    }
    return 0;
}

int alefs_bitmap_init(struct alefs_dev *dev)
{
    uint64_t bitmap_bytes = ALEFS_BITMAP_BLOCKS * ALEFS_BLOCK_SIZE;
    uint8_t *bitmap = calloc(1, bitmap_bytes);
    if (!bitmap) return -ENOMEM;

    uint64_t start = dev->sb.bitmap_start;
    for (uint64_t i = 0; i < start; i++) {
        uint64_t byte = i / 8;
        uint64_t bit  = i % 8;
        if (byte < bitmap_bytes)
            bitmap[byte] |= (1 << bit);
    }

    for (uint64_t i = start + ALEFS_BITMAP_BLOCKS; i < dev->num_blocks; i++) {
        uint64_t byte = i / 8;
        uint64_t bit  = i % 8;
        if (byte < bitmap_bytes)
            bitmap[byte] &= ~(1 << bit);
    }

    int ret = bitmap_save(dev, bitmap);
    free(bitmap);
    return ret;
}

int alefs_bitmap_alloc(struct alefs_dev *dev, uint64_t *block)
{
    uint64_t bitmap_bytes = ALEFS_BITMAP_BLOCKS * ALEFS_BLOCK_SIZE;
    uint8_t *bitmap = calloc(1, bitmap_bytes);
    if (!bitmap) return -ENOMEM;

    int ret = bitmap_load(dev, bitmap);
    if (ret) { free(bitmap); return ret; }

    for (uint64_t i = dev->sb.data_start; i < dev->num_blocks; i++) {
        uint64_t byte = i / 8;
        uint64_t bit  = i % 8;
        if (!(bitmap[byte] & (1 << bit))) {
            bitmap[byte] |= (1 << bit);
            *block = i;
            dev->sb.free_blocks--;
            ret = bitmap_save(dev, bitmap);
            free(bitmap);
            return ret;
        }
    }

    free(bitmap);
    return -ENOSPC;
}

int alefs_bitmap_free(struct alefs_dev *dev, uint64_t block)
{
    uint64_t bitmap_bytes = ALEFS_BITMAP_BLOCKS * ALEFS_BLOCK_SIZE;
    uint8_t *bitmap = calloc(1, bitmap_bytes);
    if (!bitmap) return -ENOMEM;

    int ret = bitmap_load(dev, bitmap);
    if (ret) { free(bitmap); return ret; }

    uint64_t byte = block / 8;
    uint64_t bit  = block % 8;
    if (byte < bitmap_bytes) {
        bitmap[byte] &= ~(1 << bit);
        dev->sb.free_blocks++;
    }

    ret = bitmap_save(dev, bitmap);
    free(bitmap);
    return ret;
}

int alefs_bitmark_set(struct alefs_dev *dev, uint64_t block)
{
    uint64_t bitmap_bytes = ALEFS_BITMAP_BLOCKS * ALEFS_BLOCK_SIZE;
    uint8_t *bitmap = calloc(1, bitmap_bytes);
    if (!bitmap) return -ENOMEM;

    int ret = bitmap_load(dev, bitmap);
    if (ret) { free(bitmap); return ret; }

    uint64_t byte = block / 8;
    uint64_t bit  = block % 8;
    if (byte < bitmap_bytes)
        bitmap[byte] |= (1 << bit);

    ret = bitmap_save(dev, bitmap);
    free(bitmap);
    return ret;
}

bool alefs_bitmap_get(struct alefs_dev *dev, uint64_t block)
{
    uint64_t bitmap_bytes = ALEFS_BITMAP_BLOCKS * ALEFS_BLOCK_SIZE;
    uint8_t *bitmap = calloc(1, bitmap_bytes);
    if (!bitmap) return false;

    if (bitmap_load(dev, bitmap)) {
        free(bitmap);
        return false;
    }

    uint64_t byte = block / 8;
    uint64_t bit  = block % 8;
    bool set = false;
    if (byte < bitmap_bytes)
        set = !!(bitmap[byte] & (1 << bit));
    free(bitmap);
    return set;
}
