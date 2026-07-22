#include "alefs.h"
#include <stdio.h>

int alefs_compress(const void *src, size_t src_len,
                   void *dst, size_t *dst_len, int level)
{
    (void)src; (void)src_len; (void)dst; (void)dst_len; (void)level;
    fprintf(stderr, "alefs: compression not compiled in\n");
    return -1;
}

int alefs_decompress(const void *src, size_t src_len,
                     void *dst, size_t *dst_len)
{
    (void)src; (void)src_len; (void)dst; (void)dst_len;
    fprintf(stderr, "alefs: compression not compiled in\n");
    return -1;
}
