#include "dlq89_priv.h"
#include <stdlib.h>

void *dlq89_priv_malloc(size_t n)
{
    if (n == 0)
    {
        n = 1;
    }
    return malloc(n);
}

void *dlq89_priv_realloc(void *p, size_t n)
{
    if (n == 0)
    {
        n = 1;
    }
    return realloc(p, n);
}

void dlq89_priv_free(void *p)
{
    free(p);
}
