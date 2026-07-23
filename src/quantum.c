#include "aleqfs.h"
#include <math.h>
#include <errno.h>

double aleqfs_quantum_probability(int16_t real, int16_t imag)
{
    double r = real, i = imag;
    return (r * r + i * i) / (32767.0 * 32767.0);
}

int aleqfs_quantum_collapse(int16_t *real, int16_t *imag)
{
    double prob = aleqfs_quantum_probability(*real, *imag);
    double roll = (double)rand() / (double)RAND_MAX;

    if (prob > roll)
        return 1;

    *real = 0;
    *imag = 0;
    return 0;
}

int aleqfs_quantum_entangle_state(struct aleqfs_dev *dev, uint64_t ino,
                                  struct aleqfs_inode *inode)
{
    uint64_t partner_ino = inode->entanglement_partner;
    if (partner_ino == 0)
        return -EINVAL;

    struct aleqfs_inode partner;
    int ret = aleqfs_inode_read(dev, partner_ino, &partner);
    if (ret) return ret;

    inode->amplitude.real = (inode->amplitude.real + partner.amplitude.real) / 2;
    inode->amplitude.imag = (inode->amplitude.imag + partner.amplitude.imag) / 2;

    partner.amplitude.real = inode->amplitude.real;
    partner.amplitude.imag = inode->amplitude.imag;

    ret = aleqfs_inode_write(dev, partner_ino, &partner);
    if (ret) return ret;

    return aleqfs_inode_write(dev, ino, inode);
}

int aleqfs_quantum_temperature(struct aleqfs_dev *dev)
{
    return (int)dev->sb.quantum_temperature;
}
