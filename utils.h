
#pragma once

#include <stdint.h>
#include <stdlib.h>

__attribute__((noreturn))
void fail(const char *format, ...);

void log_info(const char *format, ...);
void log_error(const char *format, ...);

void *xmalloc(size_t size);

uint64_t time_monotonic();

int shm_create();

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define max(a,b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a > _b ? _a : _b; })

#define min(a,b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a < _b ? _a : _b; })

#define clamp(x, a, b) min(max(x, a), b)


#define BIT(n) (1 << (n))
