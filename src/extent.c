#include "alefs.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

int alefs_extent_alloc(struct alefs_dev *dev, uint64_t count, struct alefs_extent *ext)
{
    uint64_t start;
    int ret = alefs_bitmap_alloc(dev, &start);
    if (ret) return ret;

    ext->start = start;
    ext->count = 1;

    for (uint64_t i = 1; i < count; i++) {
        uint64_t b;
        ret = alefs_bitmap_alloc(dev, &b);
        if (ret) break;
        ext->count++;
    }

    return 0;
}

int alefs_extent_free(struct alefs_dev *dev, const struct alefs_extent *ext)
{
    for (uint64_t i = 0; i < ext->count; i++) {
        int ret = alefs_bitmap_free(dev, ext->start + i);
        if (ret) return ret;
    }
    return 0;
}

static int find_extent(const struct alefs_inode *inode, uint64_t block_offset,
                       uint64_t *ext_idx, uint64_t *ext_block_offset)
{
    uint64_t accumulated = 0;
    for (uint32_t i = 0; i < inode->extent_count; i++) {
        if (block_offset < accumulated + inode->extents[i].count) {
            *ext_idx = i;
            *ext_block_offset = block_offset - accumulated;
            return 0;
        }
        accumulated += inode->extents[i].count;
    }
    return -ENOENT;
}

static uint64_t total_extent_blocks(const struct alefs_inode *inode)
{
    uint64_t total = 0;
    for (uint32_t i = 0; i < inode->extent_count; i++)
        total += inode->extents[i].count;
    return total;
}

int alefs_extent_read(struct alefs_dev *dev, const struct alefs_inode *inode,
                      uint64_t offset, void *buf, uint64_t size)
{
    uint64_t pos = 0;
    uint8_t *dst = buf;

    while (size > 0) {
        uint64_t block_offset = offset / ALEFS_BLOCK_SIZE;
        uint64_t byte_offset = offset % ALEFS_BLOCK_SIZE;
        uint64_t ext_idx, ext_block_off;

        if (find_extent(inode, block_offset, &ext_idx, &ext_block_off) != 0)
            break;

        uint64_t dev_block = inode->extents[ext_idx].start + ext_block_off;
        uint64_t to_copy = ALEFS_BLOCK_SIZE - byte_offset;
        if (to_copy > size) to_copy = size;

        uint8_t block_buf[ALEFS_BLOCK_SIZE];
        int ret = alefs_dev_read(dev, dev_block, block_buf);
        if (ret) return ret;

        memcpy(dst, block_buf + byte_offset, to_copy);
        dst += to_copy;
        offset += to_copy;
        pos += to_copy;
        size -= to_copy;
    }

    return (int)pos;
}

int alefs_extent_write(struct alefs_dev *dev, struct alefs_inode *inode,
                       uint64_t offset, const void *buf, uint64_t size)
{
    const uint8_t *src = buf;
    uint64_t remaining = size;

    while (remaining > 0) {
        uint64_t block_offset = offset / ALEFS_BLOCK_SIZE;
        uint64_t byte_offset = offset % ALEFS_BLOCK_SIZE;

        uint64_t ext_idx, ext_block_off;
        if (find_extent(inode, block_offset, &ext_idx, &ext_block_off) != 0)
            return -ENOSPC;

        uint64_t dev_block = inode->extents[ext_idx].start + ext_block_off;
        uint64_t to_copy = ALEFS_BLOCK_SIZE - byte_offset;
        if (to_copy > remaining) to_copy = remaining;

        uint8_t block_buf[ALEFS_BLOCK_SIZE];
        int ret;

        if (byte_offset > 0 || to_copy < ALEFS_BLOCK_SIZE) {
            ret = alefs_dev_read(dev, dev_block, block_buf);
            if (ret) return ret;
        }

        memcpy(block_buf + byte_offset, src, to_copy);
        ret = alefs_dev_write(dev, dev_block, block_buf);
        if (ret) return ret;

        src += to_copy;
        offset += to_copy;
        remaining -= to_copy;
    }

    if (offset > inode->size)
        inode->size = offset;

    return 0;
}

int alefs_extent_append(struct alefs_dev *dev, struct alefs_inode *inode,
                        const void *buf, uint64_t size, uint64_t *bytes_written,
                        uint64_t ino)
{
    uint64_t offset = inode->size;
    uint64_t orig_size = inode->size;
    const uint8_t *src = buf;
    uint64_t remaining = size;
    uint64_t block_offset = offset / ALEFS_BLOCK_SIZE;

    while (remaining > 0) {
        uint64_t ext_idx, ext_block_off;
        if (find_extent(inode, block_offset, &ext_idx, &ext_block_off) != 0) {
            struct alefs_extent ext;
            int ret = alefs_extent_alloc(dev, 1, &ext);
            if (ret) {
                *bytes_written = offset - orig_size;
                return ret;
            }

            if (inode->extent_count < ALEFS_NR_EXTENTS) {
                inode->extents[inode->extent_count] = ext;
                inode->extent_count++;
            } else {
                return -ENOSPC;
            }
            continue;
        }

        uint64_t dev_block = inode->extents[ext_idx].start + ext_block_off;
        uint64_t byte_in_block = offset % ALEFS_BLOCK_SIZE;
        uint64_t to_copy = ALEFS_BLOCK_SIZE - byte_in_block;
        if (to_copy > remaining) to_copy = remaining;

        uint8_t block_buf[ALEFS_BLOCK_SIZE];
        int ret;

        if (byte_in_block > 0) {
            ret = alefs_dev_read(dev, dev_block, block_buf);
            if (ret) { *bytes_written = offset - orig_size; return ret; }
        }

        memcpy(block_buf + byte_in_block, src, to_copy);
        ret = alefs_dev_write(dev, dev_block, block_buf);
        if (ret) { *bytes_written = offset - orig_size; return ret; }

        src += to_copy;
        offset += to_copy;
        remaining -= to_copy;
        block_offset = offset / ALEFS_BLOCK_SIZE;
    }

    inode->size = offset;
    inode->blocks = total_extent_blocks(inode) * 8;
    inode->mtime = time(NULL);
    *bytes_written = size;
    return alefs_inode_write(dev, ino, inode);
}
