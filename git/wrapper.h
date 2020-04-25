// SPDX-License-Identifier: GPL-2.0-only

#ifndef _GIT_WRAPPERS_H
#define _GIT_WRAPPERS_H

#include <errno.h>
#include <poll.h>
#include <unistd.h>

ssize_t xread(int fd, void* buf, size_t len);
ssize_t xwrite(int fd, const void* buf, size_t len);
ssize_t read_in_full(int fd, void* buf, size_t count);
ssize_t write_in_full(int fd, const void* buf, size_t count);

#endif
