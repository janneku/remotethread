#include <remotethread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define NUM_ALLOCS		100

int main(int argc, char **argv)
{
	if (init_remotethread(&argc, &argv))
		return 1;

	srand(time(NULL));

	int i;
	int retry;
	char *ptr[NUM_ALLOCS];
	size_t p;

	for (i = 0; i < NUM_ALLOCS; ++i) {
		size_t len = 256 + 64*i;
		ptr[i] = remotethread_malloc(len, NULL);
		memset(ptr[i], i, len);
		remotethread_check_alloc();
	}
	for (retry = 0; retry < NUM_ALLOCS / 2; ++retry) {
		i = rand() % NUM_ALLOCS;
		size_t len = 256 + 64*i;
		if (ptr[i]) {
			for (p = 0; p < len; ++p)
				assert(ptr[i][p] == i);
			remotethread_free(ptr[i], NULL);
		}
		ptr[i] = NULL;
		remotethread_check_alloc();
	}
	for (i = 0; i < NUM_ALLOCS; ++i) {
		size_t len = 256 + 64*i;
		if (ptr[i] == NULL) {
			ptr[i] = remotethread_malloc(len * 2, NULL);
			memset(ptr[i], i, len * 2);
		} else {
			ptr[i] = remotethread_realloc(ptr[i], len * 2, NULL);
			memset(ptr[i] + len, i, len);
		}
		remotethread_check_alloc();
	}
	for (i = NUM_ALLOCS - 1; i >= 0; --i) {
		size_t len = 256 + 64*i;
		if (ptr[i]) {
			for (p = 0; p < len * 2; ++p)
				assert(ptr[i][p] == i);
			remotethread_free(ptr[i], NULL);
		}
		remotethread_check_alloc();
	}

	return 0;
}
