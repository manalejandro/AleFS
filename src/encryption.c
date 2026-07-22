#include "alefs.h"
#include <stdio.h>

int alefs_encrypt(const uint8_t *key, const uint8_t *iv,
                  const uint8_t *plain, uint8_t *cipher, size_t len)
{
    (void)key; (void)iv; (void)plain; (void)cipher; (void)len;
    fprintf(stderr, "alefs: encryption not compiled in\n");
    return -1;
}

int alefs_decrypt(const uint8_t *key, const uint8_t *iv,
                  const uint8_t *cipher, uint8_t *plain, size_t len)
{
    (void)key; (void)iv; (void)cipher; (void)plain; (void)len;
    fprintf(stderr, "alefs: encryption not compiled in\n");
    return -1;
}
