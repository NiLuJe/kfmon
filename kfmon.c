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

// Because daemon() only appeared in glibc 2.21
static int daemonize(void)
{
	int fd;

	switch (fork()) {
		case -1:
			return -1;
		case 0:
			break;
		default:
			_exit(0);
	}

	if (setsid() == -1)
		return -1;

	// Double fork, for... reasons!
	signal(SIGHUP, SIG_IGN);
	switch (fork()) {
		case -1:
			return -1;
		case 0:
			break;
		default:
			_exit(0);
	}

	if (chdir("/") == -1)
		return -1;

	umask(0);

	// Redirect stdin & stdout to /dev/null
	if ((fd = open("/dev/null", O_RDWR)) != -1) {
		dup2(fd, fileno(stdin));
		dup2(fd, fileno(stdout));
		if (fd > 2)
			close (fd);
	} else {
		fprintf(stderr, "Failed to redirect stdint & stdout to /dev/null\n");
		return -1;
	}

	// Redirect stderr to our logfile
	if ((fd = open(KFMON_LOGFILE, O_WRONLY | O_CREAT | O_APPEND, 0600)) != -1) {
		dup2(fd, fileno(stderr));
		if (fd > 2)
			close (fd);
	} else {
		fprintf(stderr, "Failed to redirect stderr to logfile '%s'\n", KFMON_LOGFILE);
		return -1;
	}

	return 0;
}

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

// Check if our target file has been processed by Nickel...
static int is_target_processed(int update)
{
	sqlite3 *db;
	sqlite3_stmt * stmt;
	int rc;
	int idx;
	int is_processed = 0;
	int needs_update = 0;

	CALL_SQLITE(open(KOBO_DB_PATH , &db));

	// NOTE: ContentType 6 should mean a book on pretty much anything since FW 1.9.17 (and why a book? Because Nickel currently identifies single PNGs as application/x-cbz, bless its cute little bytes).
	CALL_SQLITE(prepare_v2(db, "SELECT EXISTS(SELECT 1 FROM content WHERE ContentID = @id AND ContentType = '6');", -1, &stmt, NULL));

	idx = sqlite3_bind_parameter_index(stmt, "@id");
	CALL_SQLITE(bind_text(stmt, idx, "file://"KFMON_TARGET_FILE, -1, SQLITE_STATIC));

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		//LOG("SELECT SQL query returned: %d", sqlite3_column_int(stmt, 0));
		if (sqlite3_column_int(stmt, 0) == 1)
			is_processed = 1;
	}

	sqlite3_finalize(stmt);

	// Optionally, update the Title, Author & Comment fields to make them more useful...
	if (is_processed && update) {
		// Check if the DB has already been updated...
		CALL_SQLITE(prepare_v2(db, "SELECT Title FROM content WHERE ContentID = @id AND ContentType = '6';", -1, &stmt, NULL));

		idx = sqlite3_bind_parameter_index(stmt, "@id");
		CALL_SQLITE(bind_text(stmt, idx, "file://"KFMON_TARGET_FILE, -1, SQLITE_STATIC));

		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			//LOG("SELECT SQL query returned: %s", sqlite3_column_text(stmt, 0));
			if (strcmp((const char *)sqlite3_column_text(stmt, 0), "KOReader") != 0)
				needs_update = 1;
		}

		sqlite3_finalize(stmt);
	}
	if (needs_update) {
		CALL_SQLITE(prepare_v2(db, "UPDATE content SET Title = @title, Attribution = @author, Description = @comment WHERE ContentID = @id AND ContentType = '6';", -1, &stmt, NULL));

		idx = sqlite3_bind_parameter_index(stmt, "@title");
		CALL_SQLITE(bind_text(stmt, idx, "KOReader", -1, SQLITE_STATIC));
		idx = sqlite3_bind_parameter_index(stmt, "@author");
		CALL_SQLITE(bind_text(stmt, idx, "KOReader Devs", -1, SQLITE_STATIC));
		idx = sqlite3_bind_parameter_index(stmt, "@comment");
		CALL_SQLITE(bind_text(stmt, idx, "An eBook reader application", -1, SQLITE_STATIC));
		idx = sqlite3_bind_parameter_index(stmt, "@id");
		CALL_SQLITE(bind_text(stmt, idx, "file://"KFMON_TARGET_FILE, -1, SQLITE_STATIC));

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			LOG("UPDATE SQL query failed: %s", sqlite3_errmsg(db));
		} else {
			LOG("Successfully updated DB data for the target PNG");
		}

		sqlite3_finalize(stmt);
	}

	sqlite3_close(db);

	return is_processed;
}

/* Spawn a process and returns its pid...
 * Knowing that pid is all I care about, leave the popen()-like piping alone
 * Massiively inspired from popen2() implementations from https://stackoverflow.com/questions/548063 */
static pid_t spawn(const char **command)
{
	pid_t pid;

	pid = fork();

	if (pid < 0)
		return pid;
	else if (pid == 0) {
		// Sweet child o' mine!
		execvp(*command, command);
		perror("execvp");
		exit(1);
	}

	// We don't want to block, so we handle the wait() later...

	return pid;
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
				if (is_target_processed(1)) {
					// Do we want to spawn something?
					int spawn_something = 1;
					// Check if our last spawn (if we have one) is still alive...
					if (last_spawn_pid > 0) {
						int child_status;
						LOG("Checking if our last spawn (%d) is still alive . . .", last_spawn_pid);
						// Check without blocking if our child hasn't already exited, and reap it to avoid zombies if it has.
						// NOTE: Reaping only on OPEN is not optimal, granted, but it's better than nothing...
						pid_t child_pid = waitpid(last_spawn_pid, &child_status, WNOHANG);
						switch (child_pid) {
							case -1:
								perror("waitpid");
							case 0:
								// Still alive! Pass.
								spawn_something = 0;
								LOG("Last spawn (%d) is still alive, continue handling events . . .", last_spawn_pid);
								// This *should* apply to the outer for loop...
								continue;
							default:
								// NOTE: I don't think we can ever get a mismatch here, but log both anyway...
								LOG("Reaped our last spawn (reaped: %d vs. stored: %d)!", child_pid, last_spawn_pid);
								// And now we can forget about it!
								last_spawn_pid = 0;
						}
					}
					if (spawn_something) {
						LOG("Spawning %s . . .", KFMON_TARGET_SCRIPT);
						// We're using execvp()...
						const char *cmd[] = {KFMON_TARGET_SCRIPT, NULL};
						last_spawn_pid = spawn(cmd);
						LOG(". . . with pid: %d", last_spawn_pid);
					} else {
						LOG("Our last spawn (%d) is still alive!", last_spawn_pid);
					}
				} else {
					LOG("Target script '%s' appears not to have been processed by Nickel yet, don't launch anything . . .", KFMON_TARGET_SCRIPT);
				}
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
				} else {
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

	// Fly, little daemon!
	if (daemonize() != 0) {
		fprintf(stderr, "Failed to daemonize!\n");
		exit(EXIT_FAILURE);
	}

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
