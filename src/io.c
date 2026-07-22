#include "alefs.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int alefs_dev_open(struct alefs_dev *dev, const char *path, int flags)
{
    memset(dev, 0, sizeof(*dev));
    dev->fd = open(path, flags);
    if (dev->fd < 0)
        return -errno;
    dev->path = strdup(path);
    dev->block_size = ALEFS_BLOCK_SIZE;
    return 0;
}

int alefs_dev_close(struct alefs_dev *dev)
{
    if (dev->dirty)
        alefs_super_sync(dev);
    if (dev->fd >= 0)
        close(dev->fd);
    free(dev->path);
    memset(dev, 0, sizeof(*dev));
    return 0;
}

int alefs_dev_read(struct alefs_dev *dev, uint64_t block, void *buf)
{
    off_t offset = (off_t)block * (off_t)dev->block_size;
    ssize_t n = pread(dev->fd, buf, dev->block_size, offset);
    if (n != (ssize_t)dev->block_size)
        return -EIO;
    return 0;
}

int alefs_dev_write(struct alefs_dev *dev, uint64_t block, const void *buf)
{
    off_t offset = (off_t)block * (off_t)dev->block_size;
    ssize_t n = pwrite(dev->fd, buf, dev->block_size, offset);
    if (n != (ssize_t)dev->block_size)
        return -EIO;
    dev->dirty = true;
    return 0;
}

int alefs_dev_sync(struct alefs_dev *dev)
{
    if (dev->dirty) {
        int ret = alefs_super_sync(dev);
        if (ret) return ret;
        dev->dirty = false;
    }
    return fsync(dev->fd);
}

int alefs_dev_create(const char *path, uint64_t size_mb)
{
    uint64_t size = size_mb * 1024ULL * 1024ULL;
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -errno;
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -errno;
    }
    close(fd);
    return 0;
}
