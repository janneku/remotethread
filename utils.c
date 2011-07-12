#include "utils.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

size_t bytes_available(int fd)
{
	int avail;
	if (ioctl(fd, FIONREAD, &avail)) {
		warning("ioctl() failed (%s)\n", strerror(errno));
		return 0;
	}
	return avail;
}

size_t read_available(int fd, void *buf, size_t len)
{
	size_t pos = 0;
	while (pos < len) {
		ssize_t got = read(fd, (char *) buf + pos, len - pos);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN)
				break;
			warning("read() failed (%s)\n", strerror(errno));
			return -1;
		} else if (got == 0) {
			warning("unexpected EOF\n");
			return -1;
		}
		pos += got;
	}
	return pos;
}

int read_all(int fd, void *buf, size_t len)
{
	size_t pos = 0;
	while (pos < len) {
		ssize_t got = read(fd, (char *) buf + pos, len - pos);
		if (got < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			warning("read() failed (%s)\n", strerror(errno));
			return -1;
		} else if (got == 0) {
			warning("unexpected EOF\n");
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
		ssize_t sent = write(fd, (const char *) buf + pos, len - pos);
		if (sent < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			warning("write() failed (%s)\n", strerror(errno));
			return -1;
		}
		pos += sent;
	}
	return 0;
}
