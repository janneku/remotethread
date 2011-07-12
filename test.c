/*
 * An example program of using remotethread to do distributed processing
 *
 * 1) Start one or more server processes by running ./remotethread-server
 * 2) Run this program and give the IP addresses of the machines running the
 *    server processes as command line arguments (--remotethread [ip])
 */
#include <remotethread.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFFER_LEN		(1024*1024)
#define CHUNKS			8
#define CHUNK_LEN		(BUFFER_LEN / CHUNKS)

void *xor_func(const void *param, size_t param_len, size_t *reply_len)
{
	const unsigned char *par = param;

	if (param_len % 2)
		return NULL;
	*reply_len = param_len / 2;
	unsigned char *reply = malloc(*reply_len);
	if (reply == NULL)
		return NULL;

	size_t i;
	for (i = 0; i < param_len / 2; ++i) {
		reply[i] = par[i] ^ par[i + param_len / 2];
	}
	return reply;
}

int main(int argc, char **argv)
{
	if (init_remotethread(&argc, &argv))
		return 1;

	unsigned char *buf = malloc(BUFFER_LEN);

	size_t i;
	for (i = 0; i < BUFFER_LEN; ++i)
		buf[i] = rand();

	struct remotethread *threads[CHUNKS];

	/* submit threads */
	for (i = 0; i < CHUNKS; ++i) {
		threads[i] = call_remotethread(xor_func, buf + i * CHUNK_LEN,
					       CHUNK_LEN);
		if (threads[i] == NULL)
			break;
	}

	/* wait replies */
	for (i = 0; i < CHUNKS; ++i) {
		if (threads[i] == NULL)
			break;

		size_t reply_len;
		void *reply = wait_remotethread(threads[i], &reply_len);
		/* do something with the reply */
		free(reply);

		destroy_remotethread(threads[i]);
	}
	free(buf);

	return 0;
}
