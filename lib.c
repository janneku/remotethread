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
#include <malloc.h>
#include <sys/mman.h>
#include <assert.h>

#define MAX_SERVERS	16

struct remotethread {
	int fd;
	struct reply reply;
	size_t pos;
	char *buf;
	size_t reply_len;
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

enum {
	CHUNK_ALLOC = 1,
	CHUNK_FREE,
};

struct chunk {
	struct chunk *prev;
	size_t size;
	int status;
};

#define ALLOC_BEGIN	0x40000000
#define PAGE_SIZE	4096

static struct chunk *const first_chunk = (struct chunk *) ALLOC_BEGIN;
static struct chunk *last_chunk = NULL;
static struct chunk *alloc_chunk = (struct chunk *) ALLOC_BEGIN;
static char *current_end = (char *) ALLOC_BEGIN;

static size_t round_up(size_t val, size_t align)
{
	return (val + align - 1) & ~(align - 1);
}

static struct chunk *merge_free_chunks(struct chunk *chunk)
{
	/* merge with previous */
	if (chunk->prev && chunk->prev->status == CHUNK_FREE) {
		struct chunk *next = chunk;
		chunk = chunk->prev;
		chunk->size += next->size;

		next->status = 0xdeadbeef;

		/* fix back pointer */
		if (next == last_chunk)
			last_chunk = chunk;
		else {
			struct chunk *next =
				(struct chunk *) ((char *) chunk + chunk->size);
			next->prev = chunk;
		}
	}

	/* merge with next */
	struct chunk *next = (struct chunk *) ((char *) chunk + chunk->size);
	if (next != (struct chunk *) current_end && next->status == CHUNK_FREE) {
		chunk->size += next->size;

		next->status = 0xdeadbeef;

		/* fix back pointer */
		if (next == last_chunk)
			last_chunk = chunk;
		else {
			struct chunk *next =
				(struct chunk *) ((char *) chunk + chunk->size);
			next->prev = chunk;
		}
	}
	alloc_chunk = chunk;
	return chunk;
}

static struct chunk *grow_alloc(size_t size)
{
	size = round_up(size, PAGE_SIZE * 16);
	if (mmap(current_end, size, PROT_READ|PROT_WRITE,
		 MAP_PRIVATE|MAP_ANONYMOUS, 0, 0) == MAP_FAILED) {
		warning("unable to grow allocation\n");
		return NULL;
	}

	struct chunk *chunk = (void *) current_end;
	current_end += size;

	chunk->size = size;
	chunk->status = CHUNK_FREE;
	chunk->prev = last_chunk;
	last_chunk = chunk;
	return merge_free_chunks(chunk);
}

void remotethread_check_alloc(void)
{
	printf("----\n");
	const struct chunk *chunk = first_chunk;
	const struct chunk *prev = NULL;
	while (chunk != (struct chunk *) current_end) {
		printf("%p %zu %s\n", chunk, chunk->size,
			chunk->status == CHUNK_FREE ? "free" : "allocated");
		assert(chunk->prev == prev);
		prev = chunk;
		chunk = (struct chunk *) ((char *) chunk + chunk->size);
	}
	assert(prev == last_chunk);
}

static struct chunk *find_free_chunk(size_t size)
{
	struct chunk *chunk = alloc_chunk;
	if (chunk != (struct chunk *) current_end) {
		while (1) {
			if (chunk->status == CHUNK_FREE && chunk->size >= size) {
				alloc_chunk = chunk;
				return chunk;
			}
			/* go to next chunk */
			chunk = (struct chunk *) ((char *) chunk + chunk->size);
			if (chunk == (struct chunk *) current_end)
				chunk = first_chunk;

			if (chunk == alloc_chunk) {
				/* scanned all the way around */
				break;
			}
		}
	}
	return grow_alloc(size);
}

static struct chunk *split_chunk(struct chunk *chunk, size_t pos)
{
	struct chunk *new_chunk = (struct chunk *) ((char *) chunk + pos);
	new_chunk->size = chunk->size - pos;
	new_chunk->status = CHUNK_FREE;
	new_chunk->prev = chunk;

	/* fix back pointer */
	if (chunk == last_chunk)
		last_chunk = new_chunk;
	else {
		struct chunk *next =
			(struct chunk *) ((char *) chunk + chunk->size);
		next->prev = new_chunk;
	}
	chunk->size = pos;
	alloc_chunk = new_chunk;
	return new_chunk;
}

void *remotethread_malloc(size_t size, const void *caller)
{
	UNUSED(caller);
	size = round_up(size + sizeof(struct chunk), 64);

	struct chunk *chunk = find_free_chunk(size);
	if (chunk == NULL)
		return NULL;
	chunk->status = CHUNK_ALLOC;

	if (chunk->size >= size + 64) {
		/* split into two */
		split_chunk(chunk, size);
	}
	return chunk + 1;
}

void remotethread_free(void *ptr, const void *caller)
{
	UNUSED(caller);
	if (ptr == NULL)
		return;

	struct chunk *chunk = (struct chunk *) ptr - 1;
	assert(chunk->status == CHUNK_ALLOC);
	chunk->status = CHUNK_FREE;
	merge_free_chunks(chunk);
}

void *remotethread_realloc(void *ptr, size_t new_size, const void *caller)
{
	UNUSED(caller);
	if (ptr == NULL)
		return remotethread_malloc(new_size, caller);

	new_size = round_up(new_size + sizeof(struct chunk), 64);
	struct chunk *chunk = (struct chunk *) ptr - 1;
	assert(chunk->status == CHUNK_ALLOC);

	if (new_size < chunk->size) {
		/* shrink */
		if (chunk->size >= new_size + 64) {
			/* split into two */
			struct chunk *new_chunk = split_chunk(chunk, new_size);
			merge_free_chunks(new_chunk);
		}
	} else {
		/* merge with next */
		struct chunk *next =
			(struct chunk *) ((char *) chunk + chunk->size);
		if (next != (struct chunk *) current_end
		    && next->status == CHUNK_FREE
		    && chunk->size + next->size >= new_size) {
			chunk->size += next->size;

			next->status = 0xdeadbeef;

			/* fix alloc pointer */
			if (next == alloc_chunk)
				alloc_chunk = first_chunk;

			/* fix back pointer */
			if (next == last_chunk)
				last_chunk = chunk;
			else {
				struct chunk *next =
				(struct chunk *) ((char *) chunk + chunk->size);
				next->prev = chunk;
			}
			if (chunk->size >= new_size + 64) {
				/* split into two */
				split_chunk(chunk, new_size);
			}
		} else {
			/* does not fit, we have to copy */
			struct chunk *new_chunk = find_free_chunk(new_size);
			if (new_chunk == NULL)
				return NULL;
			new_chunk->status = CHUNK_ALLOC;

			if (new_chunk->size >= new_size + 64) {
				/* split into two */
				split_chunk(new_chunk, new_size);
			}
			memcpy(new_chunk + 1, chunk + 1,
				chunk->size - sizeof(struct chunk));
			chunk->status = CHUNK_FREE;
			merge_free_chunks(chunk);
			chunk = new_chunk;
		}
	}
	return chunk + 1;
}

struct remotethread *call_remotethread(remotethread_func_t func,
				       const void *param, size_t param_len)
{
	if (num_servers == 0) {
		static int warned = 0;
		if (warned == 0) {
			warning("no servers defined! use --remotethread [ip]\n");
			warned = 1;
		}
		return NULL;
	}

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		warning("socket() failed (%s)\n", strerror(errno));
		return NULL;
	}

	/* connect to a random server */
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_addr = servers[rand() % num_servers];
	sin.sin_port = htons(DEFAULT_PORT);
	if (connect(fd, (struct sockaddr *) &sin, sizeof sin)) {
		close(fd);
		warning("connect() failed (%s)\n", strerror(errno));
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

	/* create copy of the parameters */
	void *param_buf = remotethread_malloc(param_len, NULL);
	if (param_buf == NULL) {
		warning("Out of memory\n");
		close(fd);
		return NULL;
	}
	memcpy(param_buf, param, param_len);

	size_t alloc_len = current_end - (char *) ALLOC_BEGIN;

	struct call call;
	call.alloc_len = htonl(alloc_len);
	call.param_len = htonl(param_len);
	call.param = (uint64_t) param_buf;
	call.eip = (uint64_t) func;

	if (write_all(fd, &call, sizeof call)) {
		close(fd);
		remotethread_free(param_buf, NULL);
		return NULL;
	}
	if (write_all(fd, (void *) ALLOC_BEGIN, alloc_len)) {
		close(fd);
		remotethread_free(param_buf, NULL);
		return NULL;
	}

	remotethread_free(param_buf, NULL);

	struct remotethread *rt = calloc(1, sizeof *rt);
	rt->fd = fd;
	return rt;
}

void *poll_remotethread(struct remotethread *rt, size_t *reply_len)
{
	if (rt->reply_len == 0) {
		if (bytes_available(rt->fd) < sizeof(struct reply))
			return RT_EAGAIN;
		if (read_all(rt->fd, &rt->reply, sizeof(struct reply)))
			return NULL;

		if (rt->reply.status == STATUS_ERROR) {
			warning("server returned an error\n");
			return NULL;
		}

		rt->reply_len = ntohl(rt->reply.reply_len);
		rt->pos = 0;
		rt->buf = malloc(rt->reply_len);
		if (rt->buf == NULL) {
			warning("Out of memory\n");
			return NULL;
		}
	}

	size_t avail = bytes_available(rt->fd);
	if (avail == 0)
		return RT_EAGAIN;
	if (avail > rt->reply_len - rt->pos) {
		warning("extra bytes in reply?\n");
		avail = rt->reply_len - rt->pos;
	}

	size_t got = read_available(rt->fd, rt->buf + rt->pos, avail);
	if (got == 0) {
		free(rt->buf);
		return NULL;
	}
	rt->pos += got;
	if (rt->pos < rt->reply_len)
		return RT_EAGAIN;

	*reply_len = rt->reply_len;
	return rt->buf;
}

void *wait_remotethread(struct remotethread *rt, size_t *reply_len)
{
	if (rt->reply_len == 0) {
		if (read_all(rt->fd, &rt->reply, sizeof(struct reply)))
			return NULL;

		if (rt->reply.status == STATUS_ERROR) {
			warning("server returned an error\n");
			return NULL;
		}

		rt->reply_len = ntohl(rt->reply.reply_len);
		rt->pos = 0;
		rt->buf = malloc(rt->reply_len);
		if (rt->buf == NULL) {
			warning("Out of memory\n");
			return NULL;
		}
	}

	if (read_all(rt->fd, rt->buf + rt->pos, rt->reply_len - rt->pos)) {
		free(rt->buf);
		return NULL;
	}
	rt->pos = rt->reply_len;
	*reply_len = rt->reply_len;
	return rt->buf;
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

	size_t alloc_len = ntohl(call.alloc_len);
	size_t param_len = ntohl(call.param_len);

	if (grow_alloc(alloc_len) == NULL)
		return -1;
	if (read_all(fd, (void *) ALLOC_BEGIN, alloc_len))
		return -1;

	/* update last_chunk */
	struct chunk *chunk = first_chunk;
	while (chunk != (struct chunk *) current_end) {
		last_chunk = chunk;
		chunk = (struct chunk *) ((char *) chunk + chunk->size);
	}

	remotethread_func_t func = (remotethread_func_t) call.eip;
	const void *param = (void *) call.param;

	size_t reply_len;
	void *reply_buf = func(param, param_len, &reply_len);

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
