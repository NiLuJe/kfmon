/*
 * Various trivial helper wrappers around standard functions
 * Imported from the Git project, c.f.,
 * https://github.com/git/git/blob/master/wrapper.c
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "wrapper.h"

// Clamp IO chunks to the smaller of 8 MiB or SSIZE_MAX,
// to deal with various implementation quirks on really old Linux,
// macOS, or AIX/IRIX.
// c.f., gnulib & busybox for similar stories.
// Since we ourselves are 32 bit Linux-bound, 8 MiB suits us just fine.
#define MAX_IO_SIZE_DEFAULT (8 * 1024 * 1024)
#if defined(SSIZE_MAX) && (SSIZE_MAX < MAX_IO_SIZE_DEFAULT)
#	define MAX_IO_SIZE SSIZE_MAX
#else
#	define MAX_IO_SIZE MAX_IO_SIZE_DEFAULT
#endif

// NOTE: This effectively restores blocking behavior for the duration of the read/write call.
//       If you need a timeout, make sure to wrap this in an appropriate polling mechanism.
//       (A crappier and non thread safe workaround would be a different handler,
//       with a static retry counter, a poll that can timeout, and that on the final timeout,
//       would clear the retry counter, set errno to ETIMEDOUT and return failure).
static int
    handle_nonblock(int fd, short poll_events, int err)
{
	struct pollfd pfd;

	// NOTE: EWOULDBLOCK is defined as EAGAIN on Linux, no need to check both.
	if (err != EAGAIN)
		return 0;

	pfd.fd     = fd;
	pfd.events = poll_events;

	/*
	 * no need to check for errors, here;
	 * a subsequent read/write will detect unrecoverable errors
	 */
	poll(&pfd, 1, -1);
	return 1;
}

/*
 * xread() is the same a read(), but it automatically restarts read()
 * operations with a recoverable error (EAGAIN and EINTR). xread()
 * DOES NOT GUARANTEE that "len" bytes is read even if the data is available.
 */
ssize_t
    xread(int fd, void* buf, size_t len)
{
	ssize_t nr;
	if (len > MAX_IO_SIZE)
		len = MAX_IO_SIZE;
	while (1) {
		nr = read(fd, buf, len);
		if (nr < 0) {
			if (errno == EINTR)
				continue;
			if (handle_nonblock(fd, POLLIN, errno))
				continue;
		}
		return nr;
	}
}

/*
 * xwrite() is the same a write(), but it automatically restarts write()
 * operations with a recoverable error (EAGAIN and EINTR). xwrite() DOES NOT
 * GUARANTEE that "len" bytes is written even if the operation is successful.
 */
ssize_t
    xwrite(int fd, const void* buf, size_t len)
{
	ssize_t nr;
	if (len > MAX_IO_SIZE)
		len = MAX_IO_SIZE;
	while (1) {
		nr = write(fd, buf, len);
		if (nr < 0) {
			if (errno == EINTR)
				continue;
			if (handle_nonblock(fd, POLLOUT, errno))
				continue;
		}

		return nr;
	}
}

ssize_t
    read_in_full(int fd, void* buf, size_t count)
{
	char*   p     = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t loaded = xread(fd, p, count);
		if (loaded < 0)
			return -1;
		if (loaded == 0)
			return total;
		count -= (size_t) loaded;
		p += loaded;
		total += loaded;
	}

	return total;
}

ssize_t
    write_in_full(int fd, const void* buf, size_t count)
{
	const char* p     = buf;
	ssize_t     total = 0;

	while (count > 0) {
		ssize_t written = xwrite(fd, p, count);
		if (written < 0)
			return -1;
		if (!written) {
			errno = ENOSPC;
			return -1;
		}
		count -= (size_t) written;
		p += written;
		total += written;
	}

	return total;
}
