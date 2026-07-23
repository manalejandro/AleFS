#include "aleqfs.h"
#include <string.h>
#include <errno.h>

int aleqfs_super_format(struct aleqfs_dev *dev, uint64_t total_blocks)
{
    struct aleqfs_superblock *sb = &dev->sb;
    memset(sb, 0, sizeof(*sb));

    sb->magic           = ALEQFS_MAGIC;
    sb->version         = ALEQFS_FS_VERSION;
    sb->block_size      = ALEQFS_BLOCK_SIZE;
    sb->total_blocks    = total_blocks;
    sb->inode_count     = ALEQFS_INODE_BLOCKS * ALEQFS_INODES_PER_BLK;
    sb->free_inodes     = sb->inode_count;
    sb->root_inode      = ALEQFS_ROOT_INO;
    sb->journal_start   = 1;
    sb->journal_blocks  = ALEQFS_JOURNAL_BLOCKS;
    sb->inode_table_start = sb->journal_start + sb->journal_blocks;
    sb->bitmap_start    = sb->inode_table_start + ALEQFS_INODE_BLOCKS;
    sb->data_start      = sb->bitmap_start + ALEQFS_BITMAP_BLOCKS;
    sb->grover_root     = 0;
    sb->quantum_temperature = 0.01;
    sb->decoherence_rate    = 0.001;

    dev->num_blocks = total_blocks;
    dev->block_size = ALEQFS_BLOCK_SIZE;
    dev->dirty      = false;
    sb->free_blocks = total_blocks - sb->data_start;

    int ret = aleqfs_dev_write(dev, 0, sb);
    if (ret) return ret;

    uint8_t zero_buf[ALEQFS_BLOCK_SIZE] = {0};
    for (uint64_t b = sb->journal_start; b < sb->data_start; b++) {
        ret = aleqfs_dev_write(dev, b, zero_buf);
        if (ret) return ret;
    }

    ret = aleqfs_bitmap_init(dev);
    if (ret) return ret;

    {
        uint64_t grover_block = 0;
        ret = aleqfs_grover_init(dev, &grover_block);
        if (ret) return ret;
        sb->grover_root = grover_block;
    }

    sb->entanglement_start = 0;
    sb->entanglement_blocks = 16;
    for (uint64_t i = 0; i < sb->entanglement_blocks; i++) {
        uint64_t blk;
        ret = aleqfs_bitmap_alloc(dev, &blk);
        if (ret) return ret;
        if (i == 0) sb->entanglement_start = blk;
        ret = aleqfs_dev_write(dev, blk, zero_buf);
        if (ret) return ret;
    }

    uint64_t root_ino;
    ret = aleqfs_inode_alloc(dev, &root_ino);
    if (ret) return ret;

    struct aleqfs_inode root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.mode   = S_IFDIR | 0755;
    root_inode.uid    = getuid();
    root_inode.gid    = getgid();
    root_inode.links  = 2;
    root_inode.atime  = root_inode.mtime = root_inode.ctime = time(NULL);
    root_inode.amplitude.real = 32767;
    root_inode.amplitude.imag = 0;
    ret = aleqfs_inode_write(dev, root_ino, &root_inode);
    if (ret) return ret;

    sb->root_inode = root_ino;
    dev->dirty = true;

    return aleqfs_super_sync(dev);
}

int aleqfs_super_load(struct aleqfs_dev *dev)
{
    int ret = aleqfs_dev_read(dev, 0, &dev->sb);
    if (ret) return ret;
    if (dev->sb.magic != ALEQFS_MAGIC)
        return -EINVAL;
    dev->num_blocks = dev->sb.total_blocks;
    dev->block_size = dev->sb.block_size;
    return 0;
}

int aleqfs_super_sync(struct aleqfs_dev *dev)
{
    int ret = aleqfs_dev_write(dev, 0, &dev->sb);
    if (ret) return ret;
    dev->dirty = false;
    return 0;
}
