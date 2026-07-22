#include "alefs.h"
#include <stdio.h>

int alefs_gc_run(struct alefs_dev *dev)
{
    (void)dev;
    return 0;
}

int alefs_gc_trim(struct alefs_dev *dev, uint64_t block)
{
    (void)dev; (void)block;
    return 0;
}
