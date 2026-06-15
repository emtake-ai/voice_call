#include <sys/time.h>
#include <time.h>

int stime(const time_t *t)
{
    struct timeval tv;

    tv.tv_sec = *t;
    tv.tv_usec = 0;

    return settimeofday(&tv, 0);
}
