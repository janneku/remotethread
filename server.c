/*
 * remotethread server
 */
#include "utils.h"
#include "proto.h"
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>

int write_file(const char *fname, const void *buf, size_t len)
{
	FILE *f = fopen(fname, "wb");
	if (f == NULL) {
		warning("Unable to write to %s\n", fname);
		return -1;
	}
	fwrite(buf, 1, len, f);
	fclose(f);
	return 0;
}

void process(int fd)
{
	struct hello hello;
	if (read_all(fd, &hello, sizeof hello))
		return;

	if (ntohl(hello.magic) != MAGIC) {
		warning("Invalid magic\n");
		return;
	}

	size_t binary_len = ntohl(hello.binary_len);
	void *binary = malloc(binary_len);
	if (binary == NULL) {
		warning("Unable to allocate binary\n");
		return;
	}
	if (read_all(fd, binary, binary_len)) {
		free(binary);
		return;
	}

	char fname[64];
	sprintf(fname, "/tmp/remotethread-%d", getpid());
	if (write_file(fname, binary, binary_len)) {
		free(binary);
		return;
	}
	free(binary);

	if (chmod(fname, 0700)) {
		perror("chmod");
		unlink(fname);
		return;
	}

	char buf[64];
	sprintf(buf, "%d", fd);
	if (execl(fname, fname, SLAVE_ARG, buf, NULL)) {
		perror("exec");
		unlink(fname);
	}
}

int main()
{
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		perror("socket");
		return 1;
	}

	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(DEFAULT_PORT);
	if (bind(listen_fd, (struct sockaddr *) &sin, sizeof sin)) {
		perror("bind");
		return 1;
	}

	if (listen(listen_fd, 10)) {
		perror("listen");
		return 1;
	}
	while (1) {
		struct sockaddr_in sin;
		socklen_t slen = sizeof sin;
		int fd = accept(listen_fd, (struct sockaddr *) &sin, &slen);
		if (fd < 0) {
			perror("accept");
			continue;
		}
		pid_t pid = fork();
		if (pid == 0) {
			int i;
			for (i = 3; i < 1000; ++i) {
				if (i != fd)
					close(i);
			}
			process(fd);
			/* if we ge back an error occured */
			struct reply reply;
			reply.status = STATUS_ERROR;
			reply.reply_len = 0;
			write_all(fd, &reply, sizeof reply);
			close(fd);
			_exit(1);
		}
		close(fd);
	}
	return 0;
}
