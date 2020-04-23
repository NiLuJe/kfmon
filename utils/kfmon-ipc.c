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
#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// Path to our IPC Unix socket
#define KFMON_IPC_SOCKET "/tmp/kfmon-ipc.ctl"

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

	// Chat with hot sockets in your area!
	char*   line = NULL;
	size_t  len  = 0;
	ssize_t nread;
	fprintf(stderr, ">>> ");
	// Read stdin line by line...
	while ((nread = getline(&line, &len, stdin)) != -1) {
		// Send it
		if (write_in_full(data_fd, line, nread) < 0) {
			// Only actual failures are left, xwrite handles the rest
			fprintf(stderr, "Aborting: write: %m!\n");
			exit(EXIT_FAILURE);
		}

		// Wait a bit in case there's a reply
		int           poll_num;
		struct pollfd pfd = { 0 };
		// Data socket
		pfd.fd     = data_fd;
		pfd.events = POLLIN;

		// Wait for data for 500ms
		while (1) {
			poll_num = poll(&pfd, 1, 500);
			if (poll_num == -1) {
				if (errno == EINTR) {
					continue;
				}
				fprintf(stderr, "Aborting: poll: %m!\n");
				exit(EXIT_FAILURE);
			}

			if (poll_num > 0) {
				if (pfd.revents & POLLIN) {
					// There's data to be read!
					if (handle_reply(data_fd)) {
						// We've successfully handled all input data, we're done!
						break;
					}
				}
			}

			if (poll_num == 0) {
				// Timed out, we're done!
				fprintf(stderr, "<<<!>>> No reply after 500ms\n");
				break;
			}
		}

		// Back to sending...
		fprintf(stderr, ">>> ");
	}
	free(line);

	// Bye now!
	close(data_fd);

	return EXIT_SUCCESS;
}
