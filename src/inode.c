#include "aleqfs.h"
#include <string.h>
#include <errno.h>

int aleqfs_inode_alloc(struct aleqfs_dev *dev, uint64_t *ino)
{
    uint64_t max_ino = dev->sb.inode_count;
    if (max_ino == 0) {
        max_ino = ALEQFS_INODE_BLOCKS * ALEQFS_INODES_PER_BLK;
    }

    for (uint64_t i = ALEQFS_ROOT_INO; i <= max_ino; i++) {
        struct aleqfs_inode inode;
        int ret = aleqfs_inode_read(dev, i, &inode);
        if (ret && ret != -EIO) return ret;
        if (inode.mode == 0) {
            *ino = i;

            struct aleqfs_inode blank;
            memset(&blank, 0, sizeof(blank));
            blank.amplitude.real = 32767;
            blank.amplitude.imag = 0;
            blank.atime = blank.mtime = blank.ctime = time(NULL);

            ret = aleqfs_inode_write(dev, i, &blank);
            if (ret) return ret;
            dev->sb.free_inodes--;
            return 0;
        }
    }

    return -ENOSPC;
}

int aleqfs_inode_free(struct aleqfs_dev *dev, uint64_t ino)
{
    struct aleqfs_inode blank;
    memset(&blank, 0, sizeof(blank));
    int ret = aleqfs_inode_write(dev, ino, &blank);
    if (ret) return ret;
    dev->sb.free_inodes++;
    return 0;
}

int aleqfs_inode_read(struct aleqfs_dev *dev, uint64_t ino, struct aleqfs_inode *inode)
{
    if (ino == 0 || ino > dev->sb.inode_count)
        return -ENOENT;
    uint64_t inodes_per_block = ALEQFS_INODES_PER_BLK;
    uint64_t block = dev->sb.inode_table_start + (ino - 1) / inodes_per_block;
    uint64_t offset = ((ino - 1) % inodes_per_block) * sizeof(struct aleqfs_inode);
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    int ret = aleqfs_dev_read(dev, block, buf);
    if (ret) return ret;
    memcpy(inode, buf + offset, sizeof(struct aleqfs_inode));
    return 0;
}

int aleqfs_inode_write(struct aleqfs_dev *dev, uint64_t ino, const struct aleqfs_inode *inode)
{
    if (ino == 0 || ino > dev->sb.inode_count)
        return -ENOENT;
    uint64_t inodes_per_block = ALEQFS_INODES_PER_BLK;
    uint64_t block = dev->sb.inode_table_start + (ino - 1) / inodes_per_block;
    uint64_t offset = ((ino - 1) % inodes_per_block) * sizeof(struct aleqfs_inode);
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    int ret = aleqfs_dev_read(dev, block, buf);
    if (ret) return ret;
    memcpy(buf + offset, inode, sizeof(struct aleqfs_inode));
    return aleqfs_dev_write(dev, block, buf);
}
