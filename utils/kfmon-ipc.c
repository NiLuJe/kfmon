/*
	KFMon: Kobo inotify-based launcher
	Copyright (C) 2016-2020 NiLuJe <ninuje@gmail.com>
	SPDX-License-Identifier: GPL-2.0-or-later

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// Small client that sends stdin to the KFMon IPC socket

// Because we're pretty much Linux-bound ;).
#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

#include "../git/wrapper.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// Path to our IPC Unix socket
#define KFMON_IPC_SOCKET "/tmp/kfmon-ipc.ctl"

// Drain stdin and send it to the IPC socket
static bool
    handle_stdin(int data_fd)
{
	// CHeck how many bytes we need to drain
	int bytes = 0;
	if (ioctl(fileno(stdin), FIONREAD, &bytes) == -1) {
		fprintf(stderr, "Aborting: ioctl: %m!\n");
		exit(EXIT_FAILURE);
	}

	// If there's nothing to read, abort.
	// We can apparently happily end up with a POLLIN flag and yet FIONREAD returning 0 when sending a ^D, for instance...
	if (bytes == 0) {
		return false;
	}

	// Eh, recycle PIPE_BUF, it should be more than enough for our needs.
	char buf[PIPE_BUF] = { 0 };

	// Now that we know how much to read, do it!
	ssize_t len = read_in_full(fileno(stdin), buf, (size_t) bytes);
	if (len < 0) {
		// Only actual failures are left, xread handles the rest
		fprintf(stderr, "Aborting: read: %m!\n");
		// FIXME: Make non-fatal?
		exit(EXIT_FAILURE);
	}

	// Send it over the socket (w/ NUL)
	buf[bytes] = '\0';
	if (write_in_full(data_fd, buf, (size_t)(bytes + 1)) < 0) {
		// Only actual failures are left, xwrite handles the rest
		fprintf(stderr, "Aborting: write: %m!\n");
		exit(EXIT_FAILURE);
	}

	// Done
	return true;
}

// Handle replies from the IPC socket
static bool
    handle_reply(int data_fd)
{
	// Eh, recycle PIPE_BUF, it should be more than enough for our needs.
	char buf[PIPE_BUF] = { 0 };

	// We don't actually know the size of the reply, so, best effort here.
	ssize_t len = xread(data_fd, buf, sizeof(buf));
	if (len < 0) {
		// Only actual failures are left, xread handles the rest
		fprintf(stderr, "Aborting: read: %m!\n");
		// FIXME: Make non-fatal?
		exit(EXIT_FAILURE);
	}

	// Ensure buffer is NUL-terminated before we start playing with it
	buf[PIPE_BUF - 1] = '\0';
	// Then print it!
	fprintf(stderr, "<<< Got a reply:\n");
	fprintf(stdout, "%.*s", (int) len, buf);

	if (len == 0) {
		// EoF, we're done, signal our polling to close the connection
		return true;
	}
	// Remote still has something to say?
	return false;
}

// Main entry point
int
    main(void)
{
	// Setup the local socket
	int data_fd = -1;
	if ((data_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) == -1) {
		fprintf(stderr, "Failed to create local IPC socket (socket: %m), aborting!\n");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_un sock_name = { 0 };
	sock_name.sun_family         = AF_UNIX;
	strncpy(sock_name.sun_path, KFMON_IPC_SOCKET, sizeof(sock_name.sun_path) - 1);

	// Connect to IPC socket
	if (connect(data_fd, (const struct sockaddr*) &sock_name, sizeof(sock_name)) == -1) {
		fprintf(stderr, "IPC is down (connect: %m), aborting!\n");
		exit(EXIT_FAILURE);
	}

	// Cheap-ass prompt is cheap!
	fprintf(stderr, ">>> ");
	// We'll be polling both stdin and the socket...
	int           poll_num;
	nfds_t        nfds    = 2;
	struct pollfd pfds[2] = { 0 };
	// stdin
	// We'll need to make it non-blocking, first...
	int flflags = fcntl(fileno(stdin), F_GETFL, 0);
	if (flflags == -1) {
		fprintf(stderr, "Aborting: getfl fcntl: %m!\n");
		exit(EXIT_FAILURE);
	}
	if (fcntl(fileno(stdin), F_SETFL, flflags | O_NONBLOCK) == -1) {
		fprintf(stderr, "Aborting: setfl fcntl: %m!\n");
		exit(EXIT_FAILURE);
	}

	pfds[0].fd     = fileno(stdin);
	pfds[0].events = POLLIN;
	// Data socket
	pfds[1].fd     = data_fd;
	pfds[1].events = POLLIN;

	// Chat with hot sockets in your area!
	while (1) {
		poll_num = poll(pfds, nfds, -1);
		if (poll_num == -1) {
			if (errno == EINTR) {
				continue;
			}
			fprintf(stderr, "Aborting: poll: %m!\n");
			exit(EXIT_FAILURE);
		}

		if (poll_num > 0) {
			// There's potentially data to be read in stdin
			if (pfds[0].revents & POLLIN) {
				if (!handle_stdin(data_fd)) {
					// There wasn't actually any data left in stdin
					fprintf(stderr, "No more data in stdin!\n");
					goto cleanup;
				}
				// If it was also closed (i.e., it's a pipe), go back to poll to check for replies now.
				if (pfds[0].revents & POLLHUP) {
					continue;
				}
			}

			if (pfds[1].revents & POLLIN) {
				// There was a reply from the socket
				if (handle_reply(data_fd)) {
					// We've successfully handled all input data, we're done!
					//break;
				}
			}

			// stdin was closed,
			if (pfds[0].revents & POLLHUP) {
				fprintf(stderr, "stdin was closed!\n");
				goto cleanup;
			}
			// Remote closed the connection
			if (pfds[1].revents & POLLHUP) {
				fprintf(stderr, "Remote closed the connection!\n");
				goto cleanup;
			}
		}

		// Back to sending...
		fprintf(stderr, ">>> ");
	}

cleanup:
	// Bye now!
	close(data_fd);

	return EXIT_SUCCESS;
}
