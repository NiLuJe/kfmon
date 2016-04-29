/*
	KFMon: Kobo inotify-based launcher
	Copyright (C) 2016 NiLuJe <ninuje@gmail.com>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>

/* Read all available inotify events from the file descriptor 'fd'.
   wd is the table of watch descriptors for the directories in argv.
   argc is the length of wd and argv.
   argv is the list of watched directories.
   Entry 0 of wd and argv is unused. */

static void handle_events(int fd, int *wd, int argc, char* argv[])
{
	/* Some systems cannot read integer variables if they are not
	   properly aligned. On other systems, incorrect alignment may
	   decrease performance. Hence, the buffer used for reading from
	   the inotify file descriptor should have the same alignment as
	   struct inotify_event. */
	char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event *event;
	int i;
	ssize_t len;
	char *ptr;

	/* Loop while events can be read from inotify file descriptor. */
	for (;;) {
	/* Read some events. */
		len = read(fd, buf, sizeof buf);
		if (len == -1 && errno != EAGAIN) {
			perror("read");
			exit(EXIT_FAILURE);
		}

		/* If the nonblocking read() found no events to read, then
		   it returns -1 with errno set to EAGAIN. In that case,
		   we exit the loop. */
		if (len <= 0)
			break;

		/* Loop over all events in the buffer */
		for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
			event = (const struct inotify_event *) ptr;

			/* Print event type */
			if (event->mask & IN_OPEN)
				printf("IN_OPEN: ");
			if (event->mask & IN_CLOSE_NOWRITE)
				printf("IN_CLOSE_NOWRITE: ");
			if (event->mask & IN_CLOSE_WRITE)
				printf("IN_CLOSE_WRITE: ");

			/* Print the name of the watched directory */
			for (i = 1; i < argc; ++i) {
				if (wd[i] == event->wd) {
					printf("%s/", argv[i]);
					break;
				}
			}

			/* Print the name of the file */
			if (event->len)
				printf("%s", event->name);

			/* Print type of filesystem object */
			if (event->mask & IN_ISDIR)
				printf(" [directory]\n");
			else
				printf(" [file]\n");
		}
	}
}

int main(int argc, char* argv[])
{
	char buf;
	int fd, i, poll_num;
	int *wd;
	nfds_t nfds;
	struct pollfd fds[2];

	if (argc < 2) {
		printf("Usage: %s PATH [PATH ...]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	printf("Press ENTER key to terminate.\n");

	/* Create the file descriptor for accessing the inotify API */
	fd = inotify_init1(IN_NONBLOCK);
	if (fd == -1) {
		perror("inotify_init1");
		exit(EXIT_FAILURE);
	}

	/* Allocate memory for watch descriptors */
	wd = calloc(argc, sizeof(int));
	if (wd == NULL) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	/* Mark directories for events
	   - file was opened
	   - file was closed */
	for (i = 1; i < argc; i++) {
		wd[i] = inotify_add_watch(fd, argv[i], IN_OPEN | IN_CLOSE);
		if (wd[i] == -1) {
			fprintf(stderr, "Cannot watch '%s'\n", argv[i]);
			perror("inotify_add_watch");
			exit(EXIT_FAILURE);
		}
	}

	/* Prepare for polling */
	nfds = 2;

	/* Console input */
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;

	/* Inotify input */
	fds[1].fd = fd;
	fds[1].events = POLLIN;

	/* Wait for events and/or terminal input */
	printf("Listening for events.\n");
	while (1) {
		poll_num = poll(fds, nfds, -1);
		if (poll_num == -1) {
			if (errno == EINTR)
				continue;
			perror("poll");
			exit(EXIT_FAILURE);
		}

		if (poll_num > 0) {
			if (fds[0].revents & POLLIN) {
				/* Console input is available. Empty stdin and quit */
				while (read(STDIN_FILENO, &buf, 1) > 0 && buf != '\n')
					continue;
				break;
			}

			if (fds[1].revents & POLLIN) {
				/* Inotify events are available */
				handle_events(fd, wd, argc, argv);
			}
		}
	}

	printf("Listening for events stopped.\n");

	/* Close inotify file descriptor */
	close(fd);

	free(wd);
	exit(EXIT_SUCCESS);
}
