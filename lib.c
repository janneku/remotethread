/*
 * remotethread API library
 */
#include "utils.h"
#include "proto.h"
#include "remotethread.h"
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define MAX_SERVERS	16

struct remotethread {
	int fd;
};

static struct in_addr servers[MAX_SERVERS];
static int num_servers = 0;
static const char *my_binary = NULL;

static void *read_file(const char *fname, size_t *len)
{
	FILE *f = fopen(fname, "rb");
	if (f == NULL) {
		warning("Unable to open %s\n", fname);
		return 0;
	}
	fseek(f, 0, SEEK_END);
	*len = ftell(f);
	void *buf = malloc(*len);
	if (buf == NULL) {
		fclose(f);
		warning("Out of memory\n");
		return 0;
	}
	fseek(f, 0, SEEK_SET);
	fread(buf, 1, *len, f);
	fclose(f);
	return buf;
}

struct remotethread *call_remotethread(remotethread_func_t func,
				       const void *param, size_t param_len)
{
	if (num_servers == 0) {
		warning("no servers defined! use --remotethread [ip]\n");
		return NULL;
	}

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return NULL;
	}

	/* connect to a random server */
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_addr = servers[rand() % num_servers];
	sin.sin_port = htons(DEFAULT_PORT);
	if (connect(fd, (struct sockaddr *) &sin, sizeof sin)) {
		close(fd);
		perror("connect");
		return NULL;
	}

	size_t binary_len;
	void *binary = read_file(my_binary, &binary_len);
	if (binary == NULL) {
		close(fd);
		return NULL;
	}

	struct hello hello;
	hello.magic = htonl(MAGIC);
	hello.binary_len = htonl(binary_len);
	if (write_all(fd, &hello, sizeof hello)) {
		free(binary);
		close(fd);
		return NULL;
	}
	if (write_all(fd, binary, binary_len)) {
		free(binary);
		close(fd);
		return NULL;
	}
	free(binary);

	struct call call;
	call.param_len = htonl(param_len);
	call.eip = (uint64_t) func;

	if (write_all(fd, &call, sizeof call)) {
		close(fd);
		return NULL;
	}
	if (write_all(fd, param, param_len)) {
		close(fd);
		return NULL;
	}

	struct remotethread *rt = malloc(sizeof *rt);
	rt->fd = fd;
	return rt;
}

void *wait_remotethread(struct remotethread *rt, size_t *reply_len)
{
	struct reply reply;
	if (read_all(rt->fd, &reply, sizeof reply))
		return NULL;

	if (reply.status == STATUS_ERROR) {
		warning("server returned an error\n");
		return NULL;
	}

	*reply_len = ntohl(reply.reply_len);

	void *reply_buf = malloc(*reply_len);
	if (reply_buf == NULL) {
		warning("Out of memory\n");
		return NULL;
	}
	if (read_all(rt->fd, reply_buf, *reply_len)) {
		free(reply_buf);
		return NULL;
	}
	return reply_buf;
} 

void destroy_remotethread(struct remotethread *rt)
{
	close(rt->fd);
	free(rt);
}

static int slave(int fd)
{
	struct call call;
	if (read_all(fd, &call, sizeof call))
		return -1;

	size_t param_len = ntohl(call.param_len);
	void *param = malloc(param_len);
	if (param == NULL) {
		warning("Out of memory\n");
		return -1;
	}
	if (read_all(fd, param, param_len)) {
		free(param);
		return -1;
	}

	size_t reply_len;
	remotethread_func_t func = (remotethread_func_t) call.eip;
	void *reply_buf = func(param, param_len, &reply_len);
	free(param);

	if (reply_buf == NULL)
		return -1;

	struct reply reply;
	reply.status = STATUS_OK;
	reply.reply_len = htonl(reply_len);

	if (write_all(fd, &reply, sizeof reply)) {
		free(reply_buf);
		return -1;
	}
	if (write_all(fd, reply_buf, reply_len)) {
		free(reply_buf);
		return -1;
	}
	free(reply_buf);
	return 0;
}

int init_remotethread(int *argc, char ***argv)
{
	my_binary = (*argv)[0];

	if (*argc >= 3 && strcmp((*argv)[1], SLAVE_ARG) == 0) {
		/* we are a slave process */
		unlink(my_binary);

		int fd = atoi((*argv)[2]);
		if (slave(fd)) {
			struct reply reply;
			reply.status = STATUS_ERROR;
			reply.reply_len = 0;
			write_all(fd, &reply, sizeof reply);
		}
		close(fd);
		exit(0);
	}

	int i, j = 1;
	for (i = 1; i < *argc; ++i) {
		const char *arg = (*argv)[i];
		const char *val = (*argv)[i + 1];
		if (strcmp(arg, "--remotethread") == 0) {
			if (inet_pton(AF_INET, val, &servers[num_servers++])
			    < 1) {
				warning("invalid address: %s\n", val);
				return -1;
			}
			i++;
		} else {
			(*argv)[j++] = (*argv)[i];
		}
	}
	*argc = j;

	return 0;
}
