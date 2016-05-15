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
	int flags = O_WRONLY | O_CREAT | O_APPEND;
	// Check if we need to truncate our log because it has grown too much...
	struct stat st;
	if ((stat(KFMON_LOGFILE, &st) == 0) && (S_ISREG(st.st_mode))) {
		// Truncate if > 1MB
		if (st.st_size > 1*1024*1024)
			flags |= O_TRUNC;
	}
	if ((fd = open(KFMON_LOGFILE, flags, 0600)) != -1) {
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
static bool is_target_mounted(void)
{
	// cf. http://program-nix.blogspot.fr/2008/08/c-language-check-filesystem-is-mounted.html
	FILE *mtab = NULL;
	struct mntent *part = NULL;
	bool is_mounted = false;

	if ((mtab = setmntent("/proc/mounts", "r")) != NULL) {
		while ((part = getmntent(mtab)) != NULL) {
			DBGLOG("Checking fs %s mounted on %s", part->mnt_fsname, part->mnt_dir);
			if ((part->mnt_dir != NULL) && (strcmp(part->mnt_dir, KFMON_TARGET_MOUNTPOINT)) == 0) {
				is_mounted = true;
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
		if (changes > 15) {
			LOG("Too many mountpoint changes without finding our target. Going buh-bye!");
			exit(EXIT_FAILURE);
		}
	}
}

// Handle parsing the main KFMon config
static int daemon_handler(void *user, const char *section, const char *key, const char *value) {
	DaemonConfig *pconfig = (DaemonConfig *)user;

	#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(key, n) == 0
	if (MATCH("daemon", "db_timeout")) {
		pconfig->db_timeout = atoi(value);
	} else if (MATCH("daemon", "use_syslog")) {
		pconfig->use_syslog = atoi(value);
	} else {
		return 0;	// unknown section/name, error
	}
	return 1;
}

// Handle parsing a watch config
static int watch_handler(void *user, const char *section, const char *key, const char *value) {
	WatchConfig *pconfig = (WatchConfig *)user;

	#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(key, n) == 0
	// NOTE: Crappy strncpy() usage, but those char arrays are zeroed first (hence the MAX-1 len to ensure that we're NULL terminated)...
	if (MATCH("watch", "filename")) {
		strncpy(pconfig->filename, value, PATH_MAX-1);
	} else if (MATCH("watch", "action")) {
		strncpy(pconfig->action, value, PATH_MAX-1);
	} else if (MATCH("watch", "do_db_update")) {
		pconfig->do_db_update = atoi(value);
	} else if (MATCH("watch", "skip_db_checks")) {
		pconfig->skip_db_checks = atoi(value);
	} else if (MATCH("watch", "db_title")) {
		strncpy(pconfig->db_title, value, DB_SZ_MAX-1);
	} else if (MATCH("watch", "db_author")) {
		strncpy(pconfig->db_author, value, DB_SZ_MAX-1);
	} else if (MATCH("watch", "db_comment")) {
		strncpy(pconfig->db_comment, value, DB_SZ_MAX-1);
	} else {
		return 0;	// unknown section/name, error
	}
	return 1;
}

// Load our config files...
static int load_config() {
	// Our config files live in the target mountpoint...
	if (!is_target_mounted()) {
		LOG("%s isn't mounted, waiting for it to be . . .", KFMON_TARGET_MOUNTPOINT);
		// If it's not, wait for it to be...
		wait_for_target_mountpoint();
	}

	// Walk the config directory to pickup our ini files... (c.f., https://keramida.wordpress.com/2009/07/05/fts3-or-avoiding-to-reinvent-the-wheel/)
	FTS *ftsp;
	FTSENT *p, *chp;
	// We only need to walk a single directory...
	char *cfg_path[] = {KFMON_CONFIGPATH, NULL};
	int rval = 0;

	if ((ftsp = fts_open(cfg_path, FTS_COMFOLLOW | FTS_LOGICAL | FTS_NOCHDIR | FTS_NOSTAT | FTS_XDEV, NULL)) == NULL) {
		perror("[KFMon] fts_open");
		return -1;
	}
	// Initialize ftsp with as many toplevel entries as possible.
	chp = fts_children(ftsp, 0);
	if (chp == NULL) {
		// No files to traverse!
		LOG("Config directory '%s' appears to be empty, aborting!", KFMON_CONFIGPATH);
		fts_close(ftsp);
		return -1;
	}
	while ((p = fts_read(ftsp)) != NULL) {
		switch (p->fts_info) {
			case FTS_F:
				// Check if it's a .ini...
				if (strncasecmp(p->fts_name+(p->fts_namelen-4), ".ini", 4) == 0) {
					LOG("Trying to load config file '%s' . . .", p->fts_path);
					// The main config has to be parsed slightly differently...
					if (strncasecmp(p->fts_name, "kfmon.ini", 4) == 0) {
						if (ini_parse(p->fts_path, daemon_handler, &daemon_config) < 0) {
							LOG("Failed to parse main config file '%s'!", p->fts_name);
							// Flag as a failure...
							rval = -1;
						}
						LOG("Daemon config loaded from '%s': db_timeout=%d, use_syslog=%d", p->fts_name, daemon_config.db_timeout, daemon_config.use_syslog);
					} else {
						// NOTE: Don't blow up when trying to store more watches than we have space for...
						if (watch_count >= WATCH_MAX) {
							LOG("We've already setup the maximum amount of watches we can handle (%d), discarding '%s'!", WATCH_MAX, p->fts_name);
							// Don't flag this as a hard failure, just warn and go on...
							break;
						}

						if (ini_parse(p->fts_path, watch_handler, &watch_config[watch_count]) < 0) {
							LOG("Failed to parse watch config file '%s'!", p->fts_name);
							// Flag as a failure...
							rval = -1;
						}
						LOG("Watch config @ index %zd loaded from '%s': filename=%s, action=%s, do_db_update=%d, db_title=%s, db_author=%s, db_comment=%s", watch_count, p->fts_name, watch_config[watch_count].filename, watch_config[watch_count].action, watch_config[watch_count].do_db_update, watch_config[watch_count].db_title, watch_config[watch_count].db_author, watch_config[watch_count].db_comment);
						// Switch to the next slot!
						watch_count++;
					}
				}
				break;
			default:
				break;
		}
	}
	fts_close(ftsp);

#ifdef DEBUG
	// Let's recap...
	DBGLOG("Daemon config recap: db_timeout=%d, use_syslog=%d", daemon_config.db_timeout, daemon_config.use_syslog);
	for (unsigned int watch_idx = 0; watch_idx < watch_count; watch_idx++) {
		DBGLOG("Watch config @ index %d recap: filename=%s, action=%s, do_db_update=%d, skip_db_checks=%d, db_title=%s, db_author=%s, db_comment=%s", watch_idx, watch_config[watch_idx].filename, watch_config[watch_idx].action, watch_config[watch_idx].do_db_update, watch_config[watch_idx].skip_db_checks, watch_config[watch_idx].db_title, watch_config[watch_idx].db_author, watch_config[watch_idx].db_comment);
	}
#endif

	return rval;
}

// Implementation of Qt4's QtHash (cf. qhash @ https://github.com/kovidgoyal/calibre/blob/master/src/calibre/devices/kobo/driver.py#L35)
static unsigned int qhash(const unsigned char *bytes, size_t length) {
	unsigned int h = 0x00000000;
	unsigned int i;

	for(i = 0; i < length; i++) {
		h = (h << 4) + bytes[i];
		h ^= (h & 0xf0000000) >> 23;
		h &= 0x0fffffff;
	}

	return h;
}

// Check if our target file has been processed by Nickel...
static bool is_target_processed(unsigned int watch_idx, bool wait_for_db)
{
	sqlite3 *db;
	sqlite3_stmt * stmt;
	int rc;
	int idx;
	bool is_processed = false;
	bool needs_update = false;

#ifdef DEBUG
	// Bypass DB checks on demand for debugging purposes...
	if(watch_config[watch_idx].skip_db_checks)
		return true;
#endif

	// Did the user want to try to update the DB for this icon?
	bool update = watch_config[watch_idx].do_db_update;

	if (update) {
		CALL_SQLITE(open_v2(KOBO_DB_PATH , &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, NULL));
	} else {
		// Open the DB ro to be extra-safe...
		CALL_SQLITE(open_v2(KOBO_DB_PATH , &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, NULL));
	}

	// Wait at most for Nms on OPEN & N*2ms on CLOSE if we ever hit a locked database during any of our proceedings...
	// NOTE: The defaults timings (steps of 450ms) appear to work reasonably well on my H2O with a 50MB Nickel DB... (i.e., it trips on OPEN when Nickel is moderately busy, but if everything's quiet, we're good).
	//	 Time will tell if that's a good middle-ground or not ;). This is user configurable in kfmon.ini (db_timeout key).
	sqlite3_busy_timeout(db, daemon_config.db_timeout * (wait_for_db + 1));
	DBGLOG("SQLite busy timeout set to %dms", daemon_config.db_timeout * (wait_for_db + 1));

	// NOTE: ContentType 6 should mean a book on pretty much anything since FW 1.9.17 (and why a book? Because Nickel currently identifies single PNGs as application/x-cbz, bless its cute little bytes).
	CALL_SQLITE(prepare_v2(db, "SELECT EXISTS(SELECT 1 FROM content WHERE ContentID = @id AND ContentType = '6');", -1, &stmt, NULL));

	// Append the proper URI scheme to our icon path...
	char book_path[PATH_MAX];
	snprintf(book_path, PATH_MAX, "file://%s", watch_config[watch_idx].filename);

	idx = sqlite3_bind_parameter_index(stmt, "@id");
	CALL_SQLITE(bind_text(stmt, idx, book_path, -1, SQLITE_STATIC));

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		DBGLOG("SELECT SQL query returned: %d", sqlite3_column_int(stmt, 0));
		if (sqlite3_column_int(stmt, 0) == 1)
			is_processed = true;
	}

	sqlite3_finalize(stmt);

	// Now that we know the book exists, we also want to check if the thumbnails do... to avoid getting triggered from the thumbnail creation.
	// NOTE: Again, this assumes FW >= 2.9.0
	if (is_processed) {
		// Assume they haven't been processed until we can confirm it...
		is_processed = false;

		// We'll need the ImageID first...
		CALL_SQLITE(prepare_v2(db, "SELECT ImageID FROM content WHERE ContentID = @id AND ContentType = '6';", -1, &stmt, NULL));

		idx = sqlite3_bind_parameter_index(stmt, "@id");
		CALL_SQLITE(bind_text(stmt, idx, book_path, -1, SQLITE_STATIC));

		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			DBGLOG("SELECT SQL query returned: %s", sqlite3_column_text(stmt, 0));
			const unsigned char *image_id = sqlite3_column_text(stmt, 0);
			size_t len = (size_t)sqlite3_column_bytes(stmt, 0);

			// Then we need the proper hashes Nickel devises...
			// cf. images_path @ https://github.com/kovidgoyal/calibre/blob/master/src/calibre/devices/kobo/driver.py#L2374
			unsigned int hash = qhash(image_id, len);
			unsigned int dir1 = hash & (0xff * 1);
			unsigned int dir2 = (hash & (0xff00 * 1)) >> 8;

			char images_path[PATH_MAX];
			snprintf(images_path, PATH_MAX, "%s/.kobo-images/%d/%d", KFMON_TARGET_MOUNTPOINT, dir1, dir2);
			DBGLOG("Checking for thumbnails in '%s' . . .", images_path);

			// Count the number of processed thumbnails we find...
			int thumbnails_num = 0;

			// Start with the full-size screensaver...
			char ss_path[PATH_MAX];
			snprintf(ss_path, PATH_MAX, "%s/%s - N3_FULL.parsed", images_path, image_id);
			if (access(ss_path, F_OK) == 0) {
				thumbnails_num++;
			} else {
				LOG("Full-size screensaver hasn't been parsed yet!");
			}

			// Then the Homescreen tile...
			// NOTE: This one might be a tad confusing...
			//	 If the icon has never been processed, this will only happen the first time we *close* the PNG's "book"... (i.e., the moment it pops up as the 'last opened' tile).
			//	 And *that* processing triggers a set of OPEN & CLOSE, meaning we can quite possibly run on book *exit* that first time (and only that first time), if database locking permits...
			char tile_path[PATH_MAX];
			snprintf(tile_path, PATH_MAX, "%s/%s - N3_LIBRARY_FULL.parsed", images_path, image_id);
			if (access(tile_path, F_OK) == 0) {
				thumbnails_num++;
			} else {
				LOG("Homescreen tile hasn't been parsed yet!");
			}

			// And finally the Library thumbnail...
			char thumb_path[PATH_MAX];
			snprintf(thumb_path, PATH_MAX, "%s/%s - N3_LIBRARY_GRID.parsed", images_path, image_id);
			if (access(thumb_path, F_OK) == 0) {
				thumbnails_num++;
			} else {
				LOG("Library thumbnail hasn't been parsed yet!");
			}

			// Only give a greenlight if we got all three!
			if (thumbnails_num >= 3)
				is_processed = true;
		}

		sqlite3_finalize(stmt);
	}

	// NOTE: Here be dragons! This works in theory, but risks confusing Nickel's handling of the DB if we do that when nickel is running (which we are).
	// We leave enabling this option to the user's responsibility. KOReader ships with it disabled.
	// The idea is to, optionally, update the Title, Author & Comment fields to make them more useful...
	if (is_processed && update) {
		// Check if the DB has already been updated by checking the title...
		CALL_SQLITE(prepare_v2(db, "SELECT Title FROM content WHERE ContentID = @id AND ContentType = '6';", -1, &stmt, NULL));

		idx = sqlite3_bind_parameter_index(stmt, "@id");
		CALL_SQLITE(bind_text(stmt, idx, book_path, -1, SQLITE_STATIC));

		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			DBGLOG("SELECT SQL query returned: %s", sqlite3_column_text(stmt, 0));
			if (strcmp((const char *)sqlite3_column_text(stmt, 0), watch_config[watch_idx].db_title) != 0)
				needs_update = true;
		}

		sqlite3_finalize(stmt);
	}
	if (needs_update) {
		CALL_SQLITE(prepare_v2(db, "UPDATE content SET Title = @title, Attribution = @author, Description = @comment WHERE ContentID = @id AND ContentType = '6';", -1, &stmt, NULL));

		// NOTE: No sanity checks are done to confirm that those watch configs are sane... The example config ships with a strong warning not to forget them if wanted, but that's it.
		idx = sqlite3_bind_parameter_index(stmt, "@title");
		CALL_SQLITE(bind_text(stmt, idx, watch_config[watch_idx].db_title, -1, SQLITE_STATIC));
		idx = sqlite3_bind_parameter_index(stmt, "@author");
		CALL_SQLITE(bind_text(stmt, idx, watch_config[watch_idx].db_author, -1, SQLITE_STATIC));
		idx = sqlite3_bind_parameter_index(stmt, "@comment");
		CALL_SQLITE(bind_text(stmt, idx, watch_config[watch_idx].db_comment, -1, SQLITE_STATIC));
		idx = sqlite3_bind_parameter_index(stmt, "@id");
		CALL_SQLITE(bind_text(stmt, idx, book_path, -1, SQLITE_STATIC));

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			LOG("UPDATE SQL query failed: %s", sqlite3_errmsg(db));
		} else {
			LOG("Successfully updated DB data for the target PNG");
		}

		sqlite3_finalize(stmt);
	}

	// A rather crappy check to wait for pending COMMITs...
	if (is_processed && wait_for_db) {
		// If there's a rollback journal for the DB, wait for it to go away...
		// NOTE: This assumes the DB was opened with the default journal_mode, DELETE
		int count = 0;
		while (access(KOBO_DB_PATH"-journal", F_OK) == 0) {
			LOG("Found a SQLite rollback journal, waiting for it to go away (iteration nr. %d) . . .", count++);
			usleep(250 * 1000);
			// NOTE: Don't wait more than 7.5s
			if (count > 30) {
				LOG("Waited for the SQLite rollback journal to go away for far too long, going on anyway.");
				break;
			}
		}
	}

	sqlite3_close(db);

	return is_processed;
}

/* Spawn a process and return its pid...
 * Massively inspired from popen2() implementations from https://stackoverflow.com/questions/548063
 * Except that getting that pid is all I care about, so forget about the popen-like piping ;). */
static pid_t spawn(char **command)
{
	pid_t pid;

	pid = fork();

	if (pid < 0)
		return pid;
	else if (pid == 0) {
		// Sweet child o' mine!
		execvp(*command, command);
		perror("[KFMon] execvp");
		exit(EXIT_FAILURE);
	}
	// We have a shiny SIGCHLD handler to reap this process when it dies, so we're done here.

	return pid;
}

// Read all available inotify events from the file descriptor 'fd'.
static bool handle_events(int fd)
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
	bool destroyed_wd = false;
	static bool pending_processing = false;

	// Loop while events can be read from inotify file descriptor.
	for (;;) {
		// Read some events.
		len = read(fd, buf, sizeof buf);
		if (len == -1 && errno != EAGAIN) {
			perror("[KFMon] read");
			exit(EXIT_FAILURE);
		}

		/* If the nonblocking read() found no events to read, then
		   it returns -1 with errno set to EAGAIN. In that case,
		   we exit the loop. */
		if (len <= 0)
			break;

		// Loop over all events in the buffer
		for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
			// NOTE: This trips -Wcast-align on ARM, but it works, and saves us some code ;).
			event = (const struct inotify_event *) ptr;

			// Identify which of our target file we've caught an event for...
			unsigned int watch_idx = 0;
			bool found_watch_idx = false;
			for (watch_idx = 0; watch_idx < watch_count; watch_idx++) {
				if (watch_config[watch_idx].inotify_wd == event->wd) {
					found_watch_idx = true;
					break;
				}
			}
			if (!found_watch_idx) {
				// NOTE: Err, that should (hopefully) never happen!
				LOG("!! Failed to match the current inotify event to any of our watched file! !!");
			}

			// Print event type
			if (event->mask & IN_OPEN) {
				LOG("Tripped IN_OPEN for %s", watch_config[watch_idx].filename);
				// Clunky detection of potential Nickel processing...
				if (watch_config[watch_idx].last_spawned_pid == 0) {
					// Only check if we're ready to spawn something...
					if (!is_target_processed(watch_idx, false)) {
						// It's not processed on OPEN, flag as pending...
						pending_processing = true;
						LOG("Flagged target icon '%s' as pending processing ...", watch_config[watch_idx].filename);
					} else {
						// It's already processed, we're good!
						pending_processing = false;
					}
				}
			}
			if (event->mask & IN_CLOSE) {
				LOG("Tripped IN_CLOSE for %s", watch_config[watch_idx].filename);
				if (watch_config[watch_idx].last_spawned_pid == 0) {
					// Check that our target file has already fully been processed by Nickel before launching anything...
					if (!pending_processing && is_target_processed(watch_idx, true)) {
						LOG("Spawning %s . . .", watch_config[watch_idx].action);
						// We're using execvp()...
						char *cmd[] = {watch_config[watch_idx].action, NULL};
						// NOTE: Block our SIGCHLD handler until execvp() actually returns, to make sure it'll have an up-to-date last_spawned_pid
						//	 Avoids races if execvp() returns really fast, which is not that uncommon for simple shell scripts.
						sigset_t sigset;
						sigemptyset (&sigset);
						sigaddset(&sigset, SIGCHLD);
						if (sigprocmask(SIG_BLOCK, &sigset, NULL) == -1)
							perror("[KFMon] sigprocmask (BLOCK)");
						watch_config[watch_idx].last_spawned_pid = spawn(cmd);
						if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) == -1)
							perror("[KFMon] sigprocmask (UNBLOCK)");
						// NOTE: For actions returning very quickly, the PID logged *may* be stale since the SIGCHLD handler might actually have done its job *before* us...
						//	 I prefer keeping this *out* of the critical section to avoid race & deadlock issues with the signal handler...
						LOG(". . . with pid: %d", watch_config[watch_idx].last_spawned_pid);
					} else {
						LOG("Target icon '%s' might not have been fully processed by Nickel yet, don't launch anything.", watch_config[watch_idx].filename);
						// NOTE: That, or we hit a SQLITE_BUSY timeout on OPEN, which tripped our 'pending processing' check.
					}
				} else {
					LOG("Our last spawn (%d) is still alive!", watch_config[watch_idx].last_spawned_pid);
				}
			}
			if (event->mask & IN_UNMOUNT)
				LOG("Tripped IN_UNMOUNT for %s", watch_config[watch_idx].filename);
			if (event->mask & IN_IGNORED) {
				LOG("Tripped IN_IGNORED for %s", watch_config[watch_idx].filename);
				// Remember that the watch was automatically destroyed so we can break from the loop...
				destroyed_wd = true;
			}
			if (event->mask & IN_Q_OVERFLOW) {
				if (event->len) {
					LOG("Huh oh... Tripped IN_Q_OVERFLOW for %s", event->name);
				} else {
					LOG("Huh oh... Tripped IN_Q_OVERFLOW");
				}
				// Destroy our only watch, and break the loop
				if (inotify_rm_watch(fd, watch_config[watch_idx].inotify_wd) == -1)
				{
					// That's too bad, but may not be fatal, so warn only...
					perror("[KFMon] inotify_rm_watch");
				}
				destroyed_wd = true;
			}
		}

		// If we caught an event indicating that the watch was automatically destroyed, break the loop.
		if (destroyed_wd)
			break;
	}

	// And we have another outer loop to break, so pass that on...
	return destroyed_wd;
}

// Handle SIGCHLD to reap processes and update our last_spawned_pid tracker ASAP
void reaper(int sig  __attribute__ ((unused))) {
	pid_t cpid;
	int wstatus;
	int saved_errno = errno;
	ssize_t foo __attribute__((unused));
	while ((cpid = waitpid((pid_t)(-1), &wstatus, WNOHANG)) > 0) {
		// Identify which of our target actions we've reaped a process from...
		unsigned int watch_idx = 0;
		bool found_watch_idx = false;
		for (watch_idx = 0; watch_idx < watch_count; watch_idx++) {
			if (watch_config[watch_idx].last_spawned_pid == cpid) {
				found_watch_idx = true;
				break;
			}
		}
		if (!found_watch_idx) {
			// NOTE: Err, that should (hopefully) never happen!
			foo = write(STDERR_FILENO, "!! Failed to match the child pid reaped to any of our tracked spawns! !!\n", 73);
		}
		// NOTE: We shouldn't ever lose track of a spawn pid, but we can't safely log what happened (fprintf & co not async-safe), so just mention *something* was reaped...
		foo = write(STDERR_FILENO, "Reaped a spawned pid.\n", 22);
		if (WIFEXITED(wstatus)) {
			foo = write(STDERR_FILENO, "It exited.\n", 11);
		} else if (WIFSIGNALED(wstatus)) {
			foo = write(STDERR_FILENO, "It was killed by a signal.\n", 27);
		}
		// Reset our pid tracker to announce that we're ready to spawn something new
		watch_config[watch_idx].last_spawned_pid = 0;
	}
	errno = saved_errno;
}

int main(int argc __attribute__ ((unused)), char* argv[] __attribute__ ((unused)))
{
	int fd, poll_num;
	struct pollfd pfd;

	// Being launched via udev leaves us with a negative nice value, fix that.
	if (nice(2) == -1) {
		perror("[KFMon] nice");
		exit(EXIT_FAILURE);
	}

	// Fly, little daemon!
	if (daemonize() != 0) {
		fprintf(stderr, "Failed to daemonize!\n");
		exit(EXIT_FAILURE);
	}

	// Say hello :)
	LOG("Initializing KFMon %s | Built on %s @ %s", KFMON_VERSION,  __DATE__, __TIME__);

	// Keep track of the reaping of our children
	struct sigaction sa;
	sa.sa_handler = &reaper;
	sigemptyset(&sa.sa_mask);
	// We don't care about SIGSTOP & SIGCONT
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	if (sigaction(SIGCHLD, &sa, 0) == -1) {
		perror("[KFMon] sigaction");
		exit(EXIT_FAILURE);
	}

	// Load our configs
	if (load_config() == -1) {
		LOG("Failed to load one or more config files!");
		exit(EXIT_FAILURE);
	}

	// Squish stderr if we want to log to the syslog... (can't do that w/ the rest in daemonize, since we don't have our config yet at that point)
	if (daemon_config.use_syslog) {
		// Redirect stderr (which is now actually our log file) to /dev/null
		if ((fd = open("/dev/null", O_RDWR)) != -1) {
			dup2(fd, fileno(stderr));
			if (fd > 2)
				close (fd);
		} else {
			fprintf(stderr, "Failed to redirect stderr to /dev/null\n");
			return -1;
		}

		// And connect to the system logger...
		openlog("kfmon", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON);
	}


	// We pretty much want to loop forever...
	while (1) {
		LOG("Beginning the main loop.");

		// Create the file descriptor for accessing the inotify API
		LOG("Initializing inotify.");
		fd = inotify_init1(IN_NONBLOCK);
		if (fd == -1) {
			perror("[KFMon] inotify_init1");
			exit(EXIT_FAILURE);
		}

		// Make sure our target file is available (i.e., the partition it resides in is mounted)
		if (!is_target_mounted()) {
			LOG("%s isn't mounted, waiting for it to be . . .", KFMON_TARGET_MOUNTPOINT);
			// If it's not, wait for it to be...
			wait_for_target_mountpoint();
		}

		// Flag each of our target files for 'file was opened' and 'file was closed' events
		for (unsigned int watch_idx = 0; watch_idx < watch_count; watch_idx++) {
			watch_config[watch_idx].inotify_wd = inotify_add_watch(fd, watch_config[watch_idx].filename, IN_OPEN | IN_CLOSE);
			if (watch_config[watch_idx].inotify_wd == -1) {
				LOG("Cannot watch '%s'! Giving up.", watch_config[watch_idx].filename);
				perror("[KFMon] inotify_add_watch");
				exit(EXIT_FAILURE);
				// NOTE: This effectively means we exit when any one of our target file cannot be found, which is not a bad thing, per se...
				//	 This basically means that it takes some kind of effort to actually be running during Nickel's processing of said target file ;).
			}
			LOG("Setup an inotify watch for '%s' @ index %d.", watch_config[watch_idx].filename, watch_idx);
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
				perror("[KFMon] poll");
				exit(EXIT_FAILURE);
			}

			if (poll_num > 0) {
				if (pfd.revents & POLLIN) {
					// Inotify events are available
					if (handle_events(fd))
						// Go back to the main loop if we exited early (because the watch was destroyed automatically after an unmount or an unlink, for instance)
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
