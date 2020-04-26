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

// Small helpers related to socket handling for IPC

#include "sock_utils.h"

// Check that we can still write to the socket (i.e., that the remote hasn't closed the socket early).
// Returns ETIMEDOUT if it's not ready after <attempts> * <timeout>ms
//   Set timeout to -1 and attempts to > 0 to wait indefinitely.
// Returns EPIPE if remote has closed the connection
// Returns EXIT_SUCCESS if remote is ready for us
// Anything else is poll's errno
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
			// errno *should* be set accordingly, but try very hard not to return 0, just in case...
			status = errno ? errno : EXIT_FAILURE;
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
