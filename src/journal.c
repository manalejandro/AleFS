#include "alefs.h"
#include <string.h>
#include <errno.h>

int alefs_journal_begin(struct alefs_dev *dev)
{
    (void)dev;
    return 0;
}

int alefs_journal_commit(struct alefs_dev *dev)
{
    (void)dev;
    return 0;
}

int alefs_journal_recover(struct alefs_dev *dev)
{
    (void)dev;
    return 0;
}
