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

#include "kfmon.h"

// Return the current time formatted as 2016-04-29 @ 20:44:13 (used for logging)
char *get_current_time(void)
{
	// cf. strftime(3) & https://stackoverflow.com/questions/7411301
	time_t t;
	struct tm *lt;

	t = time(NULL);
	lt = localtime(&t);

	// Needs to be static to avoid dealing with painful memory handling...
	static char sz_time[24];
	strftime(sz_time, sizeof(sz_time), "%Y-%m-%d @ %H:%M:%S", lt);

	return sz_time;
}

// Check that our target mountpoint is indeed mounted...
static int is_target_mounted(void)
{
	// cf. http://program-nix.blogspot.fr/2008/08/c-language-check-filesystem-is-mounted.html
	FILE *mtab = NULL;
	struct mntent *part = NULL;
	int is_mounted = 0;

	if ((mtab = setmntent("/proc/mounts", "r")) != NULL) {
		while ((part = getmntent(mtab)) != NULL) {
			//LOG("Checking fs %s mounted on %s", part->mnt_fsname, part->mnt_dir);
			if ((part->mnt_dir != NULL) && (strcmp(part->mnt_dir, KFMON_TARGET_MOUNTPOINT)) == 0) {
				is_mounted = 1;
				break;
			}
		}
		endmntent(mtab);
	}

	return is_mounted;
}

// Monitor mountpoint activity...
static void wait_for_target_mountpoint(void)
{
	// cf. https://stackoverflow.com/questions/5070801
	int mfd = open("/proc/mounts", O_RDONLY, 0);
	struct pollfd pfd;
	int rv;

	int changes = 0;
	pfd.fd = mfd;
	pfd.events = POLLERR | POLLPRI;
	pfd.revents = 0;
	while ((rv = poll(&pfd, 1, 5)) >= 0) {
		if (pfd.revents & POLLERR) {
			LOG("Mountpoints changed (iteration nr. %d)", changes++);

			// Stop polling once we know our mountpoint is available...
			if (is_target_mounted()) {
				LOG("Yay! Target mountpoint is available!");
				break;
			}
		}
		pfd.revents = 0;

		// If we can't find our mountpoint after that many changes, assume we're screwed...
		if (changes > 10) {
			LOG("Too many mountpoint changes without finding our target. Going buh-bye!");
			exit(EXIT_FAILURE);
		}
	}
}

static int sqlite_callback(void *foo __attribute__ ((unused)), int argc, char **argv, char **azColName)
{
	int i;
	for (i = 0; i < argc; i++) {
		LOG("%s = %s", azColName[i], argv[i] ? argv[i] : "NULL");
	}
	return 0;
}

// Check if our target file has been processed by Nickel...
static int is_target_processed(void)
{
	sqlite3 *db;
	char *zErrMsg = 0;
	int rc;

	rc = sqlite3_open(KOBO_DB_PATH , &db);
	if (rc) {
		LOG("Can't open Nickel database: %s", sqlite3_errmsg(db));
		sqlite3_close(db);
		return 1;
	}

	// NOTE: ContentType 6 should mean a book on pretty much anything since FW 1.9.17 (and why a book? Because Nickel currently identifies single PNGs as application/x-cbz, bless its cute little bytes).
	rc = sqlite3_exec(db, "SELECT EXISTS(SELECT 1 FROM content WHERE ContentID = 'file://"KFMON_TARGET_FILE"' AND ContentType = '6');", sqlite_callback, 0, &zErrMsg);
	if (rc != SQLITE_OK) {
		LOG("SQL error: %s", zErrMsg);
		sqlite3_free(zErrMsg);
	}
	sqlite3_close(db);

	return 0;
}

/* Read all available inotify events from the file descriptor 'fd'. */
static int handle_events(int fd, int wd)
{
	/* Some systems cannot read integer variables if they are not
	   properly aligned. On other systems, incorrect alignment may
	   decrease performance. Hence, the buffer used for reading from
	   the inotify file descriptor should have the same alignment as
	   struct inotify_event. */
	char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event *event;
	ssize_t len;
	char *ptr;
	int ret;
	int destroyed_wd = 0;

	// Loop while events can be read from inotify file descriptor.
	for (;;) {
		// Read some events.
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

		// Loop over all events in the buffer
		for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
			event = (const struct inotify_event *) ptr;

			// Print event type
			if (event->mask & IN_OPEN) {
				LOG("Tripped IN_OPEN for %s", KFMON_TARGET_FILE);
				// FIXME: See what happens if we open KFMON_TARGET_FILE from inside KOReader itself...
				//	  We block on system(), so we can't do anything much from here...
				//	  We should at least update KOReader's startup script so it doesn't run multiple concurrent instances of itself.
				// Wait for a bit in case Nickel has some stupid crap to do...
				sleep(1);
				// Check that our target file has already been processed by Nickel before launching anything...
				is_target_processed();
				LOG("Launching %s . . .", KFMON_TARGET_SCRIPT);
				ret = system(KFMON_TARGET_SCRIPT);
				LOG(". . . which returned: %d", ret);
			}
			if (event->mask & IN_CLOSE)
				LOG("Tripped IN_CLOSE for %s", KFMON_TARGET_FILE);
			if (event->mask & IN_UNMOUNT)
				LOG("Tripped IN_UNMOUNT for %s", KFMON_TARGET_FILE);
			if (event->mask & IN_IGNORED) {
				LOG("Tripped IN_IGNORED for %s", KFMON_TARGET_FILE);
				// Remember that the watch was automatically destroyed so we can break from the loop...
				destroyed_wd = 1;
			}
			if (event->mask & IN_Q_OVERFLOW) {
				if (event->len) {
					LOG("Huh oh... Tripped IN_Q_OVERFLOW for %s", event->name);
				}
				else {
					LOG("Huh oh... Tripped IN_Q_OVERFLOW");
				}
				// Destroy our only watch, and break the loop
				if (inotify_rm_watch(fd, wd) == -1)
				{
					// That's too bad, but may not be fatal, so warn only...
					perror("inotify_rm_watch");
				}
				destroyed_wd = 1;
			}
		}

		// If we caught an event indicating that the watch was automatically destroyed, break the loop.
		if (destroyed_wd)
			break;
	}

	// And we have another outer loop to break, so pass that on...
	return destroyed_wd;
}

int main(int argc __attribute__ ((unused)), char* argv[] __attribute__ ((unused)))
{
	int fd, poll_num;
	int wd;
	struct pollfd pfd;

	// We pretty much want to loop forever...
	while (1) {
		LOG("Beginning the main loop.");

		// Create the file descriptor for accessing the inotify API
		LOG("Initializing inotify.");
		fd = inotify_init1(IN_NONBLOCK);
		if (fd == -1) {
			perror("inotify_init1");
			exit(EXIT_FAILURE);
		}

		// Make sure our target file is available (i.e., the partition it resides in is mounted)
		if (!is_target_mounted()) {
			LOG("%s isn't mounted, waiting for it to be . . .", KFMON_TARGET_MOUNTPOINT);
			// If it's not, wait for it to be...
			wait_for_target_mountpoint();
		}

		// Mark target file for 'file was opened' event
		wd = inotify_add_watch(fd, KFMON_TARGET_FILE, IN_OPEN | IN_CLOSE);
		if (wd == -1) {
			LOG("Cannot watch '%s'", KFMON_TARGET_FILE);
			perror("inotify_add_watch");
			exit(EXIT_FAILURE);
		}

		// Inotify input
		pfd.fd = fd;
		pfd.events = POLLIN;

		// Wait for events
		LOG("Listening for events.");
		while (1) {
			poll_num = poll(&pfd, 1, -1);
			if (poll_num == -1) {
				if (errno == EINTR)
					continue;
				perror("poll");
				exit(EXIT_FAILURE);
			}

			if (poll_num > 0) {
				if (pfd.revents & POLLIN) {
					// Inotify events are available
					if (handle_events(fd, wd))
						// Go back to the main loop if we exited early (because the watch was destroyed automatically after an unmount)
						break;
				}
			}
		}
		LOG("Stopped listening for events.");

		// Close inotify file descriptor
		close(fd);
	}

	exit(EXIT_SUCCESS);
}
