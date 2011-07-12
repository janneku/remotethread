#ifndef _UTILS_H
#define _UTILS_H

#include <string.h>
#include <stdio.h>

#define APP_NAME	"remotethread"

#define warning(...)	fprintf(stderr, APP_NAME " WARNING: " __VA_ARGS__)

int read_all(int fd, void *buf, size_t len);
int write_all(int fd, const void *buf, size_t len);

#endif
