#include "alefs.h"
#include <stdio.h>

int alefs_tier_init(struct alefs_dev *dev)
{
    (void)dev;
    return 0;
}

int alefs_tier_migrate(struct alefs_dev *dev, uint64_t block, int tier)
{
    (void)dev; (void)block; (void)tier;
    return 0;
}
