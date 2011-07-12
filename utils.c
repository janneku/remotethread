#include "utils.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int read_all(int fd, void *buf, size_t len)
{
	size_t pos = 0;
	while (pos < len) {
		ssize_t got = read(fd, (char *) buf + pos, len - pos);
		if (got < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			perror("read");
			return -1;
		} else if (got == 0) {
			fprintf(stderr, "unexpected EOF\n");
			return -1;
		}
		pos += got;
	}
	return 0;
}

int write_all(int fd, const void *buf, size_t len)
{
	size_t pos = 0;
	while (pos < len) {
		ssize_t sent = write(fd, (char *) buf + pos, len - pos);
		if (sent < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			perror("write");
			return -1;
		}
		pos += sent;
	}
	return 0;
}
