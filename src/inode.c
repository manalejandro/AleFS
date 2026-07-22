#include "alefs.h"
#include <string.h>
#include <errno.h>

int alefs_inode_alloc(struct alefs_dev *dev, uint64_t *ino)
{
    struct alefs_inode blank;
    memset(&blank, 0, sizeof(blank));

    for (uint64_t i = ALEFS_ROOT_INO; i <= dev->sb.inode_count; i++) {
        struct alefs_inode inode;
        int ret = alefs_inode_read(dev, i, &inode);
        if (ret && ret != -EIO) return ret;
        if (inode.mode == 0 || (ret == -EIO && i == dev->sb.inode_count)) {
            *ino = i;
            ret = alefs_inode_write(dev, i, &blank);
            if (ret) return ret;
            dev->sb.free_inodes--;
            return 0;
        }
    }

    return -ENOSPC;
}

int alefs_inode_free(struct alefs_dev *dev, uint64_t ino)
{
    struct alefs_inode blank;
    memset(&blank, 0, sizeof(blank));
    int ret = alefs_inode_write(dev, ino, &blank);
    if (ret) return ret;
    dev->sb.free_inodes++;
    return 0;
}

int alefs_inode_read(struct alefs_dev *dev, uint64_t ino, struct alefs_inode *inode)
{
    if (ino == 0 || ino > dev->sb.inode_count)
        return -ENOENT;
    uint64_t inodes_per_block = ALEFS_INODES_PER_BLK;
    uint64_t block = dev->sb.inode_table_start + (ino - 1) / inodes_per_block;
    uint64_t offset = ((ino - 1) % inodes_per_block) * sizeof(struct alefs_inode);
    uint8_t buf[ALEFS_BLOCK_SIZE];
    int ret = alefs_dev_read(dev, block, buf);
    if (ret) return ret;
    memcpy(inode, buf + offset, sizeof(struct alefs_inode));
    return 0;
}

int alefs_inode_write(struct alefs_dev *dev, uint64_t ino, const struct alefs_inode *inode)
{
    if (ino == 0 || ino > dev->sb.inode_count)
        return -ENOENT;
    uint64_t inodes_per_block = ALEFS_INODES_PER_BLK;
    uint64_t block = dev->sb.inode_table_start + (ino - 1) / inodes_per_block;
    uint64_t offset = ((ino - 1) % inodes_per_block) * sizeof(struct alefs_inode);
    uint8_t buf[ALEFS_BLOCK_SIZE];
    int ret = alefs_dev_read(dev, block, buf);
    if (ret) return ret;
    memcpy(buf + offset, inode, sizeof(struct alefs_inode));
    return alefs_dev_write(dev, block, buf);
}
