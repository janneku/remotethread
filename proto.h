#ifndef _PROTO_H
#define _PROTO_H

#include <stdint.h>

#define MAGIC			0x4a33de22
#define SLAVE_ARG		"--remotethread-slave"

#define DEFAULT_PORT		12950

#define PACKED		__attribute__((packed))

struct hello {
	uint32_t magic;
	uint32_t binary_len;
} PACKED;

struct call {
	uint32_t param_len;
	uint64_t eip;
} PACKED;

#define STATUS_OK	1
#define STATUS_ERROR	2

struct reply {
	uint8_t status;
	uint32_t reply_len;
} PACKED;

#endif
