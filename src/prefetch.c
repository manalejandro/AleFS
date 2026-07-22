#include "alefs.h"
#include <stdio.h>

void alefs_prefetch_init(void)
{
}

void alefs_prefetch_record(uint64_t block)
{
    (void)block;
}

uint64_t alefs_prefetch_predict(void)
{
    return 0;
}
