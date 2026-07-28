#include "aleqfs.h"
#include <errno.h>

#define JNL_MAGIC       0x4A4F55524E414CULL
#define ENT_MAGIC       0x454E5452
#define JT_ALLOC        1
#define ENTRIES_PER_BLK 126

struct jnl_entry {
    uint32_t magic;
    uint32_t type;
    uint64_t ino;
    uint64_t block;
    uint64_t count;
};

struct jnl_block {
    uint64_t magic;
    uint64_t reserved;
    struct jnl_entry entries[ENTRIES_PER_BLK];
    uint8_t pad[48];
};

static bool inode_has_block(const struct aleqfs_inode *inode, uint64_t block)
{
    for (uint32_t i = 0; i < inode->extent_count; i++)
        if (block >= inode->extents[i].start &&
            block < inode->extents[i].start + inode->extents[i].count)
            return true;
    return false;
}

static int find_free_slot(struct jnl_block *jb)
{
    for (int i = 0; i < ENTRIES_PER_BLK; i++)
        if (jb->entries[i].magic != ENT_MAGIC || jb->entries[i].ino == 0)
            return i;
    return -1;
}

int aleqfs_journal_recover(struct aleqfs_dev *dev)
{
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    struct jnl_block *jb = (struct jnl_block *)buf;
    int recovered = 0;

    for (uint64_t b = 0; b < ALEQFS_JOURNAL_BLOCKS; b++) {
        int ret = aleqfs_dev_read(dev, dev->sb.journal_start + b, buf);
        if (ret < 0 || jb->magic != JNL_MAGIC)
            continue;

        bool dirty = false;

        for (int i = 0; i < ENTRIES_PER_BLK; i++) {
            struct jnl_entry *e = &jb->entries[i];
            if (e->magic != ENT_MAGIC || e->ino == 0)
                continue;

            int needs_free = 0;
            struct aleqfs_inode inode;
            ret = aleqfs_inode_read(dev, e->ino, &inode);

            if (ret == 0 && inode.mode != 0) {
                for (uint64_t k = 0; k < e->count; k++)
                    if (!inode_has_block(&inode, e->block + k))
                        needs_free++;
            } else {
                needs_free = (int)e->count;
            }

            if (needs_free > 0) {
                for (uint64_t k = 0; k < e->count; k++) {
                    if (ret == 0 && inode_has_block(&inode, e->block + k))
                        continue;
                    aleqfs_bitmap_free(dev, e->block + k);
                }
                recovered += needs_free;
            }

            e->magic = 0;
            e->ino = 0;
            dirty = true;
        }

        if (dirty) {
            aleqfs_dev_write(dev, dev->sb.journal_start + b, buf);
            aleqfs_dev_flush(dev);
        }
    }

    if (recovered > 0) {
        fprintf(stderr, "aleqfs: journal recovery freed %d leaked block(s)\n",
                recovered);
        aleqfs_super_sync(dev);
        aleqfs_dev_flush(dev);
    }

    return recovered;
}

int aleqfs_journal_record(struct aleqfs_dev *dev, uint64_t ino,
                           uint64_t block, uint64_t count)
{
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    struct jnl_block *jb = (struct jnl_block *)buf;

    for (uint64_t b = 0; b < ALEQFS_JOURNAL_BLOCKS; b++) {
        uint64_t blk = dev->sb.journal_start + b;
        int ret = aleqfs_dev_read(dev, blk, buf);
        if (ret < 0)
            continue;

        if (jb->magic != JNL_MAGIC) {
            memset(jb, 0, sizeof(*jb));
            jb->magic = JNL_MAGIC;
            jb->entries[0].magic = ENT_MAGIC;
            jb->entries[0].type = JT_ALLOC;
            jb->entries[0].ino = ino;
            jb->entries[0].block = block;
            jb->entries[0].count = count;
            ret = aleqfs_dev_write(dev, blk, buf);
            if (ret == 0) aleqfs_dev_flush(dev);
            return ret;
        }

        int slot = find_free_slot(jb);
        if (slot < 0)
            continue;

        jb->entries[slot].magic = ENT_MAGIC;
        jb->entries[slot].type = JT_ALLOC;
        jb->entries[slot].ino = ino;
        jb->entries[slot].block = block;
        jb->entries[slot].count = count;
        ret = aleqfs_dev_write(dev, blk, buf);
        if (ret == 0) aleqfs_dev_flush(dev);
        return ret;
    }

    fprintf(stderr, "aleqfs: journal full!\n");
    return -ENOSPC;
}

int aleqfs_journal_clear(struct aleqfs_dev *dev, uint64_t ino,
                          uint64_t block, uint64_t count)
{
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    struct jnl_block *jb = (struct jnl_block *)buf;

    for (uint64_t b = 0; b < ALEQFS_JOURNAL_BLOCKS; b++) {
        uint64_t blk = dev->sb.journal_start + b;
        int ret = aleqfs_dev_read(dev, blk, buf);
        if (ret < 0 || jb->magic != JNL_MAGIC)
            continue;

        for (int i = 0; i < ENTRIES_PER_BLK; i++) {
            struct jnl_entry *e = &jb->entries[i];
            if (e->magic == ENT_MAGIC && e->ino == ino &&
                e->block == block && e->count == count) {
                e->magic = 0;
                e->ino = 0;
                ret = aleqfs_dev_write(dev, blk, buf);
                if (ret == 0)
                    aleqfs_dev_flush(dev);
                return ret;
            }
        }
    }

    return 0;
}
