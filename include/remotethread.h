#ifndef _REMOTETHREAD_H
#define _REMOTETHREAD_H

#include <string.h>

struct remotethread;

typedef void *(*remotethread_func_t)(const void *param, size_t param_len,
				     size_t *reply_len);

void *remotethread_malloc(size_t size, const void *caller);
void remotethread_free(void *ptr, const void *caller);

#define RT_EAGAIN	((void *) -1)

struct remotethread *call_remotethread(remotethread_func_t func,
				       const void *param, size_t param_len);
void *poll_remotethread(struct remotethread *rt, size_t *reply_len);
void *wait_remotethread(struct remotethread *rt, size_t *reply_len);
void destroy_remotethread(struct remotethread *rt);

int init_remotethread(int *argc, char ***argv);

#endif
