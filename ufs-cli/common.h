// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __COMMON_H__
#define __COMMON_H__

#include <endian.h>
#include <linux/types.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#define SUCCESS  0
#define INIT	-1
#define ERROR	-2
#define INVAL	-3
#define AGAIN	-4

#define DEVICE_PATH_NAME_SIZE_MAX	256
#define U32_TO_STR_SIZE_MAX		12

#define pr_err(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)

#define DWORD(b3, b2, b1, b0) htobe32(((b3) << 24) | ((b2) << 16) |\
				      ((b1) << 8) | (b0))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

struct ufs_characteristics {
	__u32 id;
	const char *name;
};

int get_ull_from_cli(unsigned long long *val);
int get_value_from_cli(int *val);
int init_device_path(char *path);
int characteristics_look_up(struct ufs_characteristics *c, __u32 id);
void dump_hex(__u8 *buf, __u16 len);
int u32_to_str(uint32_t val, char *buf, size_t size);
uint64_t fast_rand64(uint64_t *seed);
#endif /* __COMMON_H__ */
