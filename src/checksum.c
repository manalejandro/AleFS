#include "aleqfs.h"

uint64_t aleqfs_checksum(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

int aleqfs_decoherence_check(struct aleqfs_dev *dev, uint64_t block,
                             uint64_t expected_checksum)
{
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    int ret = aleqfs_dev_read(dev, block, buf);
    if (ret) return ret;

    uint64_t actual = aleqfs_checksum(buf, sizeof(buf));
    return (actual == expected_checksum) ? 0 : -1;
}
