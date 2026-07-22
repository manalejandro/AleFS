#include "alefs.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

int alefs_super_format(struct alefs_dev *dev, uint64_t total_blocks)
{
    struct alefs_superblock *sb = &dev->sb;

    memset(sb, 0, sizeof(*sb));
    sb->magic           = ALEFS_MAGIC;
    sb->version         = ALEFS_FS_VERSION;
    sb->block_size      = ALEFS_BLOCK_SIZE;
    sb->total_blocks    = total_blocks;
    sb->inode_count     = ALEFS_INODE_BLOCKS * ALEFS_INODES_PER_BLK;
    sb->free_inodes     = sb->inode_count;
    sb->root_inode      = ALEFS_ROOT_INO;
    sb->journal_start   = 1;
    sb->journal_blocks  = ALEFS_JOURNAL_BLOCKS;
    sb->inode_table_start = sb->journal_start + sb->journal_blocks;
    sb->btree_root      = 0;
    sb->bitmap_start    = sb->inode_table_start + ALEFS_INODE_BLOCKS;
    sb->data_start      = sb->bitmap_start + ALEFS_BITMAP_BLOCKS;

    dev->num_blocks = total_blocks;
    sb->free_blocks = total_blocks - sb->data_start;

    int ret = alefs_dev_write(dev, 0, sb);
    if (ret) return ret;

    uint8_t zero_buf[ALEFS_BLOCK_SIZE] = {0};
    for (uint64_t b = sb->journal_start; b < sb->bitmap_start; b++) {
        ret = alefs_dev_write(dev, b, zero_buf);
        if (ret) return ret;
    }

    ret = alefs_bitmap_init(dev);
    if (ret) return ret;

    {
        uint64_t btree_block = 0;
        ret = alefs_btree_init(dev, &btree_block);
        if (ret) return ret;
        sb->btree_root = btree_block;
    }

    uint64_t root_ino;
    ret = alefs_inode_alloc(dev, &root_ino);
    if (ret) return ret;

    struct alefs_inode root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.mode   = S_IFDIR | 0755;
    root_inode.uid    = getuid();
    root_inode.gid    = getgid();
    root_inode.links  = 2;
    root_inode.atime  = root_inode.mtime = root_inode.ctime = time(NULL);
    ret = alefs_inode_write(dev, root_ino, &root_inode);
    if (ret) return ret;

    sb->root_inode = root_ino;

    dev->dirty = true;
    return alefs_super_sync(dev);
}

int alefs_super_load(struct alefs_dev *dev)
{
    int ret = alefs_dev_read(dev, 0, &dev->sb);
    if (ret) return ret;
    if (dev->sb.magic != ALEFS_MAGIC)
        return -EINVAL;
    dev->num_blocks = dev->sb.total_blocks;
    dev->block_size = dev->sb.block_size;
    return 0;
}

int alefs_super_sync(struct alefs_dev *dev)
{
    return alefs_dev_write(dev, 0, &dev->sb);
}
