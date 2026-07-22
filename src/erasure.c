#include "alefs.h"
#include <stdio.h>

int alefs_erasure_encode(int k, int m, const uint8_t **data,
                         uint8_t **parity, size_t len)
{
    (void)k; (void)m; (void)data; (void)parity; (void)len;
    fprintf(stderr, "alefs: erasure coding not compiled in\n");
    return -1;
}

int alefs_erasure_decode(int k, int m, uint8_t **data,
                         uint8_t **parity, size_t len, int *erasures)
{
    (void)k; (void)m; (void)data; (void)parity; (void)len; (void)erasures;
    fprintf(stderr, "alefs: erasure coding not compiled in\n");
    return -1;
}
