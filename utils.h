
#pragma once

#include <stdlib.h>
#include <stdint.h>

__attribute__((noreturn))
void fail(const char *format, ...);

void log_info(const char *format, ...);
void log_error(const char *format, ...);

void *xmalloc(size_t size);

uint64_t time_monotonic();


#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define max(a,b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a > _b ? _a : _b; })

#define min(a,b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a < _b ? _a : _b; })
