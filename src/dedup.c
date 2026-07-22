#include "alefs.h"
#include <stdio.h>
#include <errno.h>

int alefs_dedup_init(struct alefs_dev *dev)
{
    (void)dev;
    return 0;
}

int alefs_dedup_find(struct alefs_dev *dev, const uint8_t *hash,
                     uint64_t *block)
{
    (void)dev; (void)hash; (void)block;
    return -ENOENT;
}

int alefs_dedup_store(struct alefs_dev *dev, const uint8_t *hash,
                      uint64_t block)
{
    (void)dev; (void)hash; (void)block;
    return 0;
}
