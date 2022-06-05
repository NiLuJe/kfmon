/*
	KFMon: Kobo inotify-based launcher
	Copyright (C) 2016-2022 NiLuJe <ninuje@gmail.com>
	SPDX-License-Identifier: GPL-3.0-or-later

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// Small helpers related to socket handling for IPC

#include "sock_utils.h"

// Check that we can still write to the socket (i.e., that the remote hasn't closed the socket early).
int
    can_write_to_socket(int data_fd, int timeout, size_t attempts)
{
	int           status = EXIT_SUCCESS;
	struct pollfd pfd    = { 0 };
	pfd.fd               = data_fd;
	pfd.events           = POLLOUT;

	size_t retry = 0U;
	while (1) {
		int poll_num = poll(&pfd, 1, timeout);
		if (poll_num == -1) {
			if (errno == EINTR) {
				continue;
			}
			// The caller will then be free to check errno
			status = EXIT_FAILURE;
			break;
		}

		if (poll_num > 0) {
			// Remote closed the connection, can't write to it anymore (even if POLLOUT is still set).
			if (pfd.revents & POLLHUP) {
				// That's obviously not good ;p
				status = EPIPE;
				break;
			}

			if (pfd.revents & POLLOUT) {
				// Remote is ready for us, let's proceed.
				status = EXIT_SUCCESS;
				break;
			}
		}

		if (poll_num == 0) {
			// Timed out, increase the retry counter
			retry++;
		}

		// Drop the axe after the final attempt
		if (retry >= attempts) {
			// Let the caller know
			status = ETIMEDOUT;
			break;
		}
	}

	return status;
}
