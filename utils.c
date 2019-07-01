
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utils.h"

void fail(const char *format, ...)
{
    va_list vas;
    va_start(vas, format);
    vfprintf(stderr, format, vas);
    va_end(vas);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}


static void log_base(const char *prefix, const char *format, va_list vas)
{
    fputs(prefix, stderr);
    fputc(' ', stderr);
    vfprintf(stderr, format, vas);
    fputc('\n', stderr);
}


void log_info(const char *format, ...)
{
    va_list vas;
    va_start(vas, format);
    log_base("[I]", format, vas);
    va_end(vas);
}


void log_error(const char *format, ...)
{
    va_list vas;
    va_start(vas, format);
    log_base("[E]", format, vas);
    va_end(vas);
}


void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fail("Memory allocation failed");
    }
    memset(ptr, 0, size);
    return ptr;
}


uint64_t time_monotonic()
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &time);
    return time.tv_sec * 1000000 + time.tv_nsec / 1000;
}
