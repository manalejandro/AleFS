#include "alefs.h"
#include <stdio.h>

int alefs_version_create(struct alefs_dev *dev, uint64_t ino)
{
    (void)dev; (void)ino;
    fprintf(stderr, "alefs: versioning not compiled in\n");
    return -1;
}

int alefs_version_list(struct alefs_dev *dev, uint64_t ino)
{
    (void)dev; (void)ino;
    printf("  (versioning not enabled)\n");
    return 0;
}
