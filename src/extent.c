#include "aleqfs.h"
#include <string.h>
#include <errno.h>

static int find_extent(const struct aleqfs_inode *inode, uint64_t file_block,
                       uint64_t *phys_block)
{
    uint64_t accum = 0;
    for (uint32_t i = 0; i < inode->extent_count; i++) {
        if (file_block < accum + inode->extents[i].count) {
            *phys_block = inode->extents[i].start + (file_block - accum);
            return (int)i;
        }
        accum += inode->extents[i].count;
    }
    return -1;
}

static uint64_t total_extent_blocks(const struct aleqfs_inode *inode)
{
    uint64_t total = 0;
    for (uint32_t i = 0; i < inode->extent_count; i++)
        total += inode->extents[i].count;
    return total;
}

int aleqfs_extent_alloc(struct aleqfs_dev *dev, uint64_t count,
                        struct aleqfs_extent *ext)
{
    for (uint64_t blk = dev->sb.data_start; blk <= dev->sb.total_blocks - count;) {
        bool found = true;
        for (uint64_t i = 0; i < count; i++) {
            if (aleqfs_bitmap_get(dev, blk + i)) {
                found = false;
                blk += i + 1;
                break;
            }
        }
        if (found) {
            ext->start = blk;
            ext->count = count;
            for (uint64_t i = 0; i < count; i++)
                aleqfs_bitmark_set(dev, blk + i);
            dev->sb.free_blocks -= count;
            dev->dirty = true;
            return 0;
        }
    }
    return -ENOSPC;
}

int aleqfs_extent_free(struct aleqfs_dev *dev, const struct aleqfs_extent *ext)
{
    for (uint64_t i = 0; i < ext->count; i++) {
        int ret = aleqfs_bitmap_free(dev, ext->start + i);
        if (ret < 0)
            return ret;
    }
    return 0;
}

int aleqfs_extent_read(struct aleqfs_dev *dev, const struct aleqfs_inode *inode,
                       uint64_t offset, void *buf, uint64_t size)
{
    if (offset >= inode->size)
        return 0;
    if (offset + size > inode->size)
        size = inode->size - offset;

    uint8_t *ptr = (uint8_t *)buf;
    uint64_t remaining = size;

    while (remaining > 0) {
        uint64_t file_block = offset / ALEQFS_BLOCK_SIZE;
        uint64_t block_off = offset % ALEQFS_BLOCK_SIZE;

        uint64_t phys_block;
        if (find_extent(inode, file_block, &phys_block) < 0)
            return -EIO;

        uint64_t to_read = ALEQFS_BLOCK_SIZE - block_off;
        if (to_read > remaining)
            to_read = remaining;

        uint8_t tmp[ALEQFS_BLOCK_SIZE];
        int ret = aleqfs_dev_read(dev, phys_block, tmp);
        if (ret < 0)
            return ret;

        memcpy(ptr, tmp + block_off, to_read);

        ptr += to_read;
        offset += to_read;
        remaining -= to_read;
    }

    return 0;
}

int aleqfs_extent_write(struct aleqfs_dev *dev, struct aleqfs_inode *inode,
                        uint64_t offset, const void *buf, uint64_t size)
{
    const uint8_t *ptr = (const uint8_t *)buf;
    uint64_t remaining = size;

    while (remaining > 0) {
        uint64_t file_block = offset / ALEQFS_BLOCK_SIZE;
        uint64_t block_off = offset % ALEQFS_BLOCK_SIZE;

        uint64_t phys_block;
        int idx = find_extent(inode, file_block, &phys_block);

        if (idx < 0) {
            bool allocated = false;

            if (inode->extent_count > 0) {
                struct aleqfs_extent *last =
                    &inode->extents[inode->extent_count - 1];
                uint64_t last_start = 0;
                for (uint32_t i = 0; i < inode->extent_count - 1; i++)
                    last_start += inode->extents[i].count;

                if (file_block == last_start + last->count) {
                    uint64_t next = last->start + last->count;
                    if (!aleqfs_bitmap_get(dev, next)) {
                        aleqfs_bitmark_set(dev, next);
                        last->count++;
                        inode->blocks++;
                        dev->sb.free_blocks--;
                        dev->dirty = true;
                        phys_block = next;
                        allocated = true;
                    }
                }
            }

            if (!allocated) {
                if (inode->extent_count >= ALEQFS_NR_EXTENTS)
                    return -ENOSPC;

                struct aleqfs_extent new_ext;
                int ret = aleqfs_extent_alloc(dev, 1, &new_ext);
                if (ret < 0)
                    return ret;

                inode->extents[inode->extent_count] = new_ext;
                inode->extent_count++;
                inode->blocks += new_ext.count;
                phys_block = new_ext.start;
            }
        }

        uint64_t to_write = ALEQFS_BLOCK_SIZE - block_off;
        if (to_write > remaining)
            to_write = remaining;

        if (block_off == 0 && to_write == ALEQFS_BLOCK_SIZE) {
            int ret = aleqfs_dev_write(dev, phys_block, ptr);
            if (ret < 0)
                return ret;
        } else {
            uint8_t tmp[ALEQFS_BLOCK_SIZE];
            int ret = aleqfs_dev_read(dev, phys_block, tmp);
            if (ret < 0)
                return ret;
            memcpy(tmp + block_off, ptr, to_write);
            ret = aleqfs_dev_write(dev, phys_block, tmp);
            if (ret < 0)
                return ret;
        }

        ptr += to_write;
        offset += to_write;
        remaining -= to_write;
    }

    if (offset > inode->size)
        inode->size = offset;

    return 0;
}

int aleqfs_extent_append(struct aleqfs_dev *dev, struct aleqfs_inode *inode,
                         const void *buf, uint64_t size, uint64_t *bytes_written,
                         uint64_t ino)
{
    (void)ino;

    uint64_t offset = inode->size;
    const uint8_t *ptr = (const uint8_t *)buf;
    uint64_t remaining = size;
    uint64_t written = 0;

    while (remaining > 0) {
        uint64_t file_block = offset / ALEQFS_BLOCK_SIZE;
        uint64_t block_off = offset % ALEQFS_BLOCK_SIZE;

        uint64_t phys_block;
        int idx = find_extent(inode, file_block, &phys_block);

        if (idx < 0) {
            bool allocated = false;

            if (inode->extent_count > 0) {
                struct aleqfs_extent *last =
                    &inode->extents[inode->extent_count - 1];
                uint64_t last_start = 0;
                for (uint32_t i = 0; i < inode->extent_count - 1; i++)
                    last_start += inode->extents[i].count;

                if (file_block == last_start + last->count) {
                    uint64_t next = last->start + last->count;
                    if (!aleqfs_bitmap_get(dev, next)) {
                        aleqfs_bitmark_set(dev, next);
                        last->count++;
                        inode->blocks++;
                        dev->sb.free_blocks--;
                        dev->dirty = true;
                        phys_block = next;
                        allocated = true;
                    }
                }
            }

            if (!allocated) {
                if (inode->extent_count >= ALEQFS_NR_EXTENTS)
                    break;

                struct aleqfs_extent new_ext;
                int ret = aleqfs_extent_alloc(dev, 1, &new_ext);
                if (ret < 0)
                    break;

                inode->extents[inode->extent_count] = new_ext;
                inode->extent_count++;
                inode->blocks += new_ext.count;
                phys_block = new_ext.start;
            }
        }

        uint64_t to_write = ALEQFS_BLOCK_SIZE - block_off;
        if (to_write > remaining)
            to_write = remaining;

        if (block_off == 0 && to_write == ALEQFS_BLOCK_SIZE) {
            if (aleqfs_dev_write(dev, phys_block, ptr) < 0)
                break;
        } else {
            uint8_t tmp[ALEQFS_BLOCK_SIZE];
            if (aleqfs_dev_read(dev, phys_block, tmp) < 0)
                break;
            memcpy(tmp + block_off, ptr, to_write);
            if (aleqfs_dev_write(dev, phys_block, tmp) < 0)
                break;
        }

        ptr += to_write;
        offset += to_write;
        written += to_write;
        remaining -= to_write;
    }

    inode->size += written;
    inode->blocks = total_extent_blocks(inode);
    inode->mtime = time(NULL);
    *bytes_written = written;

    if (written > 0)
        return aleqfs_inode_write(dev, ino, inode);
    return 0;
}
