
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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
	if (format[strlen(format) - 1] != '\n') {
	// This is for libvncserver
	fputc('\n', stderr);
	}
	fputs("\x1b[0m", stderr);
}


void log_info(const char *format, ...)
{
	va_list vas;
	va_start(vas, format);
	log_base("\x1b[1m\x1b[94m[I]", format, vas);
	va_end(vas);
}


void log_error(const char *format, ...)
{
	va_list vas;
	va_start(vas, format);
	log_base("\x1b[1m\x1b[91m[E]", format, vas);
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


int shm_create()
{
	const char *filename_format = "/wvnc-%d";
	char filename[sizeof(filename_format) + 10];
	int fd = -1;
	for (int i = 0; i < 10000; i++) {
		snprintf(filename, sizeof(filename), filename_format, i);
		fd = shm_open(filename, O_RDWR | O_EXCL | O_CREAT | O_TRUNC, 0660);
		if (fd >= 0) {
			// Just the fd matters now
			shm_unlink(filename);
			break;
		}
	}
	if (fd < 0) {
		fail("Failed to open SHM file");
	}
	return fd;
}
