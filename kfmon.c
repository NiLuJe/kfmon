/*
	KFMon: Kobo inotify-based launcher
	Copyright (C) 2016-2020 NiLuJe <ninuje@gmail.com>
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

#include "kfmon.h"

// Because daemon() only appeared in glibc 2.21 (and doesn't double-fork anyway)
static int
    daemonize(void)
{
	switch (fork()) {
		case -1:
			PFLOG(LOG_CRIT, "initial fork: %m");
			return -1;
		case 0:
			break;
		default:
			_exit(EXIT_SUCCESS);
	}

	if (setsid() == -1) {
		PFLOG(LOG_CRIT, "setsid: %m");
		return -1;
	}

	// Double fork, for... reasons!
	// In practical terms, this ensures we get re-parented to init *now*.
	// Ignore SIGHUP while we're there, since we don't want to be killed by it.
	struct sigaction sa = { .sa_handler = SIG_IGN, .sa_flags = SA_RESTART };
	if (sigaction(SIGHUP, &sa, NULL) == -1) {
		PFLOG(LOG_CRIT, "sigaction: %m");
		return -1;
	}
	switch (fork()) {
		case -1:
			PFLOG(LOG_CRIT, "final fork: %m");
			return -1;
		case 0:
			break;
		default:
			_exit(EXIT_SUCCESS);
	}

	if (chdir("/") == -1) {
		PFLOG(LOG_CRIT, "chdir: %m");
		return -1;
	}

	// Make sure we keep honoring rcS's umask
	umask(022);    // Flawfinder: ignore

	// Store a copy of stdin, stdout & stderr so we can restore it to our children later on...
	// NOTE: Hence the + 3 in the two (three w/ use_syslog) following fd tests.
	origStdin  = dup(fileno(stdin));
	origStdout = dup(fileno(stdout));
	origStderr = dup(fileno(stderr));

	// Redirect stdin & stdout to /dev/null
	int fd = -1;
	if ((fd = open("/dev/null", O_RDWR)) != -1) {
		dup2(fd, fileno(stdin));
		dup2(fd, fileno(stdout));
		if (fd > 2 + 3) {
			close(fd);
		}
	} else {
		PFLOG(LOG_CRIT, "Failed to redirect stdin & stdout to /dev/null (open: %m)");
		return -1;
	}

	// Redirect stderr to our logfile
	// NOTE: We do need O_APPEND (as opposed to simply calling lseek(fd, 0, SEEK_END) after open),
	//       because auxiliary scripts *may* also append to this log file ;).
	int flags = O_WRONLY | O_CREAT | O_APPEND;
	// Check if we need to truncate our log because it has grown too much...
	struct stat st;
	if ((stat(KFMON_LOGFILE, &st) == 0) && (S_ISREG(st.st_mode))) {
		// Truncate if > 1MB
		if (st.st_size > 1 * 1024 * 1024) {
			flags |= O_TRUNC;
		}
	}
	if ((fd = open(KFMON_LOGFILE, flags, S_IRUSR | S_IWUSR)) != -1) {
		dup2(fd, fileno(stderr));
		if (fd > 2 + 3) {
			close(fd);
		}
	} else {
		PFLOG(LOG_CRIT, "Failed to redirect stderr to logfile '%s' (open: %m)", KFMON_LOGFILE);
		return -1;
	}

	return 0;
}

// Wrapper around localtime_r, making sure this part is thread-safe (used for logging)
static struct tm*
    get_localtime(struct tm* restrict lt)
{
	time_t t = time(NULL);
	tzset();

	return localtime_r(&t, lt);
}

// Wrapper around strftime, making sure this part is thread-safe (used for logging)
static char*
    format_localtime(struct tm* restrict lt, char* restrict sz_time, size_t len)
{
	// c.f., strftime(3) & https://stackoverflow.com/questions/7411301
	strftime(sz_time, len, "%Y-%m-%d @ %H:%M:%S", lt);

	return sz_time;
}

// Return the current time formatted as 2016-04-29 @ 20:44:13 (used for logging)
// NOTE: The use of static variables prevents this from being thread-safe,
//       but in the main thread, we use static storage for simplicity's sake.
static char*
    get_current_time(void)
{
	static struct tm    local_tm = { 0 };
	struct tm* restrict lt       = get_localtime(&local_tm);

	static char sz_time[22];

	return format_localtime(lt, sz_time, sizeof(sz_time));
}

// And now the same, but with user supplied storage, thus potentially thread-safe:
// e.g., we use the stack in reaper_thread().
static char*
    get_current_time_r(struct tm* restrict local_tm, char* restrict sz_time, size_t len)
{
	struct tm* restrict lt = get_localtime(local_tm);
	return format_localtime(lt, sz_time, len);
}

static const char*
    get_log_prefix(int prio)
{
	// Reuse (part of) the syslog() priority constants
	switch (prio) {
		case LOG_CRIT:
			return "CRIT";
		case LOG_ERR:
			return "ERR!";
		case LOG_WARNING:
			return "WARN";
		case LOG_NOTICE:
			return "NOTE";
		case LOG_INFO:
			return "INFO";
		case LOG_DEBUG:
			return "DBG!";
		default:
			return "OOPS";
	}
}

// Check that our target mountpoint is indeed mounted...
static bool
    is_target_mounted(void)
{
	// c.f., http://program-nix.blogspot.com/2008/08/c-language-check-filesystem-is-mounted.html
	FILE* restrict          mtab       = NULL;
	struct mntent* restrict part       = NULL;
	bool                    is_mounted = false;

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
static void
    wait_for_target_mountpoint(void)
{
	// c.f., https://stackoverflow.com/questions/5070801
	int           mfd   = open("/proc/mounts", O_RDONLY | O_CLOEXEC);
	struct pollfd pfd   = { 0 };
	pfd.fd              = mfd;
	pfd.events          = POLLERR | POLLPRI;
	pfd.revents         = 0;
	uint8_t changes     = 0U;
	uint8_t max_changes = 6U;

	while (poll(&pfd, 1, -1) >= 0) {
		if (pfd.revents & POLLERR) {
			LOG(LOG_INFO, "Mountpoints changed (iteration nr. %d of %hhu)", ++changes, max_changes);

			// Stop polling once we know our mountpoint is available...
			if (is_target_mounted()) {
				LOG(LOG_NOTICE, "Yay! Target mountpoint is available!");
				break;
			}
		}
		pfd.revents = 0;

		// If we can't find our mountpoint after that many changes, assume we're screwed...
		if (changes >= max_changes) {
			LOG(LOG_ERR, "Too many mountpoint changes without finding our target (shutdown?), aborting!");
			close(mfd);
			// NOTE: We have to hide this behind a slightly crappy check, because this runs during load_config,
			//       at which point FBInk is not yet initialized...
			if (fbinkConfig.row != 0) {
				fbink_print(FBFD_AUTO, "[KFMon] Internal storage unavailable, bye!", &fbinkConfig);
			}
			exit(EXIT_FAILURE);
		}
	}

	close(mfd);
}

// Sanitize user input for keys expecting an unsigned short integer
// NOTE: Inspired from git's strtoul_ui @ git-compat-util.h
static int
    strtoul_hu(const char* str, unsigned short int* restrict result)
{
	// NOTE: We want to *reject* negative values (which strtoul does not)!
	if (strchr(str, '-')) {
		LOG(LOG_WARNING, "Assigned a negative value (%s) to a key expecting an unsigned short int.", str);
		return -EINVAL;
	}

	// Now that we know it's positive, we can go on with strtoul...
	char* endptr;
	errno                 = 0;    // To distinguish success/failure after call
	unsigned long int val = strtoul(str, &endptr, 10);

	if ((errno == ERANGE && val == ULONG_MAX) || (errno != 0 && val == 0)) {
		PFLOG(LOG_WARNING, "strtoul: %m");
		return -EINVAL;
	}

	// NOTE: It fact, always clamp to SHRT_MAX, since some of these may end up cast to an int (e.g., db_timeout)
	if (val > SHRT_MAX) {
		LOG(LOG_WARNING,
		    "Encountered a value larger than SHRT_MAX assigned to a key, clamping it down to SHRT_MAX");
		val = SHRT_MAX;
	}

	if (endptr == str) {
		LOG(LOG_WARNING,
		    "No digits were found in value '%s' assigned to a key expecting an unsigned short int.",
		    str);
		return -EINVAL;
	}

	// If we got here, strtoul() successfully parsed at least part of a number.
	// But we do want to enforce the fact that the input really was *only* an integer value.
	if (*endptr != '\0') {
		LOG(LOG_WARNING,
		    "Found trailing characters (%s) behind value '%lu' assigned from string '%s' to a key expecting an unsigned short int.",
		    endptr,
		    val,
		    str);
		return -EINVAL;
	}

	// Make sure there isn't a loss of precision on this arch when casting explicitly
	if ((unsigned short int) val != val) {
		LOG(LOG_WARNING, "Loss of precision when casting value '%lu' to an unsigned short int.", val);
		return -EINVAL;
	}

	*result = (unsigned short int) val;
	return EXIT_SUCCESS;
}

// Sanitize user input for keys expecting a boolean
// NOTE: Inspired from Linux's strtobool (tools/lib/string.c) as well as sudo's implementation of the same.
static int
    strtobool(const char* restrict str, bool* restrict result)
{
	if (!str) {
		LOG(LOG_WARNING, "Passed an empty value to a key expecting a boolean.");
		return -EINVAL;
	}

	switch (str[0]) {
		case 't':
		case 'T':
			if (strcasecmp(str, "true") == 0) {
				*result = true;
				return EXIT_SUCCESS;
			}
			break;
		case 'y':
		case 'Y':
			if (strcasecmp(str, "yes") == 0) {
				*result = true;
				return EXIT_SUCCESS;
			}
			break;
		case '1':
			if (str[1] == '\0') {
				*result = true;
				return EXIT_SUCCESS;
			}
			break;
		case 'f':
		case 'F':
			if (strcasecmp(str, "false") == 0) {
				*result = false;
				return EXIT_SUCCESS;
			}
			break;
		case 'n':
		case 'N':
			switch (str[1]) {
				case 'o':
				case 'O':
					if (str[2] == '\0') {
						*result = false;
						return EXIT_SUCCESS;
					}
					break;
				default:
					break;
			}
			break;
		case '0':
			if (str[1] == '\0') {
				*result = false;
				return EXIT_SUCCESS;
			}
			break;
		case 'o':
		case 'O':
			switch (str[1]) {
				case 'n':
				case 'N':
					if (str[2] == '\0') {
						*result = true;
						return EXIT_SUCCESS;
					}
					break;
				case 'f':
				case 'F':
					switch (str[2]) {
						case 'f':
						case 'F':
							if (str[3] == '\0') {
								*result = false;
								return EXIT_SUCCESS;
							}
							break;
						default:
							break;
					}
					break;
				default:
					break;
			}
			break;
		default:
			// NOTE: *result is zero-initialized, no need to explicitly set it to false
			break;
	}

	LOG(LOG_WARNING, "Assigned an invalid or malformed value (%s) to a key expecting a boolean.", str);
	return -EINVAL;
}

// Handle parsing the main KFMon config
static int
    daemon_handler(void* user, const char* restrict section, const char* restrict key, const char* restrict value)
{
	DaemonConfig* restrict pconfig = (DaemonConfig*) user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(key, n) == 0
	if (MATCH("daemon", "db_timeout")) {
		if (strtoul_hu(value, &pconfig->db_timeout) < 0) {
			LOG(LOG_CRIT, "Passed an invalid value for db_timeout!");
			return 0;
		}
	} else if (MATCH("daemon", "use_syslog")) {
		if (strtobool(value, &pconfig->use_syslog) < 0) {
			LOG(LOG_CRIT, "Passed an invalid value for use_syslog!");
			return 0;
		}
	} else if (MATCH("daemon", "with_notifications")) {
		if (strtobool(value, &pconfig->with_notifications) < 0) {
			LOG(LOG_CRIT, "Passed an invalid value for with_notifications!");
			return 0;
		}
	} else {
		return 0;    // unknown section/name, error
	}
	return 1;
}

// Handle parsing a watch config
static int
    watch_handler(void* user, const char* restrict section, const char* restrict key, const char* restrict value)
{
	WatchConfig* restrict pconfig = (WatchConfig*) user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(key, n) == 0
	if (MATCH("watch", "filename")) {
		if (str5cpy(pconfig->filename, CFG_SZ_MAX, value, CFG_SZ_MAX, NOTRUNC) < 0) {
			LOG(LOG_CRIT, "Passed an invalid value for filename (too long?)!");
			return 0;
		}
	} else if (MATCH("watch", "action")) {
		if (str5cpy(pconfig->action, CFG_SZ_MAX, value, CFG_SZ_MAX, NOTRUNC) < 0) {
			LOG(LOG_CRIT, "Passed an invalid value for action (too long?)!");
			return 0;
		}
	} else if (MATCH("watch", "label")) {
		if (str5cpy(pconfig->label, CFG_SZ_MAX, value, CFG_SZ_MAX, TRUNC) < 0) {
			LOG(LOG_WARNING, "The value passed for label may have been truncated!");
		}
	} else if (MATCH("watch", "hidden")) {
		if (strtobool(value, &pconfig->hidden) < 0) {
			LOG(LOG_CRIT, "Passed an invalid value for hidden!");
			return 0;
		}
	} else if (MATCH("watch", "block_spawns")) {
		if (strtobool(value, &pconfig->block_spawns) < 0) {
			LOG(LOG_CRIT, "Passed an invalid value for block_spawns!");
			return 0;
		}
	} else if (MATCH("watch", "skip_db_checks")) {
		if (strtobool(value, &pconfig->skip_db_checks) < 0) {
			LOG(LOG_CRIT, "Passed an invalid value for skip_db_checks!");
			return 0;
		}
	} else if (MATCH("watch", "do_db_update")) {
		if (strtobool(value, &pconfig->do_db_update) < 0) {
			LOG(LOG_CRIT, "Passed an invalid value for do_db_update!");
			return 0;
		}
	} else if (MATCH("watch", "db_title")) {
		// NOTE: str5cpy returns OKTRUNC (1) if we allow truncation, which we do here
		if (str5cpy(pconfig->db_title, DB_SZ_MAX, value, DB_SZ_MAX, TRUNC) != 0) {
			LOG(LOG_WARNING, "The value passed for db_title may have been truncated!");
		}
	} else if (MATCH("watch", "db_author")) {
		if (str5cpy(pconfig->db_author, DB_SZ_MAX, value, DB_SZ_MAX, TRUNC) != 0) {
			LOG(LOG_WARNING, "The value passed for db_author may have been truncated!");
		}
	} else if (MATCH("watch", "db_comment")) {
		if (str5cpy(pconfig->db_comment, DB_SZ_MAX, value, DB_SZ_MAX, TRUNC) != 0) {
			LOG(LOG_WARNING, "The value passed for db_comment may have been truncated!");
		}
	} else if (MATCH("watch", "reboot_on_exit")) {
		;
	} else {
		return 0;    // unknown section/name, error
	}
	return 1;
}

// Validate a watch config
static bool
    validate_watch_config(void* user)
{
	WatchConfig* restrict pconfig = (WatchConfig*) user;

	bool sane = true;

	if (pconfig->filename[0] == '\0') {
		LOG(LOG_CRIT, "Mandatory key 'filename' is missing or blank!");
		sane = false;
	} else {
		// Make sure we're not trying to set multiple watches on the same file...
		// (because that would only actually register the first one parsed).
		uint8_t matches  = 0U;
		uint8_t bmatches = 0U;
		for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
			// Only relevant for active watches
			if (!watchConfig[watch_idx].is_active) {
				continue;
			}

			if (strcmp(pconfig->filename, watchConfig[watch_idx].filename) == 0) {
				matches++;
			}

			// Check the basename, too, for IPC...
			if (strcmp(basename(pconfig->filename), basename(watchConfig[watch_idx].filename)) == 0) {
				bmatches++;
			}
		}
		// As we're not yet flagged active, we won't loop over ourselves ;).
		if (matches >= 1U) {
			LOG(LOG_WARNING, "Tried to setup multiple watches on file '%s'!", pconfig->filename);
			sane = false;
		}
		if (bmatches >= 1U) {
			LOG(LOG_WARNING,
			    "Tried to setup multiple watches on files with an identical basename: '%s'!",
			    basename(pconfig->filename));
			sane = false;
		}
	}
	if (pconfig->action[0] == '\0') {
		LOG(LOG_CRIT, "Mandatory key 'action' is missing or blank!");
		sane = false;
	}

	// Don't warn about a missing/blank 'label', it's optional.

	// If we asked for a database update, the next three keys become mandatory
	if (pconfig->do_db_update) {
		if (pconfig->db_title[0] == '\0') {
			LOG(LOG_CRIT, "Mandatory key 'db_title' is missing or blank!");
			sane = false;
		}
		if (pconfig->db_author[0] == '\0') {
			LOG(LOG_CRIT, "Mandatory key 'db_author' is missing or blank!");
			sane = false;
		}
		if (pconfig->db_comment[0] == '\0') {
			LOG(LOG_CRIT, "Mandatory key 'db_comment' is missing or blank!");
			sane = false;
		}
	}

	return sane;
}

// Validate a watch config, and merge it to its final location if it's sane and updated
static bool
    validate_and_merge_watch_config(void* user, uint8_t target_idx, bool* was_updated)
{
	WatchConfig* restrict pconfig = (WatchConfig*) user;

	bool sane    = true;
	bool updated = false;

	if (pconfig->filename[0] == '\0') {
		LOG(LOG_CRIT, "Mandatory key 'filename' is missing or blank!");
		sane = false;
	} else {
		// Did it change?
		if (strcmp(pconfig->filename, watchConfig[target_idx].filename) != 0) {
			// Make sure we're not trying to set multiple watches on the same file...
			// (because that would only actually register the first one parsed).
			uint8_t matches  = 0U;
			uint8_t bmatches = 0U;
			for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
				// Only relevant for active watches
				if (!watchConfig[watch_idx].is_active) {
					continue;
				}

				// Skip the to-be-updated watch, since we'll overwrite it if this check pans out...
				if (watch_idx == target_idx) {
					continue;
				}

				if (strcmp(pconfig->filename, watchConfig[watch_idx].filename) == 0) {
					matches++;
				}

				// Check basename, too, for IPC...
				if (strcmp(basename(pconfig->filename), basename(watchConfig[watch_idx].filename)) == 0) {
					bmatches++;
				}
			}
			// We explicitly make sure not to loop over ourselves ;).
			if (matches >= 1U) {
				LOG(LOG_WARNING, "Tried to setup multiple watches on file '%s'!", pconfig->filename);
				sane = false;
			}
			if (bmatches >= 1U) {
				LOG(LOG_WARNING,
				    "Tried to setup multiple watches on files with an identical basename: '%s'!",
				    basename(pconfig->filename));
				sane = false;
			}
			if (sane) {
				// Filename changed, and it was updated to something sane, update our target watch!
				// NOTE: Forgo error checking, as this has already gone through an input validation pass.
				str5cpy(
				    watchConfig[target_idx].filename, CFG_SZ_MAX, pconfig->filename, CFG_SZ_MAX, NOTRUNC);
				updated = true;
				LOG(LOG_NOTICE,
				    "Updated filename to '%s' for watch config @ index %hhu",
				    watchConfig[target_idx].filename,
				    target_idx);
			}
		}
	}
	if (pconfig->action[0] == '\0') {
		LOG(LOG_CRIT, "Mandatory key 'action' is missing or blank!");
		sane = false;
	} else {
		if (strcmp(pconfig->action, watchConfig[target_idx].action) != 0) {
			str5cpy(watchConfig[target_idx].action, CFG_SZ_MAX, pconfig->action, CFG_SZ_MAX, NOTRUNC);
			updated = true;
			LOG(LOG_NOTICE,
			    "Updated action to '%s' for watch config @ index %hhu",
			    watchConfig[target_idx].action,
			    target_idx);
		}
	}

	// Check if label was updated...
	if (strcmp(pconfig->label, watchConfig[target_idx].label) != 0) {
		str5cpy(watchConfig[target_idx].label, CFG_SZ_MAX, pconfig->label, CFG_SZ_MAX, TRUNC);
		updated = true;
		LOG(LOG_NOTICE,
		    "Updated label to '%s' for watch config @ index %hhu",
		    watchConfig[target_idx].label,
		    target_idx);
	}

	// Check if hidden was updated...
	if (pconfig->hidden != watchConfig[target_idx].hidden) {
		watchConfig[target_idx].hidden = pconfig->hidden;
		updated                        = true;
		LOG(LOG_NOTICE,
		    "Updated hidden to %d for watch config @ index %hhu",
		    watchConfig[target_idx].hidden,
		    target_idx);
	}

	// Check if block_spawns was updated...
	if (pconfig->block_spawns != watchConfig[target_idx].block_spawns) {
		watchConfig[target_idx].block_spawns = pconfig->block_spawns;
		updated                              = true;
		LOG(LOG_NOTICE,
		    "Updated block_spawns to %d for watch config @ index %hhu",
		    watchConfig[target_idx].block_spawns,
		    target_idx);
	}

	// Check if skip_db_checks was updated...
	if (pconfig->skip_db_checks != watchConfig[target_idx].skip_db_checks) {
		watchConfig[target_idx].skip_db_checks = pconfig->skip_db_checks;
		updated                                = true;
		LOG(LOG_NOTICE,
		    "Updated skip_db_checks to %d for watch config @ index %hhu",
		    watchConfig[target_idx].skip_db_checks,
		    target_idx);
	}

	// Check if do_db_update was updated...
	if (pconfig->do_db_update != watchConfig[target_idx].do_db_update) {
		watchConfig[target_idx].do_db_update = pconfig->do_db_update;
		updated                              = true;
		LOG(LOG_NOTICE,
		    "Updated do_db_update to %d for watch config @ index %hhu",
		    watchConfig[target_idx].do_db_update,
		    target_idx);
	}

	// If we asked for a database update, the next three keys become mandatory
	if (pconfig->do_db_update) {
		if (pconfig->db_title[0] == '\0') {
			LOG(LOG_CRIT, "Mandatory key 'db_title' is missing or blank!");
			sane = false;
		} else {
			if (strcmp(pconfig->db_title, watchConfig[target_idx].db_title) != 0) {
				str5cpy(watchConfig[target_idx].db_title, DB_SZ_MAX, pconfig->db_title, DB_SZ_MAX, TRUNC);
				updated = true;
				LOG(LOG_NOTICE,
				    "Updated db_title to '%s' for watch config @ index %hhu",
				    watchConfig[target_idx].db_title,
				    target_idx);
			}
		}
		if (pconfig->db_author[0] == '\0') {
			LOG(LOG_CRIT, "Mandatory key 'db_author' is missing or blank!");
			sane = false;
		} else {
			if (strcmp(pconfig->db_author, watchConfig[target_idx].db_author) != 0) {
				str5cpy(
				    watchConfig[target_idx].db_author, DB_SZ_MAX, pconfig->db_author, DB_SZ_MAX, TRUNC);
				updated = true;
				LOG(LOG_NOTICE,
				    "Updated db_author to '%s' for watch config @ index %hhu",
				    watchConfig[target_idx].db_author,
				    target_idx);
			}
		}
		if (pconfig->db_comment[0] == '\0') {
			LOG(LOG_CRIT, "Mandatory key 'db_comment' is missing or blank!");
			sane = false;
		} else {
			if (strcmp(pconfig->db_comment, watchConfig[target_idx].db_comment) != 0) {
				str5cpy(
				    watchConfig[target_idx].db_comment, DB_SZ_MAX, pconfig->db_comment, DB_SZ_MAX, TRUNC);
				updated = true;
				LOG(LOG_NOTICE,
				    "Updated db_comment to '%s' for watch config @ index %hhu",
				    watchConfig[target_idx].db_comment,
				    target_idx);
			}
		}
	}

	if (sane && updated) {
		fbink_printf(FBFD_AUTO,
			     NULL,
			     &fbinkConfig,
			     "[KFMon] Updated the watch on %s",
			     basename(watchConfig[target_idx].filename));
		// Notify the caller
		*was_updated = true;
	}

	return sane;
}

// Returns the index of the first usable entry in the watch list
static int8_t
    get_next_available_watch_entry(void)
{
	for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
		if (!watchConfig[watch_idx].is_active) {
			return (int8_t) watch_idx;
		}
	}
	return -1;
}

// Mimic scandir's alphasort
static int
    fts_alphasort(const FTSENT** a, const FTSENT** b)
{
	// NOTE: alphasort actually uses strcoll now, but this is Kobo, locales are broken anyway, so, strcmp is The Way.
	//	 Or strverscmp is we wanted natural sorting, which we don't really need here ;).
	return strcmp((*a)->fts_name, (*b)->fts_name);
}

// Load our config files...
static int
    load_config(void)
{
	// Our config files live in the target mountpoint...
	if (!is_target_mounted()) {
		LOG(LOG_NOTICE, "%s isn't mounted, waiting for it to be . . .", KFMON_TARGET_MOUNTPOINT);
		// If it's not, wait for it to be...
		wait_for_target_mountpoint();
	}

	// Walk the config directory to pickup our ini files... (c.f.,
	// https://keramida.wordpress.com/2009/07/05/fts3-or-avoiding-to-reinvent-the-wheel/)
	// We only need to walk a single directory...
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"
	char* const cfg_path[] = { KFMON_CONFIGPATH, NULL };
#pragma GCC diagnostic pop

	// Don't chdir (because that mountpoint can go buh-bye), and don't stat (because we don't need to).
	FTS* restrict ftsp;
	if ((ftsp = fts_open(
		 cfg_path, FTS_COMFOLLOW | FTS_LOGICAL | FTS_NOCHDIR | FTS_NOSTAT | FTS_XDEV, &fts_alphasort)) == NULL) {
		PFLOG(LOG_CRIT, "fts_open: %m");
		return -1;
	}
	// Initialize ftsp with as many toplevel entries as possible.
	FTSENT* restrict chp;
	chp = fts_children(ftsp, 0);
	if (chp == NULL) {
		// No files to traverse!
		LOG(LOG_CRIT, "Config directory '%s' appears to be empty, aborting!", KFMON_CONFIGPATH);
		fts_close(ftsp);
		return -1;
	}

	// Until something goes wrong...
	int rval = EXIT_SUCCESS;
	// Keep track of how many watches we've set up
	uint8_t watch_count = 0U;

	FTSENT* restrict p;
	while ((p = fts_read(ftsp)) != NULL) {
		switch (p->fts_info) {
			case FTS_F:
				// Check if it's a .ini and not either an unix hidden file or a Mac resource fork...
				if (p->fts_namelen > 4 &&
				    strncasecmp(p->fts_name + (p->fts_namelen - 4), ".ini", 4) == 0 &&
				    strncasecmp(p->fts_name, ".", 1) != 0) {
					LOG(LOG_INFO, "Trying to load config file '%s' . . .", p->fts_path);
					// The main config has to be parsed slightly differently...
					if (strcasecmp(p->fts_name, "kfmon.ini") == 0) {
						// NOTE: Can technically return -1 on file open error,
						//       but that shouldn't really ever happen
						//       given the nature of the loop we're in ;).
						int ret = ini_parse(p->fts_path, daemon_handler, &daemonConfig);
						if (ret != 0) {
							LOG(LOG_CRIT,
							    "Failed to parse main config file '%s' (first error on line %d), will abort!",
							    p->fts_name,
							    ret);
							// Flag as a failure...
							rval = -1;
						} else {
							LOG(LOG_NOTICE,
							    "Daemon config loaded from '%s': db_timeout=%hu, use_syslog=%d, with_notifications=%d",
							    p->fts_name,
							    daemonConfig.db_timeout,
							    daemonConfig.use_syslog,
							    daemonConfig.with_notifications);
						}
					} else if (strcasecmp(p->fts_name, "kfmon.user.ini") == 0) {
						// NOTE: Skip the user config for now,
						//       as we cannot ensure the order in which files will be walked,
						//       and we need it to be parsed *after* the main daemon config.
						continue;
					} else {
						// NOTE: Don't blow up when trying to store more watches than we have
						//       space for...
						if (watch_count >= WATCH_MAX) {
							LOG(LOG_WARNING,
							    "We've already setup the maximum amount of watches we can handle (%d), discarding '%s'!",
							    WATCH_MAX,
							    p->fts_name);
							// Don't flag this as a hard failure, just warn and go on...
							break;
						}

						// Assume a config is invalid until proven otherwise...
						bool is_watch_valid = false;
						int  ret =
						    ini_parse(p->fts_path, watch_handler, &watchConfig[watch_count]);
						if (ret != 0) {
							LOG(LOG_WARNING,
							    "Failed to parse watch config file '%s' (first error on line %d), it will be discarded!",
							    p->fts_name,
							    ret);
						} else {
							if (validate_watch_config(&watchConfig[watch_count])) {
								LOG(LOG_NOTICE,
								    "Watch config @ index %hhu loaded from '%s': filename=%s, action=%s, label=%s, hidden=%d, block_spawns=%d, do_db_update=%d, db_title=%s, db_author=%s, db_comment=%s",
								    watch_count,
								    p->fts_name,
								    watchConfig[watch_count].filename,
								    watchConfig[watch_count].action,
								    watchConfig[watch_count].label,
								    watchConfig[watch_count].hidden,
								    watchConfig[watch_count].block_spawns,
								    watchConfig[watch_count].do_db_update,
								    watchConfig[watch_count].db_title,
								    watchConfig[watch_count].db_author,
								    watchConfig[watch_count].db_comment);

								is_watch_valid = true;
							} else {
								LOG(LOG_WARNING,
								    "Watch config file '%s' is not valid, it will be discarded!",
								    p->fts_name);
							}
						}
						// If the watch config is valid, mark it as active, and increment the active count.
						// Otherwise, clear the slot so it can be reused.
						if (is_watch_valid) {
							watchConfig[watch_count++].is_active = true;
						} else {
							watchConfig[watch_count] = (const WatchConfig){ 0 };
						}
					}
				}
				break;
			default:
				break;
		}
	}
	fts_close(ftsp);

	// Now we can see if we have an user daemon config to handle...
	const char usercfg_path[] = KFMON_CONFIGPATH "/kfmon.user.ini";
	if (access(usercfg_path, F_OK) == 0) {
		int ret = ini_parse(usercfg_path, daemon_handler, &daemonConfig);
		if (ret != 0) {
			LOG(LOG_CRIT,
			    "Failed to parse user config file '%s' (first error on line %d), will abort!",
			    "kfmon.user.ini",
			    ret);
			// Flag as a failure...
			rval = -1;
		} else {
			LOG(LOG_NOTICE,
			    "Daemon config loaded from '%s': db_timeout=%hu, use_syslog=%d, with_notifications=%d",
			    "kfmon.user.ini",
			    daemonConfig.db_timeout,
			    daemonConfig.use_syslog,
			    daemonConfig.with_notifications);
		}
	}

#ifdef DEBUG
	// Let's recap (including failures)...
	DBGLOG("Daemon config recap: db_timeout=%hu, use_syslog=%d, with_notifications=%d",
	       daemonConfig.db_timeout,
	       daemonConfig.use_syslog,
	       daemonConfig.with_notifications);
	for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
		DBGLOG(
		    "Watch config @ index %hhu recap: active=%d, filename=%s, action=%s, label=%s, hidden=%d, block_spawns=%d, skip_db_checks=%d, do_db_update=%d, db_title=%s, db_author=%s, db_comment=%s",
		    watch_idx,
		    watchConfig[watch_idx].is_active,
		    watchConfig[watch_idx].filename,
		    watchConfig[watch_idx].action,
		    watchConfig[watch_idx].label,
		    watchConfig[watch_idx].hidden,
		    watchConfig[watch_idx].block_spawns,
		    watchConfig[watch_idx].skip_db_checks,
		    watchConfig[watch_idx].do_db_update,
		    watchConfig[watch_idx].db_title,
		    watchConfig[watch_idx].db_author,
		    watchConfig[watch_idx].db_comment);
	}
#endif

	return rval;
}

// Check if watch configs have been added/removed/updated...
static int
    update_watch_configs(void)
{
	// Walk the config directory to pickup our ini files... (c.f.,
	// https://keramida.wordpress.com/2009/07/05/fts3-or-avoiding-to-reinvent-the-wheel/)
	// We only need to walk a single directory...
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"
	char* const cfg_path[] = { KFMON_CONFIGPATH, NULL };
#pragma GCC diagnostic pop

	// Don't chdir (because that mountpoint can go buh-bye), and don't stat (because we don't need to).
	FTS* restrict ftsp;
	if ((ftsp = fts_open(
		 cfg_path, FTS_COMFOLLOW | FTS_LOGICAL | FTS_NOCHDIR | FTS_NOSTAT | FTS_XDEV, &fts_alphasort)) == NULL) {
		PFLOG(LOG_CRIT, "fts_open: %m");
		return -1;
	}
	// Initialize ftsp with as many toplevel entries as possible.
	FTSENT* restrict chp;
	chp = fts_children(ftsp, 0);
	if (chp == NULL) {
		// No files to traverse!
		LOG(LOG_CRIT, "Config directory '%s' appears to be empty, aborting!", KFMON_CONFIGPATH);
		fts_close(ftsp);
		return -1;
	}

	// Keep track of which watch indexes are up-to-date, so we can drop stale watches if some configs were deleted.
	// NOTE: Init to -1 because 0 is a valid watch index ;).
	int8_t  new_watch_list[] = { [0 ... WATCH_MAX - 1] = -1 };
	uint8_t new_watch_count  = 0U;
	// If there was a meaningful update, we'll update the IPC socket's mtime as a hint to clients that new data is available.
	bool notify_update = false;

	FTSENT* restrict p;
	while ((p = fts_read(ftsp)) != NULL) {
		switch (p->fts_info) {
			case FTS_F:
				// Check if it's a .ini and not either an unix hidden file or a Mac resource fork...
				if (p->fts_namelen > 4 &&
				    strncasecmp(p->fts_name + (p->fts_namelen - 4), ".ini", 4) == 0 &&
				    strncasecmp(p->fts_name, ".", 1) != 0) {
					// NOTE: We only care about *watch* configs,
					//       the main config is only loaded at startup
					//       This is mainly to keep our fd shenanigans sane,
					//       which would get a bit hairier
					//       if we needed to be able to toggle syslog usage...
					if (strcasecmp(p->fts_name, "kfmon.ini") == 0) {
						continue;
					} else if (strcasecmp(p->fts_name, "kfmon.user.ini") == 0) {
						continue;
					} else {
						LOG(LOG_INFO,
						    "Checking watch config file '%s' for changes . . .",
						    p->fts_path);

						// Store the results in a temporary struct,
						// so we can compare it to our current watches...
						WatchConfig cur_watch = { 0 };

						int ret = ini_parse(p->fts_path, watch_handler, &cur_watch);
						if (ret != 0) {
							LOG(LOG_WARNING,
							    "Failed to parse watch config file '%s' (first error on line %d), it will be discarded!",
							    p->fts_name,
							    ret);
						} else {
							// Try to match it to a current watch, based on the trigger file...
							uint8_t watch_idx    = 0U;
							bool    is_new_watch = true;
							for (watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
								// Only check active watches
								if (!watchConfig[watch_idx].is_active) {
									continue;
								}

								if (strcmp(cur_watch.filename,
									   watchConfig[watch_idx].filename) == 0) {
									// Gotcha!
									is_new_watch = false;
									// And we're good!
									break;
								}
							}

							if (is_new_watch) {
								// New watch! Make it so!
								int8_t new_watch_idx = get_next_available_watch_entry();
								if (new_watch_idx < 0) {
									// Discard it if we already have the maximum amount of watches set up
									LOG(LOG_WARNING,
									    "Can't find an available watch slot for '%s', probably because we've already setup the maximum amount of watches we can handle (%d), discarding it!",
									    p->fts_name,
									    WATCH_MAX);
								} else {
									watch_idx              = (uint8_t) new_watch_idx;
									watchConfig[watch_idx] = cur_watch;

									if (validate_watch_config(
										&watchConfig[watch_idx])) {
										LOG(LOG_NOTICE,
										    "Watch config @ index %hhu loaded from '%s': filename=%s, action=%s, label=%s, hidden=%d, block_spawns=%d, do_db_update=%d, db_title=%s, db_author=%s, db_comment=%s",
										    watch_idx,
										    p->fts_name,
										    watchConfig[watch_idx].filename,
										    watchConfig[watch_idx].action,
										    watchConfig[watch_idx].label,
										    watchConfig[watch_idx].hidden,
										    watchConfig[watch_idx].block_spawns,
										    watchConfig[watch_idx].do_db_update,
										    watchConfig[watch_idx].db_title,
										    watchConfig[watch_idx].db_author,
										    watchConfig[watch_idx].db_comment);

										// Flag it as active
										watchConfig[watch_idx].is_active = true;
										new_watch_list[new_watch_count++] =
										    (int8_t) watch_idx;

										fbink_printf(
										    FBFD_AUTO,
										    NULL,
										    &fbinkConfig,
										    "[KFMon] Setup a new watch on %s",
										    basename(
											watchConfig[watch_idx].filename));

										// New stuff!
										notify_update = true;
									} else {
										LOG(LOG_WARNING,
										    "New watch config file '%s' is not valid, it will be discarded!",
										    p->fts_name);

										// Clear the slot
										watchConfig[watch_idx] =
										    (const WatchConfig){ 0 };
									}
								}
							} else {
								// Updated watch!
								pthread_mutex_lock(&ptlock);
								bool is_watch_spawned =
								    is_watch_already_spawned(watch_idx);
								pthread_mutex_unlock(&ptlock);
								// Don't do anything if it's already running...
								if (is_watch_spawned) {
									LOG(LOG_INFO,
									    "Cannot update watch slot %hhu (%s => %s), as it's currently running! Discarding potentially new data from '%s'!",
									    watch_idx,
									    basename(watchConfig[watch_idx].filename),
									    basename(watchConfig[watch_idx].action),
									    p->fts_name);

									// Don't forget to flag it as a keeper...
									new_watch_list[new_watch_count++] =
									    (int8_t) watch_idx;
								} else {
									bool was_updated = false;
									// Validate what was parsed, and merge it if it's sane!
									if (validate_and_merge_watch_config(
										&cur_watch, watch_idx, &was_updated)) {
										// NOTE: validate_and_merge takes care of both
										//       logging and updating the watch data
										new_watch_list[new_watch_count++] =
										    (int8_t) watch_idx;

										// Updated stuff!
										if (was_updated) {
											notify_update = true;
										}

									} else {
										LOG(LOG_CRIT,
										    "Updated watch config file '%s' is not valid, it will be discarded!",
										    p->fts_name);

										fbink_printf(
										    FBFD_AUTO,
										    NULL,
										    &fbinkConfig,
										    "[KFMon] Dropped the watch on %s!",
										    basename(
											watchConfig[watch_idx].filename));

										// Don't keep the previous state around,
										// clear the slot.
										watchConfig[watch_idx] =
										    (const WatchConfig){ 0 };
										LOG(LOG_NOTICE,
										    "Released watch slot %hhu.",
										    watch_idx);

										// Less stuff!
										notify_update = true;
									}
								}
							}
						}
					}
				}
				break;
			default:
				break;
		}
	}
	fts_close(ftsp);

	// Purge stale watch entries (in case a config has been deleted, but not its watched file;
	// or if an existing config file was updated, but failed to pass watch_handler @ ini_parse).
	for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
		// It of course needs to be active first so it can potentially be stale ;)
		if (!watchConfig[watch_idx].is_active) {
			continue;
		}

		// Is that active entry stale (i.e., we couldn't find its config file)?
		bool keep = false;
		for (uint8_t i = 0U; i < WATCH_MAX; i++) {
			if ((int8_t) watch_idx == new_watch_list[i]) {
				keep = true;
				break;
			}
		}

		// It's stale, drop it now
		if (!keep) {
			LOG(LOG_WARNING,
			    "Watch config @ index %hhu (%s => %s) is still active, but its config file is either gone or broken! Discarding it!",
			    watch_idx,
			    basename(watchConfig[watch_idx].filename),
			    basename(watchConfig[watch_idx].action));

			fbink_printf(FBFD_AUTO,
				     NULL,
				     &fbinkConfig,
				     "[KFMon] Dropped the watch on %s!",
				     basename(watchConfig[watch_idx].filename));

			watchConfig[watch_idx] = (const WatchConfig){ 0 };
			LOG(LOG_NOTICE, "Released watch slot %hhu.", watch_idx);

			// Stale stuff!
			notify_update = true;
		}
	}

	// There were meaningful updates, update the IPC socket's mtime!
	if (notify_update) {
		// Leave atime alone, update mtime to now
		const struct timespec times[2] = { { 0, UTIME_OMIT }, { 0, UTIME_NOW } };
		if (utimensat(0, KFMON_IPC_SOCKET, times, 0) == -1) {
			PFLOG(LOG_WARNING, "utimensat: %m");
		}
	}

#ifdef DEBUG
	// Let's recap (including failures)...
	for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
		DBGLOG(
		    "Watch config @ index %hhu recap: active=%d, filename=%s, action=%s, label=%s, hidden=%d, block_spawns=%d, skip_db_checks=%d, do_db_update=%d, db_title=%s, db_author=%s, db_comment=%s",
		    watch_idx,
		    watchConfig[watch_idx].is_active,
		    watchConfig[watch_idx].filename,
		    watchConfig[watch_idx].action,
		    watchConfig[watch_idx].label,
		    watchConfig[watch_idx].hidden,
		    watchConfig[watch_idx].block_spawns,
		    watchConfig[watch_idx].skip_db_checks,
		    watchConfig[watch_idx].do_db_update,
		    watchConfig[watch_idx].db_title,
		    watchConfig[watch_idx].db_author,
		    watchConfig[watch_idx].db_comment);
	}
#endif

	return 0;
}

// Implementation of Qt4's QtHash, c.f., qhash @
// https://github.com/kovidgoyal/calibre/blob/205754891e341e7f940e70057ac3a96a2443fdbd/src/calibre/devices/kobo/driver.py#L41-L59
static unsigned int
    qhash(const unsigned char* restrict bytes, size_t length)
{
	unsigned int h = 0;

	for (unsigned int i = 0; i < length; i++) {
		h = (h << 4) + bytes[i];
		h ^= (h & 0xf0000000) >> 23;
		h &= 0x0fffffff;
	}

	return h;
}

// Check if our target file has been processed by Nickel...
static bool
    is_target_processed(uint8_t watch_idx, bool wait_for_db)
{
#ifdef DEBUG
	// Bypass DB checks on demand for debugging purposes...
	if (watchConfig[watch_idx].skip_db_checks)
		return true;
#endif

	// Did the user want to try to update the DB for this icon?
	bool update       = watchConfig[watch_idx].do_db_update;
	bool is_processed = false;
	bool needs_update = false;

	// NOTE: Open the db in single-thread threading mode (we build w/o threadsafe),
	//       and without a shared cache: we only do SQL from the main thread.
	sqlite3* db;
	if (update) {
		CALL_SQLITE(open_v2(
		    KOBO_DB_PATH, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_PRIVATECACHE, NULL));
	} else {
		// Open the DB ro to be extra-safe...
		CALL_SQLITE(open_v2(
		    KOBO_DB_PATH, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_PRIVATECACHE, NULL));
	}

	// Wait at most for Nms on OPEN & N*2ms on CLOSE if we ever hit a locked database during any of our proceedings.
	// NOTE: The defaults timings (steps of 500ms) appear to work reasonably well on my H2O with a 50MB Nickel DB...
	//       (i.e., it trips on OPEN when Nickel is moderately busy, but if everything's quiet, we're good).
	//       Time will tell if that's a good middle-ground or not ;).
	//       This is user configurable in kfmon.ini (db_timeout key).
	// NOTE: On current FW versions, where the DB is now using WAL, we're exceedingly unlikely to ever hit a BUSY DB
	//       (c.f., https://www.sqlite.org/wal.html)
	sqlite3_busy_timeout(db, (int) daemonConfig.db_timeout * (wait_for_db + 1));
	DBGLOG("SQLite busy timeout set to %dms", (int) daemonConfig.db_timeout * (wait_for_db + 1));

	// NOTE: ContentType 6 should mean a book on pretty much anything since FW 1.9.17 (and why a book?
	//       Because Nickel currently identifies single PNGs as application/x-cbz, bless its cute little bytes).
	sqlite3_stmt* stmt;
	CALL_SQLITE(prepare_v2(
	    db, "SELECT EXISTS(SELECT 1 FROM content WHERE ContentID = @id AND ContentType = '6');", -1, &stmt, NULL));

	// Append the proper URI scheme to our icon path...
	char book_path[CFG_SZ_MAX + 7];
	snprintf(book_path, sizeof(book_path), "file://%s", watchConfig[watch_idx].filename);

	int idx = sqlite3_bind_parameter_index(stmt, "@id");
	CALL_SQLITE(bind_text(stmt, idx, book_path, -1, SQLITE_STATIC));

	int rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		DBGLOG("SELECT SQL query returned: %d", sqlite3_column_int(stmt, 0));
		if (sqlite3_column_int(stmt, 0) == 1) {
			is_processed = true;
		}
	}

	sqlite3_finalize(stmt);

	// NOTE: If the file doesn't appear to have been processed by Nickel yet, despite clearly existing on the FS,
	//       since we got an inotify event from it, see if there isn't a case issue in the filename specified in the .ini...
	//       (FAT32 is case-insensitive, but we make a case sensitive SQL query, because it's much faster!)
	// NOTE: This works, but is commented out,
	//       because it's a massive performance sink for what's essentially a very minor QOL fix for a PEBCAK...
	/*
	if (!is_processed) {
		bool broken_case = false;

		CALL_SQLITE(prepare_v2(
		    db,
		    "SELECT EXISTS(SELECT 1 FROM content WHERE ContentID = @id COLLATE NOCASE AND ContentType = '6');",
		    -1,
		    &stmt,
		    NULL));
		idx = sqlite3_bind_parameter_index(stmt, "@id");
		CALL_SQLITE(bind_text(stmt, idx, book_path, -1, SQLITE_STATIC));

		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			DBGLOG("SELECT SQL query returned: %d", sqlite3_column_int(stmt, 0));
			if (sqlite3_column_int(stmt, 0) == 1) {
				is_processed = true;
				broken_case  = true;
			}
		}

		sqlite3_finalize(stmt);

		// If the case-insensitive query worked, do yet another query to log the expected filename...
		if (broken_case) {
			CALL_SQLITE(prepare_v2(
			    db,
			    "SELECT ContentID FROM content WHERE ContentID = @id COLLATE NOCASE AND ContentType = '6';",
			    -1,
			    &stmt,
			    NULL));
			idx = sqlite3_bind_parameter_index(stmt, "@id");
			CALL_SQLITE(bind_text(stmt, idx, book_path, -1, SQLITE_STATIC));

			rc = sqlite3_step(stmt);
			if (rc == SQLITE_ROW) {
				// Warn, and update book_path, so we don't have to do any more slow case-insensitive queries...
				snprintf(book_path, sizeof(book_path), "%s", sqlite3_column_text(stmt, 0));
				DBGLOG("SELECT SQL query returned: %s", book_path);
				LOG(LOG_WARNING,
				    "Watch config @ index %hhu has a filename field with broken case (%s -> %s)!",
				    watch_idx,
				    watchConfig[watch_idx].filename,
				    book_path + 7);
			}

			sqlite3_finalize(stmt);
		}
	}
	*/

	// Now that we know the book exists, we also want to check if the thumbnails do,
	// to avoid getting triggered from the thumbnail creation...
	// NOTE: Again, this assumes FW >= 2.9.0
	if (is_processed) {
		// Assume they haven't been processed until we can confirm it...
		is_processed = false;

		// We'll need the ImageID first...
		CALL_SQLITE(prepare_v2(
		    db, "SELECT ImageID FROM content WHERE ContentID = @id AND ContentType = '6';", -1, &stmt, NULL));

		idx = sqlite3_bind_parameter_index(stmt, "@id");
		CALL_SQLITE(bind_text(stmt, idx, book_path, -1, SQLITE_STATIC));

		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			const unsigned char* image_id = sqlite3_column_text(stmt, 0);
			size_t               len      = (size_t) sqlite3_column_bytes(stmt, 0);
			DBGLOG("SELECT SQL query returned: %s", image_id);

			// Then we need the proper hashes Nickel devises...
			// c.f., images_path @
			// https://github.com/kovidgoyal/calibre/blob/205754891e341e7f940e70057ac3a96a2443fdbd/src/calibre/devices/kobo/driver.py#L2584-L2600
			unsigned int hash = qhash(image_id, len);
			unsigned int dir1 = hash & (0xff * 1);
			unsigned int dir2 = (hash & (0xff00 * 1)) >> 8;

			char images_path[KFMON_PATH_MAX];
			int  ret = snprintf(images_path,
                                           sizeof(images_path),
                                           "%s/.kobo-images/%u/%u",
                                           KFMON_TARGET_MOUNTPOINT,
                                           dir1,
                                           dir2);
			if (ret < 0 || (size_t) ret >= sizeof(images_path)) {
				LOG(LOG_WARNING, "Couldn't build the image path string!");
			}
			DBGLOG("Checking for thumbnails in '%s' . . .", images_path);

			// Count the number of processed thumbnails we find...
			uint8_t thumbnails_count = 0U;
			char    thumbnail_path[KFMON_PATH_MAX];

			// Start with the full-size screensaver...
			ret = snprintf(
			    thumbnail_path, sizeof(thumbnail_path), "%s/%s - N3_FULL.parsed", images_path, image_id);
			if (ret < 0 || (size_t) ret >= sizeof(thumbnail_path)) {
				LOG(LOG_WARNING, "Couldn't build the thumbnail path string!");
			}
			DBGLOG("Checking for full-size screensaver '%s' . . .", thumbnail_path);
			if (access(thumbnail_path, F_OK) == 0) {
				thumbnails_count++;
			} else {
				LOG(LOG_INFO, "Full-size screensaver hasn't been parsed yet!");
			}

			// Then the Homescreen tile...
			// NOTE: This one might be a tad confusing...
			//       If the icon has never been processed,
			//       this will only happen the first time we *close* the PNG's "book"...
			//       (i.e., the moment it pops up as the 'last opened' tile).
			//       And *that* processing triggers a set of OPEN & CLOSE,
			//       meaning we can quite possibly run on book *exit* that first time,
			//       (and only that first time), if database locking permits...
			ret = snprintf(thumbnail_path,
				       sizeof(thumbnail_path),
				       "%s/%s - N3_LIBRARY_FULL.parsed",
				       images_path,
				       image_id);
			if (ret < 0 || (size_t) ret >= sizeof(thumbnail_path)) {
				LOG(LOG_WARNING, "Couldn't build the thumbnail path string!");
			}
			DBGLOG("Checking for homescreen tile '%s' . . .", thumbnail_path);
			if (access(thumbnail_path, F_OK) == 0) {
				thumbnails_count++;
			} else {
				LOG(LOG_INFO, "Homescreen tile hasn't been parsed yet!");
			}

			// And finally the Library thumbnail...
			ret = snprintf(thumbnail_path,
				       sizeof(thumbnail_path),
				       "%s/%s - N3_LIBRARY_GRID.parsed",
				       images_path,
				       image_id);
			if (ret < 0 || (size_t) ret >= sizeof(thumbnail_path)) {
				LOG(LOG_WARNING, "Couldn't build the thumbnail path string!");
			}
			DBGLOG("Checking for library thumbnail '%s' . . .", thumbnail_path);
			if (access(thumbnail_path, F_OK) == 0) {
				thumbnails_count++;
			} else {
				LOG(LOG_INFO, "Library thumbnail hasn't been parsed yet!");
			}

			// Only give a greenlight if we got all three!
			if (thumbnails_count == 3U) {
				is_processed = true;
			}
		}

		// NOTE: It's now safe to destroy the statement.
		//       (We can't do that early in the success branch,
		//       because we still hold a pointer to a result depending on the statement (image_id))
		sqlite3_finalize(stmt);
	}

	// NOTE: Here be dragons!
	//       This works in theory,
	//       but risks confusing Nickel's handling of the DB if we do that when nickel is running (which we are).
	//       Because doing it with Nickel running is a potentially terrible idea,
	//       for various reasons (c.f., https://www.sqlite.org/howtocorrupt.html for the gory details,
	//       some of which probably even apply here! :p).
	//       As such, we leave enabling this option to the user's responsibility.
	//       KOReader ships with it disabled.
	//       The idea is to, optionally, update the Title, Author & Comment fields to make them more useful...
	if (is_processed && update) {
		// Check if the DB has already been updated by checking the title...
		CALL_SQLITE(prepare_v2(
		    db, "SELECT Title FROM content WHERE ContentID = @id AND ContentType = '6';", -1, &stmt, NULL));

		idx = sqlite3_bind_parameter_index(stmt, "@id");
		CALL_SQLITE(bind_text(stmt, idx, book_path, -1, SQLITE_STATIC));

		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			DBGLOG("SELECT SQL query returned: %s", sqlite3_column_text(stmt, 0));
			if (strcmp((const char*) sqlite3_column_text(stmt, 0), watchConfig[watch_idx].db_title) != 0) {
				needs_update = true;
			}
		}

		sqlite3_finalize(stmt);
	}
	if (needs_update) {
		CALL_SQLITE(prepare_v2(
		    db,
		    "UPDATE content SET Title = @title, Attribution = @author, Description = @comment WHERE ContentID = @id AND ContentType = '6';",
		    -1,
		    &stmt,
		    NULL));

		// NOTE: No sanity checks are done to confirm that those watch configs are sane,
		//       we only check that they are *present*...
		//       The example config ships with a strong warning not to forget them if wanted, but that's it.
		idx = sqlite3_bind_parameter_index(stmt, "@title");
		CALL_SQLITE(bind_text(stmt, idx, watchConfig[watch_idx].db_title, -1, SQLITE_STATIC));
		idx = sqlite3_bind_parameter_index(stmt, "@author");
		CALL_SQLITE(bind_text(stmt, idx, watchConfig[watch_idx].db_author, -1, SQLITE_STATIC));
		idx = sqlite3_bind_parameter_index(stmt, "@comment");
		CALL_SQLITE(bind_text(stmt, idx, watchConfig[watch_idx].db_comment, -1, SQLITE_STATIC));
		idx = sqlite3_bind_parameter_index(stmt, "@id");
		CALL_SQLITE(bind_text(stmt, idx, book_path, -1, SQLITE_STATIC));

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			LOG(LOG_WARNING, "UPDATE SQL query failed: %s", sqlite3_errmsg(db));
		} else {
			LOG(LOG_NOTICE, "Successfully updated DB data for the target PNG");
		}

		sqlite3_finalize(stmt);
	}

	// A rather crappy check to wait for pending COMMITs...
	if (is_processed && wait_for_db) {
		// If there's a rollback journal for the DB, wait for it to go away...
		// NOTE: This assumes the DB was opened with the default journal_mode, DELETE
		//       This doesn't appear to be the case anymore, on FW >= 4.6.x (and possibly earlier),
		//       it's now using WAL (which makes sense, and our whole job safer ;)).
		const struct timespec zzz   = { 0L, 500000000L };
		uint8_t               count = 0U;
		while (access(KOBO_DB_PATH "-journal", F_OK) == 0) {
			LOG(LOG_INFO,
			    "Found a SQLite rollback journal, waiting for it to go away (iteration nr. %hhu) . . .",
			    (uint8_t) count++);
			nanosleep(&zzz, NULL);
			// NOTE: Don't wait more than 10s
			if (count >= 20U) {
				LOG(LOG_WARNING,
				    "Waited for the SQLite rollback journal to go away for far too long, going on anyway.");
				break;
			}
		}
	}

	sqlite3_close(db);

	return is_processed;
}

// Heavily inspired from https://stackoverflow.com/a/35235950
// Initializes the process table. -1 means the entry in the table is available.
static void
    init_process_table(void)
{
	for (uint8_t i = 0U; i < WATCH_MAX; i++) {
		PT.spawn_pids[i]     = -1;
		PT.spawn_watchids[i] = -1;
	}
}

// Returns the index of the next available entry in the process table.
static int8_t
    get_next_available_pt_entry(void)
{
	for (uint8_t i = 0U; i < WATCH_MAX; i++) {
		if (PT.spawn_watchids[i] == -1) {
			return (int8_t) i;
		}
	}
	return -1;
}

// Adds information about a new spawn to the process table.
static void
    add_process_to_table(uint8_t i, pid_t pid, uint8_t watch_idx)
{
	PT.spawn_pids[i]     = pid;
	PT.spawn_watchids[i] = (int8_t) watch_idx;
}

// Removes information about a spawn from the process table.
static void
    remove_process_from_table(uint8_t i)
{
	PT.spawn_pids[i]     = -1;
	PT.spawn_watchids[i] = -1;
}

// Initializes the FBInk config
static void
    init_fbink_config(void)
{
	// NOTE: The struct is zero-initialized, so we only tweak what's non-default
	//       (the defaults are explicitly designed to always be 0 for this very purpose).
	fbinkConfig.row         = -5;
	fbinkConfig.is_centered = true;
	fbinkConfig.is_padded   = true;
	// NOTE: For now, we *want* fbink_init's status report logged, so we leave this disabled.
	// fbinkConfig.is_quiet = false;
	// If we log to syslog, tell FBInk to do the same
	if (daemonConfig.use_syslog) {
		fbinkConfig.to_syslog = true;
	}
}

// Wait for a specific child process to die, and reap it (runs in a dedicated thread per spawn).
static void*
    reaper_thread(void* ptr)
{
	uint8_t i = *((uint8_t*) ptr);

	pid_t tid = (pid_t) syscall(SYS_gettid);

	pid_t   cpid;
	uint8_t watch_idx;
	pthread_mutex_lock(&ptlock);
	cpid      = PT.spawn_pids[i];
	watch_idx = (uint8_t) PT.spawn_watchids[i];
	pthread_mutex_unlock(&ptlock);

	// Storage needed for get_current_time_r
	struct tm local_tm;
	char      sz_time[22];

	// Remember the current time for the execvp errno/exitcode heuristic...
	struct timespec then = { 0 };
	clock_gettime(CLOCK_MONOTONIC_RAW, &then);

	MTLOG(LOG_INFO,
	      "[%s] [INFO] [TID: %ld] Waiting to reap process %ld (from watch idx %hhu) . . .",
	      get_current_time_r(&local_tm, sz_time, sizeof(sz_time)),
	      (long) tid,
	      (long) cpid,
	      watch_idx);
	pid_t ret;
	int   wstatus;
	// Wait for our child process to terminate, retrying on EINTR
	// NOTE: This is quite likely overkill on Linux (c.f., https://stackoverflow.com/a/59795677)
	do {
		ret = waitpid(cpid, &wstatus, 0);
	} while (ret == -1 && errno == EINTR);
	// Recap what happened to it
	if (ret != cpid) {
		PFMTLOG(LOG_CRIT, "waitpid: %m");
		free(ptr);
		return (void*) NULL;
	} else {
		if (WIFEXITED(wstatus)) {
			int exitcode = WEXITSTATUS(wstatus);
			MTLOG(
			    LOG_NOTICE,
			    "[%s] [NOTE] [TID: %ld] Reaped process %ld (from watch idx %hhu): It exited with status %d.",
			    get_current_time_r(&local_tm, sz_time, sizeof(sz_time)),
			    (long) tid,
			    (long) cpid,
			    watch_idx,
			    exitcode);
			// NOTE: Ugly hack to try to salvage execvp's potential error...
			//       If the process exited with a non-zero status code,
			//       within (roughly) a second of being launched,
			//       assume the exit code is actually inherited from execvp's errno...
			struct timespec now = { 0 };
			clock_gettime(CLOCK_MONOTONIC_RAW, &now);
			// NOTE: We should be okay not using difftime on Linux (We're using a monotonic clock, time_t is int64_t).
			if (exitcode != 0 && (now.tv_sec - then.tv_sec) <= 1) {
				char buf[256];
				// NOTE: We *know* we'll be using the GNU, glibc >= 2.13 version of strerror_r
				// NOTE: Even if it's not entirely clear from the manpage, printf's %m *is* thread-safe,
				//       c.f., stdio-common/vfprintf.c:962 (it's using strerror_r).
				//       But since we're not checking errno but a custom variable, do it the hard way :)
				const char* sz_error = strerror_r(exitcode, buf, sizeof(buf));
				MTLOG(
				    LOG_CRIT,
				    "[%s] [CRIT] [TID: %ld] If nothing was visibly launched, and/or especially if status > 1, this *may* actually be an execvp() error: %s.",
				    get_current_time_r(&local_tm, sz_time, sizeof(sz_time)),
				    (long) tid,
				    sz_error);
				fbink_printf(FBFD_AUTO,
					     NULL,
					     &fbinkConfig,
					     "[KFMon] PID %ld exited unexpectedly: %d!",
					     (long) cpid,
					     exitcode);
			}
		} else if (WIFSIGNALED(wstatus)) {
			// NOTE: strsignal is not thread safe... Use psignal instead.
			int  sigcode = WTERMSIG(wstatus);
			char buf[256];
			snprintf(
			    buf,
			    sizeof(buf),
			    "[KFMon] [%s] [WARN] [TID: %ld] Reaped process %ld (from watch idx %hhu): It was killed by signal %d",
			    get_current_time_r(&local_tm, sz_time, sizeof(sz_time)),
			    (long) tid,
			    (long) cpid,
			    watch_idx,
			    sigcode);
			fbink_printf(FBFD_AUTO,
				     NULL,
				     &fbinkConfig,
				     "[KFMon] PID %ld was killed by signal %d!",
				     (long) cpid,
				     sigcode);
			if (daemonConfig.use_syslog) {
				// NOTE: No strsignal means no human-readable interpretation of the signal w/ syslog
				//       (the %m token only works for errno)...
				syslog(LOG_NOTICE, "%s", buf);
			} else {
				psignal(sigcode, buf);
			}
		}
	}

	// And now we can safely remove it from the process table
	pthread_mutex_lock(&ptlock);
	remove_process_from_table(i);
	pthread_mutex_unlock(&ptlock);

	free(ptr);

	return (void*) NULL;
}

// Spawn a process and return its pid...
// Initially inspired from popen2() implementations from https://stackoverflow.com/questions/548063
// As well as the glibc's system() call,
// With a bit of added tracking to handle reaping without a SIGCHLD handler.
static pid_t
    spawn(char* const* command, uint8_t watch_idx)
{
	pid_t pid = fork();

	if (pid < 0) {
		// Fork failed?
		PFLOG(LOG_ERR, "Aborting: fork: %m");
		fbink_print(FBFD_AUTO, "[KFMon] fork failed ?!", &fbinkConfig);
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		// Sweet child o' mine!
		// NOTE: We're multithreaded & forking, this means that from this point on until execve(),
		//       we can only use async-safe functions!
		//       See pthread_atfork(3) for details.
		// Do the whole stdin/stdout/stderr dance again,
		// to ensure that child process doesn't inherit our tweaked fds...
		dup2(origStdin, fileno(stdin));
		dup2(origStdout, fileno(stdout));
		dup2(origStderr, fileno(stderr));
		close(origStdin);
		close(origStdout);
		close(origStderr);
		// Restore signals
		struct sigaction sa = { .sa_handler = SIG_DFL, .sa_flags = SA_RESTART };
		sigaction(SIGHUP, &sa, NULL);
		// NOTE: We used to use execvpe when being launched from udev,
		//       in order to sanitize all the crap we inherited from udev's env ;).
		//       Now, we actually rely on the specific env we inherit from rcS/on-animator!
		execvp(*command, command);
		// NOTE: This will only ever be reached on error, hence the lack of actual return value check ;).
		//       Resort to an ugly hack by exiting with execvp()'s errno,
		//       which we can then try to salvage in the reaper thread.
		exit(errno);
	} else {
		// Parent
		// Keep track of the process
		int8_t i;
		pthread_mutex_lock(&ptlock);
		i = get_next_available_pt_entry();
		pthread_mutex_unlock(&ptlock);

		if (i < 0) {
			// NOTE: If we ever hit this error codepath,
			//       we don't have to worry about leaving that last spawn as a zombie:
			//       One of the benefits of the double-fork we do to daemonize is that, on our death,
			//       our children will get reparented to init, which, by design,
			//       will handle the reaping automatically.
			LOG(LOG_ERR,
			    "Failed to find an available entry in our process table for pid %ld, aborting!",
			    (long) pid);
			fbink_print(FBFD_AUTO, "[KFMon] Can't spawn any more processes!", &fbinkConfig);
			exit(EXIT_FAILURE);
		} else {
			pthread_mutex_lock(&ptlock);
			add_process_to_table((uint8_t) i, pid, watch_idx);
			pthread_mutex_unlock(&ptlock);

			DBGLOG("Assigned pid %ld (from watch idx %hhu) to process table entry idx %hhd",
			       (long) pid,
			       watch_idx,
			       i);
			// NOTE: We can't do that from the child proper, because it's not async-safe,
			//       so do it from here.
			LOG(LOG_NOTICE,
			    "Spawned process %ld (%s -> %s @ watch idx %hhu) . . .",
			    (long) pid,
			    watchConfig[watch_idx].filename,
			    watchConfig[watch_idx].action,
			    watch_idx);
			if (daemonConfig.with_notifications) {
				fbink_printf(FBFD_AUTO,
					     NULL,
					     &fbinkConfig,
					     "[KFMon] Launched %s :)",
					     basename(watchConfig[watch_idx].action));
			}
			// NOTE: We achieve reaping in a non-blocking way by doing the reaping from a dedicated thread
			//       for every spawn...
			//       See #2 for an history of the previous failed attempts...
			pthread_t rthread;
			uint8_t*  arg = malloc(sizeof(*arg));
			if (arg == NULL) {
				LOG(LOG_ERR, "Couldn't allocate memory for thread arg, aborting!");
				fbink_print(FBFD_AUTO, "[KFMon] OOM ?!", &fbinkConfig);
				exit(EXIT_FAILURE);
			}
			*arg = (uint8_t) i;

			// NOTE: We will *never* wait for one of these threads to die from the main thread, so,
			//       start them in detached state
			//       to make sure their resources will be released when they terminate.
			pthread_attr_t attr;
			if (pthread_attr_init(&attr) != 0) {
				PFLOG(LOG_ERR, "Aborting: pthread_attr_init: %m");
				fbink_print(FBFD_AUTO, "[KFMon] pthread_attr_init failed ?!", &fbinkConfig);
				exit(EXIT_FAILURE);
			}
			if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
				PFLOG(LOG_ERR, "Aborting: pthread_attr_setdetachstate: %m");
				fbink_print(FBFD_AUTO, "[KFMon] pthread_attr_setdetachstate failed ?!", &fbinkConfig);
				exit(EXIT_FAILURE);
			}

			// NOTE: Use a smaller stack (ulimit -s is 8MB on the Kobos).
			//       Base it on pointer size, aiming for 1MB on x64 (meaning 512KB on x86/arm).
			//       Floor it at 512KB to be safe, though.
			//       In the grand scheme of things, this won't really change much ;).
			if (pthread_attr_setstacksize(
				&attr, MAX((1U * 1024U * 1024U) / 2U, (sizeof(void*) * 1024U * 1024U) / 8U)) != 0) {
				PFLOG(LOG_ERR, "Aborting: pthread_attr_setstacksize: %m");
				fbink_print(FBFD_AUTO, "[KFMon] pthread_attr_setstacksize failed ?!", &fbinkConfig);
				exit(EXIT_FAILURE);
			}
			if (pthread_create(&rthread, &attr, reaper_thread, arg) != 0) {
				PFLOG(LOG_ERR, "Aborting: pthread_create: %m");
				fbink_print(FBFD_AUTO, "[KFMon] pthread_create failed ?!", &fbinkConfig);
				exit(EXIT_FAILURE);
			}

			// Prettify the thread's name. Must be <= 15 characters long (i.e., 16 bytes, NULL included).
			char thname[16];
			snprintf(thname, sizeof(thname), "Reap:%ld", (long) pid);
			if (pthread_setname_np(rthread, thname) != 0) {
				PFLOG(LOG_ERR, "Aborting: pthread_setname_np: %m");
				fbink_print(FBFD_AUTO, "[KFMon] pthread_setname_np failed ?!", &fbinkConfig);
				exit(EXIT_FAILURE);
			}

			if (pthread_attr_destroy(&attr) != 0) {
				PFLOG(LOG_ERR, "Aborting: pthread_attr_destroy: %m");
				fbink_print(FBFD_AUTO, "[KFMon] pthread_attr_destroy failed ?!", &fbinkConfig);
				exit(EXIT_FAILURE);
			}
		}
	}

	return pid;
}

// Check if a given inotify watch already has a spawn running
static bool
    is_watch_already_spawned(uint8_t watch_idx)
{
	// Walk our process table to see if the given watch currently has a registered running process
	for (uint8_t i = 0U; i < WATCH_MAX; i++) {
		if (PT.spawn_watchids[i] == (int8_t) watch_idx) {
			return true;
			// NOTE: Assume everything's peachy,
			//       and we'll never end up with the same watch_idx assigned to multiple indices in the
			//       process table.
		}
	}

	return false;
}

// Check if a watch flagged as a spawn blocker (e.g., KOReader or Plato) is already running
// NOTE: This is mainly to prevent spurious spawns that might be unwittingly caused by their file manager
//       (be it through metadata reading, thumbnails creation, or whatever).
//       Another workaround is of course to kill KFMon as part of their startup process...
static bool
    is_blocker_running(void)
{
	// Walk our process table to identify watches with a currently running process
	for (uint8_t i = 0U; i < WATCH_MAX; i++) {
		if (PT.spawn_watchids[i] != -1) {
			// Walk the active watch list to match that currently running watch to its block_spawns flag
			for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
				if (!watchConfig[watch_idx].is_active) {
					continue;
				}

				if (PT.spawn_watchids[i] == (int8_t) watch_idx) {
					if (watchConfig[watch_idx].block_spawns) {
						return true;
					}
				}
			}
		}
	}

	// Nothing currently running is a spawn blocker, we're good to go!
	return false;
}

// Check if spawns are inhibited by the global block file
static bool
    are_spawns_blocked(void)
{
	const char block_path[] = KFMON_CONFIGPATH "/BLOCK";
	if (access(block_path, F_OK) == 0) {
		// Global block file is here, prevent new spawns...
		return true;
	}

	return false;
}

// Return the pid of the spawn of a given inotify watch
static pid_t
    get_spawn_pid_for_watch(uint8_t watch_idx)
{
	for (uint8_t i = 0U; i < WATCH_MAX; i++) {
		if (PT.spawn_watchids[i] == (int8_t) watch_idx) {
			return PT.spawn_pids[i];
		}
	}

	return -1;
}

// Read all available inotify events from the file descriptor 'fd' (caller breaks on true).
static bool
    handle_events(int fd)
{
	// NOTE: Because the framebuffer state is liable to have changed since our last init/reinit,
	//       either expectedly (boot -> pickel -> nickel), or a bit more unpredictably (rotation, bitdepth change),
	//       we'll ask FBInk to make sure it has an up-to-date fb state for each new batch of events,
	//       so that messages will be printed properly, no matter what :).
	//       Put everything behind our mutex to be super-safe,
	//       since we're playing with library globals...
	// NOTE: Even forgetting about rotation and bitdepth changes, which may not ever happen on most *vanilla* devices,
	//       this is needed because processing is done very early by Nickel for "new" icons,
	//       when they end up on the Home screen straight away,
	//       (which is a given if you added at most 3 items, with the new Home screen).
	//       Not doing a reinit would be problematic, because it's early enough that pickel is still running,
	//       so we'd be inheriting its quirky fb setup and not Nickel's...
	// NOTE: This was moved from inside the following loop to here, just outside of it, in order to limit locking,
	//       but it will in fact change nothing if events aren't actually batched,
	//       which appears to be the case in most of our use-cases...
	pthread_mutex_lock(&ptlock);
	// NOTE: It went fine once, assume that'll still be the case and skip error checking...
	fbink_reinit(FBFD_AUTO, &fbinkConfig);
	pthread_mutex_unlock(&ptlock);

	// Some systems cannot read integer variables if they are not properly aligned.
	// On other systems, incorrect alignment may decrease performance.
	// Hence, the buffer used for reading from the inotify file descriptor
	// should have the same alignment as struct inotify_event.
	char                        buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event* event;
	bool                        destroyed_wd  = false;
	bool                        was_unmounted = false;

	// Loop while events can be read from inotify file descriptor.
	for (;;) {
		// Read some events.
		ssize_t len = read(fd, buf, sizeof(buf));    // Flawfinder: ignore
		if (len == -1 && errno != EAGAIN) {
			if (errno == EINTR) {
				continue;
			}
			PFLOG(LOG_ERR, "Aborting: read: %m");
			fbink_print(FBFD_AUTO, "[KFMon] read failed ?!", &fbinkConfig);
			exit(EXIT_FAILURE);
		}

		// If the nonblocking read() found no events to read,
		// then it returns -1 with errno set to EAGAIN.
		// In that case, we exit the loop.
		if (len <= 0) {
			break;
		}

		// Loop over all events in the buffer
		for (char* ptr = buf; ptr < buf + len; ptr += sizeof(*event) + event->len) {
			// NOTE: This trips -Wcast-align on ARM, but should be safe nonetheless ;).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
			event = (const struct inotify_event*) ptr;
#pragma GCC diagnostic pop

			// Identify which of our target file we've caught an event for...
			uint8_t watch_idx       = 0U;
			bool    found_watch_idx = false;
			for (watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
				// Needs to be an active watch
				if (!watchConfig[watch_idx].is_active) {
					continue;
				}

				if (watchConfig[watch_idx].inotify_wd == event->wd) {
					found_watch_idx = true;
					break;
				}
			}
			if (!found_watch_idx) {
				// NOTE: Err, that should (hopefully) never happen!
				LOG(LOG_CRIT,
				    "!! Failed to match the current inotify event to any of our watched file! !!");
				// NOTE: First, point to the final slot, instead of OOB. This'll at least avoid UB.
				//       We *probably* ought to fail harder here, though,
				//       but I *do* want to drain the event...
				watch_idx = WATCH_MAX - 1;
			}

			// Print event type
			if (event->mask & IN_OPEN) {
				LOG(LOG_NOTICE, "Tripped IN_OPEN for %s", watchConfig[watch_idx].filename);
				// Clunky detection of potential Nickel processing...
				bool is_watch_spawned;
				bool is_blocker_spawned;
				pthread_mutex_lock(&ptlock);
				is_watch_spawned   = is_watch_already_spawned(watch_idx);
				is_blocker_spawned = is_blocker_running();
				pthread_mutex_unlock(&ptlock);
				bool is_spawn_blocked = are_spawns_blocked();

				if (!is_watch_spawned && !is_blocker_spawned && !is_spawn_blocked) {
					// Only check if we're ready to spawn something...
					if (!is_target_processed(watch_idx, false)) {
						// It's not processed on OPEN, flag as pending...
						watchConfig[watch_idx].pending_processing = true;
						LOG(LOG_INFO,
						    "Flagged target icon '%s' as pending processing ...",
						    watchConfig[watch_idx].filename);
					} else {
						// It's already processed, we're good!
						watchConfig[watch_idx].pending_processing = false;
					}
				}
			}
			if (event->mask & IN_CLOSE) {
				LOG(LOG_NOTICE, "Tripped IN_CLOSE for %s", watchConfig[watch_idx].filename);
				// NOTE: Make sure we won't run a specific command multiple times
				//       while an earlier instance of it is still running...
				//       This is mostly of interest for KOReader/Plato:
				//       it means we can keep KFMon running while they're up,
				//       without risking trying to spawn multiple instances of them,
				//       in case they end up tripping their own inotify watch ;).
				bool is_watch_spawned;
				bool is_blocker_spawned;
				pthread_mutex_lock(&ptlock);
				is_watch_spawned   = is_watch_already_spawned(watch_idx);
				is_blocker_spawned = is_blocker_running();
				pthread_mutex_unlock(&ptlock);
				bool is_spawn_blocked = are_spawns_blocked();

				if (!is_watch_spawned && !is_blocker_spawned && !is_spawn_blocked) {
					// Check that our target file has already fully been processed by Nickel
					// before launching anything...
					bool should_spawn = !watchConfig[watch_idx].pending_processing &&
							    is_target_processed(watch_idx, true);
					// NOTE: In case the target file has been processed during this power cycle,
					//       check that it happened at least 10s ago, to avoid spurious launches on start,
					//       as FW 4.13 now appears to trigger an extra set of open/close events on startup,
					//       right *after* having processed a new image. Which means that without this check,
					//       it happily blazes right through every other checks,
					//       and ends up running the new target script straightaway... :/
					if (should_spawn && watchConfig[watch_idx].processing_ts > 0) {
						struct timespec now = { 0 };
						clock_gettime(CLOCK_MONOTONIC_RAW, &now);
						if (now.tv_sec - watchConfig[watch_idx].processing_ts <= 10) {
							LOG(LOG_NOTICE,
							    "Target icon '%s' has only *just* finished processing, assuming this is a spurious post-processing event!",
							    watchConfig[watch_idx].filename);
							should_spawn = false;
						} else {
							// Now that everything appears sane, clear the processing timestamp,
							// to avoid going through this branch for the rest of this power cycle ;).
							LOG(LOG_NOTICE,
							    "Target icon '%s' should be properly processed by now :)",
							    watchConfig[watch_idx].filename);
							watchConfig[watch_idx].processing_ts = 0;
						}
					}

					if (should_spawn) {
						LOG(LOG_INFO,
						    "Preparing to spawn %s for watch idx %hhu . . .",
						    watchConfig[watch_idx].action,
						    watch_idx);
						if (watchConfig[watch_idx].block_spawns) {
							LOG(LOG_NOTICE,
							    "%s is flagged as a spawn blocker, it will prevent *any* event from triggering a spawn while it is still running!",
							    watchConfig[watch_idx].action);
						}
						// We're using execvp()...
						char* const cmd[] = { watchConfig[watch_idx].action, NULL };
						spawn(cmd, watch_idx);
					} else {
						LOG(LOG_NOTICE,
						    "Target icon '%s' might not have been fully processed by Nickel yet, don't launch anything.",
						    watchConfig[watch_idx].filename);
						fbink_printf(FBFD_AUTO,
							     NULL,
							     &fbinkConfig,
							     "[KFMon] Not spawning %s: still processing!",
							     basename(watchConfig[watch_idx].action));
						// NOTE: That, or we hit a SQLITE_BUSY timeout on OPEN,
						//       which tripped our 'pending processing' check.
						// NOTE: The first time we encounter a not-yet processed file on close,
						//       remember it, so we can avoid a spurious launch in case Nickel
						//       triggers multiple open/close events in a very short amount of time,
						//       as seems to be the case on startup since FW 4.13 for brand new files...
						if (watchConfig[watch_idx].processing_ts == 0) {
							struct timespec now;
							if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) == 0) {
								watchConfig[watch_idx].processing_ts = now.tv_sec;
							}
						}
					}
				} else {
					if (is_watch_spawned) {
						pid_t spid;
						pthread_mutex_lock(&ptlock);
						spid = get_spawn_pid_for_watch(watch_idx);
						pthread_mutex_unlock(&ptlock);

						LOG(LOG_INFO,
						    "As watch idx %hhu (%s) still has a spawned process (%ld -> %s) running, we won't be spawning another instance of it!",
						    watch_idx,
						    watchConfig[watch_idx].filename,
						    (long) spid,
						    watchConfig[watch_idx].action);
						fbink_printf(FBFD_AUTO,
							     NULL,
							     &fbinkConfig,
							     "[KFMon] Not spawning %s: still running!",
							     basename(watchConfig[watch_idx].action));
					} else if (is_blocker_spawned) {
						LOG(LOG_INFO,
						    "As a spawn blocker process is currently running, we won't be spawning anything else to prevent unwanted behavior!");
						fbink_printf(FBFD_AUTO,
							     NULL,
							     &fbinkConfig,
							     "[KFMon] Not spawning %s: blocked!",
							     basename(watchConfig[watch_idx].action));
					} else if (is_spawn_blocked) {
						LOG(LOG_INFO,
						    "As the global spawn inhibiter flag is present, we won't be spawning anything!");
						fbink_printf(FBFD_AUTO,
							     NULL,
							     &fbinkConfig,
							     "[KFMon] Not spawning %s: inhibited!",
							     basename(watchConfig[watch_idx].action));
					}
				}
			}
			if (event->mask & IN_UNMOUNT) {
				LOG(LOG_NOTICE, "Tripped IN_UNMOUNT for %s", watchConfig[watch_idx].filename);
				// Remember that we encountered an unmount,
				// so we don't try to manually remove watches that are already gone...
				was_unmounted = true;
			}
			// NOTE: Something (badly coalesced/ordered events?) is a bit wonky on the Kobos
			//       when onboard gets unmounted:
			//       we actually never get an IN_UNMOUNT event, only IN_IGNORED...
			//       Another strange behavior is that we get them in a staggered mannered,
			//       and not in one batch, as I do on my sandbox when unmounting a tmpfs...
			//       That may explain why the explicit inotify_rm_watch() calls we do later
			//       on all our other watches don't seem to error out...
			//       In the end, we behave properly, but it's still strange enough to document ;).
			if (event->mask & IN_IGNORED) {
				LOG(LOG_NOTICE, "Tripped IN_IGNORED for %s", watchConfig[watch_idx].filename);
				// Remember that the watch was automatically destroyed so we can break from the loop...
				destroyed_wd                            = true;
				watchConfig[watch_idx].wd_was_destroyed = true;
			}
			if (event->mask & IN_Q_OVERFLOW) {
				if (event->len) {
					LOG(LOG_WARNING, "Huh oh... Tripped IN_Q_OVERFLOW for %s", event->name);
				} else {
					LOG(LOG_WARNING, "Huh oh... Tripped IN_Q_OVERFLOW for... something?");
				}
				// Try to remove the inotify watch we matched
				// (... hoping matching actually was successful), and break the loop.
				LOG(LOG_INFO,
				    "Trying to remove inotify watch for '%s' @ index %hhu.",
				    watchConfig[watch_idx].filename,
				    watch_idx);
				if (inotify_rm_watch(fd, watchConfig[watch_idx].inotify_wd) == -1) {
					// That's too bad, but may not be fatal, so warn only...
					PFLOG(LOG_WARNING, "inotify_rm_watch: %m");
				} else {
					// Flag it as gone if rm was successful
					watchConfig[watch_idx].inotify_wd = -1;
				}
				destroyed_wd                            = true;
				watchConfig[watch_idx].wd_was_destroyed = true;
			}
		}

		// If we caught an unmount, explain why we don't explicitly have to tear down our watches
		if (was_unmounted) {
			LOG(LOG_INFO, "Unmount detected, nothing to do, all watches will naturally get destroyed.");
		}
		// If we caught an event indicating that a watch was automatically destroyed, break the loop.
		if (destroyed_wd) {
			// But before we do that, make sure we've removed *all* our *other* active watches first
			// (again, hoping matching was successful), since we'll be setting them up all again later...
			for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
				if (!watchConfig[watch_idx].is_active) {
					continue;
				}

				if (!watchConfig[watch_idx].wd_was_destroyed) {
					// Don't do anything if that was because of an unmount...
					// Because that assures us that everything is/will soon be gone
					// (since by design, all our target files live on the same mountpoint),
					// even if we didn't get to parse all the events in one go
					// to flag them as destroyed one by one.
					if (!was_unmounted) {
						// Check if that watch index is active to begin with,
						// as we might have just skipped it if its target file was missing...
						if (watchConfig[watch_idx].inotify_wd == -1) {
							LOG(LOG_INFO,
							    "Inotify watch for '%s' @ index %hhu is already inactive!",
							    watchConfig[watch_idx].filename,
							    watch_idx);
						} else {
							// Log what we're doing...
							LOG(LOG_INFO,
							    "Trying to remove inotify watch for '%s' @ index %hhu.",
							    watchConfig[watch_idx].filename,
							    watch_idx);
							if (inotify_rm_watch(fd, watchConfig[watch_idx].inotify_wd) ==
							    -1) {
								// That's too bad, but may not be fatal, so warn only...
								PFLOG(LOG_WARNING, "inotify_rm_watch: %m");
							} else {
								// It's gone!
								watchConfig[watch_idx].inotify_wd = -1;
							}
						}
					}
				} else {
					// Reset the flag to avoid false-positives on the next iteration of the loop,
					// since we re-use the array's content.
					watchConfig[watch_idx].wd_was_destroyed = false;
				}
			}
			break;
		}
	}

	// And we have another outer loop to break, so pass that on...
	return destroyed_wd;
}

// Handle input data from a successful IPC connection (caller breaks on true).
static bool
    handle_ipc(int data_fd)
{
	// Eh, recycle PIPE_BUF, it should be more than enough for our needs.
	char buf[PIPE_BUF] = { 0 };

	// We don't actually know the size of the input data, so, best effort here.
	ssize_t len = xread(data_fd, buf, sizeof(buf) - 1U);
	if (len < 0) {
		// Only actual failures are left, xread handles the rest
		PFLOG(LOG_WARNING, "read: %m");
		fbink_print(FBFD_AUTO, "[KFMon] read failed ?!", &fbinkConfig);
		// Signal our polling to close the connection, don't retry, as we risk failing here again otherwise.
		return true;
	}

	if (len == 0) {
		// EoF, we're done, signal our polling to close the connection
		return true;
	}

	// Handle the supported commands
	if ((strncasecmp(buf, "list", 4) == 0) || (strncasecmp(buf, "gui-list", 8) == 0)) {
		LOG(LOG_INFO, "Processing IPC watch listing request");
		// Discriminate gui-list
		bool gui = (buf[0] == 'g' || buf[0] == 'G');

		// Reply with a list of active watches, format is id:basename(filename):label (separated by a LF)
		//                                             or id:basename(filename) if the watch has no label set.
		for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
			if (!watchConfig[watch_idx].is_active) {
				continue;
			}

			// If it's a gui listing, skip hidden watches
			if (gui && watchConfig[watch_idx].hidden) {
				continue;
			}

			// If it has a label, add it in a third field, otherwise, don't even print the extra field separator.
			int packet_len = 0;
			if (*watchConfig[watch_idx].label) {
				packet_len = snprintf(buf,
						      sizeof(buf),
						      "%hhu:%s:%s\n",
						      watch_idx,
						      basename(watchConfig[watch_idx].filename),
						      watchConfig[watch_idx].label);
			} else {
				packet_len = snprintf(
				    buf, sizeof(buf), "%hhu:%s\n", watch_idx, basename(watchConfig[watch_idx].filename));
			}
			// Make sure we reply with that in full (w/o a NUL, we're not done yet) to the client.
			if (send_in_full(data_fd, buf, (size_t)(packet_len)) < 0) {
				// Only actual failures are left, so we're pretty much done
				if (errno == EPIPE) {
					PFLOG(LOG_WARNING, "Client closed the connection early");
				} else {
					PFLOG(LOG_WARNING, "send: %m");
					fbink_print(FBFD_AUTO, "[KFMon] send failed ?!", &fbinkConfig);
				}
				// Don't retry on write failures, just signal our polling to close the connection
				return true;
			}
			// NOTE: In debug mode, add a delay to test handling of replies split across multiple reads in clients...
#ifdef DEBUG
			const struct timespec zzz = { 0L, 250000000L };
			nanosleep(&zzz, NULL);
#endif
		}
		// Now that we're done, send a final NUL, just to be nice.
		buf[0] = '\0';
		if (send_in_full(data_fd, buf, 1U) < 0) {
			// Only actual failures are left, so we're pretty much done
			if (errno == EPIPE) {
				PFLOG(LOG_WARNING, "Client closed the connection early");
			} else {
				PFLOG(LOG_WARNING, "send: %m");
				fbink_print(FBFD_AUTO, "[KFMon] send failed ?!", &fbinkConfig);
			}
			// Don't retry on write failures, just signal our polling to close the connection
			return true;
		}
	} else if ((strncmp(buf, "start", 5) == 0) || (strncmp(buf, "force-start", 11) == 0) ||
		   (strncmp(buf, "trigger", 7) == 0) || (strncmp(buf, "force-trigger", 13) == 0)) {
		// Discriminate force-*
		bool force = (buf[0] == 'f');
		// Discriminate trigger from start
		bool trigger = (force ? buf[6] == 't' : buf[0] == 't');
		// Pull the actual id out of there. Could have went with strtok, too.
		uint8_t watch_id                       = WATCH_MAX;
		char    watch_basename[CFG_SZ_MAX + 1] = { 0 };
		errno                                  = 0;
		int n                                  = 0;
		if (force) {
			if (trigger) {
				n = sscanf(buf, "force-trigger:%" CFG_SZ_MAX_STR "s", watch_basename);
			} else {
				n = sscanf(buf, "force-start:%hhu", &watch_id);
			}
		} else {
			if (trigger) {
				n = sscanf(buf, "trigger:%" CFG_SZ_MAX_STR "s", watch_basename);
			} else {
				n = sscanf(buf, "start:%hhu", &watch_id);
			}
		}
		// We'll add a courtesy reply with the status
		// NOTE: Actually replying something is *mandatory* in our little IPC "protocol",
		//       as failing to get a reply in time is the only way a client can figure out that KFMon
		//       is already busy with a previous IPC connection...
		int packet_len = 0;
		if (n == 1) {
			// Got it! Now check if it's valid...
			bool found_watch_idx = false;
			for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
				// Needs to be an active watch.
				if (!watchConfig[watch_idx].is_active) {
					continue;
				}

				if (trigger) {
					// trigger looks up by basename(filename)
					if (strcmp(basename(watchConfig[watch_idx].filename), watch_basename) == 0) {
						found_watch_idx = true;
						watch_id        = watch_idx;
						break;
					}
				} else {
					// start looks up by watch_idx
					if (watch_id == watch_idx) {
						found_watch_idx = true;
						break;
					}
				}
			}
			if (!found_watch_idx) {
				// Invalid or inactive watch, can't do anything.
				if (trigger) {
					LOG(LOG_WARNING,
					    "Received a request to %strigger an invalid watch '%s'",
					    force ? "force " : "",
					    watch_basename);
				} else {
					LOG(LOG_WARNING,
					    "Received a request to %sstart an invalid watch idx %hhu",
					    force ? "force " : "",
					    watch_id);
				}
				packet_len = snprintf(buf, sizeof(buf), "ERR_INVALID_ID\n");
			} else {
				// Go ahead, we thankfully have a few less sanity checks to deal with than handle_events,
				// because no SQL ;).
				if (trigger) {
					LOG(LOG_INFO,
					    "Processing IPC request to %strigger watch '%s'",
					    force ? "force " : "",
					    watch_basename);
				} else {
					LOG(LOG_INFO,
					    "Processing IPC request to %sstart watch idx %hhu",
					    force ? "force " : "",
					    watch_id);
				}

				// See handle_events for the logic behind spawn blocking & co.
				bool is_watch_spawned;
				bool is_blocker_spawned;
				pthread_mutex_lock(&ptlock);
				is_watch_spawned   = is_watch_already_spawned(watch_id);
				is_blocker_spawned = is_blocker_running();
				pthread_mutex_unlock(&ptlock);
				bool is_spawn_blocked = are_spawns_blocked();

				// Can't force something that is itself a spawn blocker...
				if (force && watchConfig[watch_id].block_spawns) {
					LOG(LOG_NOTICE,
					    "Dropping the force flag, as the requested watch is a spawn blocker");
					force = false;
				}

				// NOTE: force ignores is_blocker_spawned and is_spawn_blocked
				if ((force && !is_watch_spawned) ||
				    (!force && !is_watch_spawned && !is_blocker_spawned && !is_spawn_blocked)) {
					// Skipping the SQL checks implies we don't need the "may still be processing"
					// logic, either ;).
					LOG(LOG_INFO,
					    "Preparing to spawn %s for watch idx %hhu . . .",
					    watchConfig[watch_id].action,
					    watch_id);
					if (watchConfig[watch_id].block_spawns) {
						LOG(LOG_NOTICE,
						    "%s is flagged as a spawn blocker, it will prevent *any* event from triggering a spawn while it is still running!",
						    watchConfig[watch_id].action);
					}
					// We're using execvp()...
					char* const cmd[] = { watchConfig[watch_id].action, NULL };
					spawn(cmd, watch_id);
					packet_len = snprintf(buf, sizeof(buf), "OK\n");
				} else {
					if (is_watch_spawned) {
						pid_t spid;
						pthread_mutex_lock(&ptlock);
						spid = get_spawn_pid_for_watch(watch_id);
						pthread_mutex_unlock(&ptlock);

						LOG(LOG_INFO,
						    "As watch idx %hhu (%s) still has a spawned process (%ld -> %s) running, we won't be spawning another instance of it!",
						    watch_id,
						    watchConfig[watch_id].filename,
						    (long) spid,
						    watchConfig[watch_id].action);
						fbink_printf(FBFD_AUTO,
							     NULL,
							     &fbinkConfig,
							     "[KFMon] Not spawning %s: still running!",
							     basename(watchConfig[watch_id].action));
						packet_len = snprintf(buf, sizeof(buf), "WARN_ALREADY_RUNNING\n");
					} else if (!force && is_blocker_spawned) {
						LOG(LOG_INFO,
						    "As a spawn blocker process is currently running, we won't be spawning anything else to prevent unwanted behavior!");
						fbink_printf(FBFD_AUTO,
							     NULL,
							     &fbinkConfig,
							     "[KFMon] Not spawning %s: blocked!",
							     basename(watchConfig[watch_id].action));
						packet_len = snprintf(buf, sizeof(buf), "WARN_SPAWN_BLOCKED\n");
					} else if (!force && is_spawn_blocked) {
						LOG(LOG_INFO,
						    "As the global spawn inhibiter flag is present, we won't be spawning anything!");
						fbink_printf(FBFD_AUTO,
							     NULL,
							     &fbinkConfig,
							     "[KFMon] Not spawning %s: inhibited!",
							     basename(watchConfig[watch_id].action));
						packet_len = snprintf(buf, sizeof(buf), "WARN_SPAWN_INHIBITED\n");
					}
				}
			}
		} else if (errno != 0) {
			PFLOG(LOG_WARNING, "sscanf: %m");
			fbink_print(FBFD_AUTO, "[KFMon] sscanf failed ?!", &fbinkConfig);
			if (trigger) {
				packet_len = snprintf(buf,
						      sizeof(buf),
						      "ERR_REALLY_MALFORMED_CMD\nExpected format is %strigger:name\n",
						      force ? "force-" : "");
			} else {
				packet_len = snprintf(buf,
						      sizeof(buf),
						      "ERR_REALLY_MALFORMED_CMD\nExpected format is %sstart:id\n",
						      force ? "force-" : "");
			}
		} else {
			if (trigger) {
				LOG(LOG_WARNING, "Malformed trigger command: %.*s", (int) len, buf);
				packet_len = snprintf(buf,
						      sizeof(buf),
						      "ERR_MALFORMED_CMD\nExpected format is %strigger:name\n",
						      force ? "force-" : "");
			} else {
				LOG(LOG_WARNING, "Malformed start command: %.*s", (int) len, buf);
				packet_len = snprintf(buf,
						      sizeof(buf),
						      "ERR_MALFORMED_CMD\nExpected format is %sstart:id\n",
						      force ? "force-" : "");
			}
		}

		// Reply with the status (w/ NUL)
		if (send_in_full(data_fd, buf, (size_t)(packet_len + 1)) < 0) {
			// Only actual failures are left, so we're pretty much done
			if (errno == EPIPE) {
				PFLOG(LOG_WARNING, "Client closed the connection early");
			} else {
				PFLOG(LOG_WARNING, "send: %m");
				fbink_print(FBFD_AUTO, "[KFMon] send failed ?!", &fbinkConfig);
			}
			// Don't retry on write failures, just signal our polling to close the connection
			return true;
		}
	} else if (strncasecmp(buf, "version", 7) == 0) {
		// Reply with KFMon's short version string.
		int packet_len = snprintf(buf, sizeof(buf), "KFMon %s\n", KFMON_VERSION);

		// w/ NUL
		if (send_in_full(data_fd, buf, (size_t)(packet_len + 1)) < 0) {
			// Only actual failures are left, so we're pretty much done
			if (errno == EPIPE) {
				PFLOG(LOG_WARNING, "Client closed the connection early");
			} else {
				PFLOG(LOG_WARNING, "send: %m");
				fbink_print(FBFD_AUTO, "[KFMon] send failed ?!", &fbinkConfig);
			}
			// Don't retry on write failures, just signal our polling to close the connection
			return true;
		}
	} else if (strncasecmp(buf, "full-version", 12) == 0) {
		// Reply with the full KFMon/SQLite/FBink version string.
		int packet_len = snprintf(buf,
					  sizeof(buf),
					  "KFMon %s (%s) | Using SQLite %s (built against %s) | With FBInk %s\n",
					  KFMON_VERSION,
					  KFMON_TIMESTAMP,
					  sqlite3_libversion(),
					  SQLITE_VERSION,
					  fbink_version());

		// w/ NUL
		if (send_in_full(data_fd, buf, (size_t)(packet_len + 1)) < 0) {
			// Only actual failures are left, so we're pretty much done
			if (errno == EPIPE) {
				PFLOG(LOG_WARNING, "Client closed the connection early");
			} else {
				PFLOG(LOG_WARNING, "send: %m");
				fbink_print(FBFD_AUTO, "[KFMon] send failed ?!", &fbinkConfig);
			}
			// Don't retry on write failures, just signal our polling to close the connection
			return true;
		}
	} else {
		LOG(LOG_WARNING, "Received an invalid/unsupported %zd bytes IPC command: %.*s", len, (int) len, buf);
		// Reply with a list of valid commands, that should be good enough, no need for a full fledged help command.
		int packet_len = snprintf(
		    buf,
		    sizeof(buf),
		    "ERR_INVALID_CMD\nComma separated list of valid commands: version, full-version, list, gui-list, start, force-start, trigger, force-trigger\n");

		// w/ NUL
		if (send_in_full(data_fd, buf, (size_t)(packet_len + 1)) < 0) {
			// Only actual failures are left, so we're pretty much done
			if (errno == EPIPE) {
				PFLOG(LOG_WARNING, "Client closed the connection early");
			} else {
				PFLOG(LOG_WARNING, "send: %m");
				fbink_print(FBFD_AUTO, "[KFMon] send failed ?!", &fbinkConfig);
			}
			// Don't retry on write failures, just signal our polling to close the connection
			return true;
		}
	}

	// Client still has something to say?
	return false;
}

// Dirty little helper to pull the command name for a specific PID from procfs
// Inspired by https://gist.github.com/fclairamb/a16a4237c46440bdb172
static void
    get_process_name(const pid_t pid, char* name)
{
	char procfile[PATH_MAX];
	snprintf(procfile, sizeof(procfile), "/proc/%ld/comm", (long) pid);
	FILE* f = fopen(procfile, "re");
	if (f) {
		size_t size = fread(name, sizeof(*name), 16U - 1U, f);
		if (size > 0) {
			// Strip trailing LF
			if (name[size - 1U] == '\n') {
				name[size - 1U] = '\0';
			}
			// NUL terminate
			char* end = name + size;
			*end      = '\0';
		}
		fclose(f);
	} else {
		// comm is 16 bytes
		str5cpy(name, 16U, "<!>", 16U, TRUNC);
	}
}

// Do the getpwuid_r dance and then just store the name
// NOTE: name is 32 bytes
static void
    get_user_name(const uid_t uid, char* name)
{
	size_t   bufsize;
	long int rc = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (rc == -1) {
		// That's the usual value on Linux
		bufsize = 1024U;
	} else {
		bufsize = (size_t) rc;
	}
	char* buf = alloca(bufsize);

	struct passwd  pwd;
	struct passwd* result;
	int            s = getpwuid_r(uid, &pwd, buf, bufsize, &result);
	if (result == NULL) {
		if (s == 0) {
			// Not found, use the UID...
			snprintf(name, 32, "%ld", (long) uid);
		} else {
			errno = s;
			PFLOG(LOG_WARNING, "getpwnam_r: %m");
			str5cpy(name, 32, "<!>", 32, TRUNC);
		}
	} else {
		str5cpy(name, 32, pwd.pw_name, 32, TRUNC);
	}
}

// Do the getgrgid_r dance and then just store the name
// NOTE: name is 32 bytes
static void
    get_group_name(const gid_t gid, char* name)
{
	size_t   bufsize;
	long int rc = sysconf(_SC_GETGR_R_SIZE_MAX);
	if (rc == -1) {
		// That's the usual value on Linux
		bufsize = 1024U;
	} else {
		bufsize = (size_t) rc;
	}
	char* buf = alloca(bufsize);

	struct group  grp;
	struct group* result;
	int           s = getgrgid_r(gid, &grp, buf, bufsize, &result);
	if (result == NULL) {
		if (s == 0) {
			// Not found, use the GID...
			snprintf(name, 32, "%ld", (long) gid);
		} else {
			errno = s;
			PFLOG(LOG_WARNING, "getgrgid_r: %m");
			str5cpy(name, 32, "<!>", 32, TRUNC);
		}
	} else {
		str5cpy(name, 32, grp.gr_name, 32, TRUNC);
	}
}

// Handle a connection attempt on socket 'conn_fd'.
static void
    handle_connection(int conn_fd)
{
	// Much like handle_events, we need to ensure fb state is consistent...
	pthread_mutex_lock(&ptlock);
	// NOTE: It went fine once, assume that'll still be the case and skip error checking...
	fbink_reinit(FBFD_AUTO, &fbinkConfig);
	pthread_mutex_unlock(&ptlock);

	int data_fd = -1;
	// NOTE: The data fd doesn't inherit the connection socket's flags on Linux.
	do {
		data_fd = accept(conn_fd, NULL, NULL);
	} while (data_fd == -1 && errno == EINTR);
	if (data_fd == -1) {
		if (errno == EAGAIN || errno == ECONNABORTED) {
			// Return early, and let the socket polling trigger a retry or wait for the next connection.
			// NOTE: That seems to be the right call for ECONNABORTED, too.
			//       c.f., Go's Accept() wrapper in src/internal/poll/fd_unix.go
			return;
		}
		PFLOG(LOG_ERR, "Aborting: accept: %m");
		fbink_print(FBFD_AUTO, "[KFMon] accept failed ?!", &fbinkConfig);
		exit(EXIT_FAILURE);
	}
	// We'll also be poll'ing it, so we want it non-blocking, and CLOEXEC.
	// NOTE: We have to do that manually, because accept4 wasn't implemented yet on Mk. 5 kernels...
	//       (The manpage mentions 2.6.28, but that's the *first* implementation (i.e., on x86).
	//       On arm, it was only implemented in 2.6.36 (Mk. 5 run on 2.6.35.3)...
	//       c.f., ports/sysdeps/unix/sysv/linux/arm/kernel-features.h @ glibc).
	int fdflags = fcntl(data_fd, F_GETFD, 0);
	if (fdflags == -1) {
		PFLOG(LOG_WARNING, "getfd fcntl: %m");
		fbink_print(FBFD_AUTO, "[KFMon] fcntl failed ?!", &fbinkConfig);
		goto cleanup;
	}
	if (fcntl(data_fd, F_SETFD, fdflags | FD_CLOEXEC) == -1) {
		PFLOG(LOG_WARNING, "setfd fcntl: %m");
		fbink_print(FBFD_AUTO, "[KFMon] fcntl failed ?!", &fbinkConfig);
		goto cleanup;
	}
	int flflags = fcntl(data_fd, F_GETFL, 0);
	if (flflags == -1) {
		PFLOG(LOG_WARNING, "getfl fcntl: %m");
		fbink_print(FBFD_AUTO, "[KFMon] fcntl failed ?!", &fbinkConfig);
		goto cleanup;
	}
	if (fcntl(data_fd, F_SETFL, flflags | O_NONBLOCK) == -1) {
		PFLOG(LOG_WARNING, "setfl fcntl: %m");
		fbink_print(FBFD_AUTO, "[KFMon] fcntl failed ?!", &fbinkConfig);
		goto cleanup;
	}

	// We'll want to log some information about the client
	// c.f., https://github.com/troydhanson/network/tree/master/unixdomain/03.pass-pid
	struct ucred ucred;
	socklen_t    len = sizeof(ucred);
	if (getsockopt(data_fd, SOL_SOCKET, SO_PEERCRED, &ucred, &len) == -1) {
		PFLOG(LOG_WARNING, "getsockopt: %m");
		fbink_print(FBFD_AUTO, "[KFMon] getsockopt failed ?!", &fbinkConfig);
		goto cleanup;
	}
	// Pull the command name from procfs
	// NOTE: comm is 16 bytes on Linux
	char pname[16] = { 0 };
	get_process_name(ucred.pid, pname);
	// Lookup UID & GID
	// NOTE: Both fields are 32 bytes on Linux
	char uname[32] = { 0 };
	get_user_name(ucred.uid, uname);
	char gname[32] = { 0 };
	get_group_name(ucred.gid, gname);

	// And now we have fancy logging :)
	LOG(LOG_INFO,
	    "Handling incoming IPC connection from PID %ld (%s) by user %s:%s",
	    (long) ucred.pid,
	    pname,
	    uname,
	    gname);

	struct pollfd pfd = { 0 };
	// Data socket
	pfd.fd     = data_fd;
	pfd.events = POLLIN;

	// Wait for data, for a few 15s windows, in order to drop inactive connections after a while
	size_t retry = 0U;
	while (1) {
		int poll_num = poll(&pfd, 1, 15 * 1000);
		if (poll_num == -1) {
			if (errno == EINTR) {
				continue;
			}
			PFLOG(LOG_WARNING, "poll: %m");
			fbink_print(FBFD_AUTO, "[KFMon] poll failed ?!", &fbinkConfig);
			goto early_close;
		}

		if (poll_num > 0) {
			// Don't even *try* to deal with a connection that was closed by the client,
			// as we wouldn't be able to reply to it in handle_ipc (NOSIGNAL send on closed socket -> EPIPE),
			// just close it on our end, too, and move on.
			// NOTE: Said client should already have reported a timeout waiting for our reply,
			//       so we don't even try to drain its command, and just forget about it.
			//       On the upside, that prevents said command from being triggered after a random delay.
			if (pfd.revents & POLLHUP) {
				PFLOG(LOG_NOTICE, "Client closed the IPC connection");
				goto early_close;
			}

			// There's data to be read!
			if (pfd.revents & POLLIN) {
				if (handle_ipc(data_fd)) {
					// We've successfully handled all input data, we're done!
					break;
				}
			}
		}

		if (poll_num == 0) {
			// Timed out, increase the retry counter
			retry++;
		}

		// Drop the axe after 60s.
		if (retry >= 4) {
			LOG(LOG_NOTICE, "Dropping inactive IPC connection");
			goto early_close;
		}
	}

early_close:
	// We're done, close the data connection
	LOG(LOG_INFO, "Closing IPC connection from PID %ld (%s) by user %s:%s", (long) ucred.pid, pname, uname, gname);

cleanup:
	close(data_fd);
}

// Handle SQLite logging on error
static void
    sql_errorlogcb(void* pArg __attribute__((unused)), int iErrCode, const char* zMsg)
{
	if (daemonConfig.use_syslog) {
		syslog(LOG_WARNING, "[*SQL*] %d (%s): %s", iErrCode, sqlite3ErrName(iErrCode), zMsg);
	} else {
		fprintf(stderr,
			"[*SQL*] [%s] [WARN] %d (%s): %s\n",
			get_current_time(),
			iErrCode,
			sqlite3ErrName(iErrCode),
			zMsg);
	}
}

int
    main(int argc __attribute__((unused)), char* argv[] __attribute__((unused)))
{
	// Make sure we're running at a neutral niceness
	// (e.g., being launched via udev would leave us with a negative nice value).
	if (setpriority(PRIO_PROCESS, 0, 0) == -1) {
		PFLOG(LOG_ERR, "Aborting: setpriority: %m");
		exit(EXIT_FAILURE);
	}

	// Fly, little daemon!
	if (daemonize() != 0) {
		LOG(LOG_ERR, "Failed to daemonize, aborting!");
		exit(EXIT_FAILURE);
	}

	// Say hello :)
	LOG(LOG_INFO,
	    "[PID: %ld] Initializing KFMon %s (%s) | Using SQLite %s (built against %s) | With FBInk %s",
	    (long) getpid(),
	    KFMON_VERSION,
	    KFMON_TIMESTAMP,
	    sqlite3_libversion(),
	    SQLITE_VERSION,
	    fbink_version());

	// Load our configs
	if (load_config() == -1) {
		LOG(LOG_ERR, "Failed to load daemon config file(s), aborting!");
		exit(EXIT_FAILURE);
	}

	// Squish stderr if we want to log to the syslog...
	// (can't do that w/ the rest in daemonize, since we don't have our config yet at that point)
	if (daemonConfig.use_syslog) {
		int fd;
		// Redirect stderr (which is now actually our log file) to /dev/null
		if ((fd = open("/dev/null", O_RDWR)) != -1) {
			dup2(fd, fileno(stderr));
			if (fd > 2 + 3) {
				close(fd);
			}
		} else {
			PFLOG(LOG_ERR, "Failed to redirect stderr to /dev/null (open: %m), aborting!");
			exit(EXIT_FAILURE);
		}

		// And connect to the system logger...
		openlog("kfmon", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON);
	}

	// Initialize the process table, to track our spawns
	init_process_table();

	// Initialize FBInk
	init_fbink_config();
	// Consider not being able to print on screen a hard pass...
	// (Mostly, it's to avoid blowing up later in fbink_print).
	if (fbink_init(FBFD_AUTO, &fbinkConfig) != EXIT_SUCCESS) {
		LOG(LOG_ERR, "Failed to initialize FBInk, aborting!");
		exit(EXIT_FAILURE);
	}

	// Initialize SQLite
	if (sqlite3_config(SQLITE_CONFIG_LOG, sql_errorlogcb, NULL) != SQLITE_OK) {
		LOG(LOG_ERR, "Failed to setup SQLite, aborting!");
		exit(EXIT_FAILURE);
	}
	if (sqlite3_initialize() != SQLITE_OK) {
		LOG(LOG_ERR, "Failed to initialize SQLite, aborting!");
		exit(EXIT_FAILURE);
	}

	// Setup the IPC socket
	int conn_fd = -1;
	// NOTE: We want it non-blocking because we handle incoming connections via poll,
	//       and CLOEXEC not to pollute our spawns.
	if ((conn_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) == -1) {
		PFLOG(LOG_ERR, "Failed to create IPC socket (socket: %m), aborting!");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_un sock_name = { 0 };
	sock_name.sun_family         = AF_UNIX;
	str5cpy(sock_name.sun_path, sizeof(sock_name.sun_path), KFMON_IPC_SOCKET, sizeof(sock_name.sun_path), TRUNC);

	// Although we should never trip an existing socket, unlink it first, just to be safe
	unlink(KFMON_IPC_SOCKET);
	if (bind(conn_fd, (const struct sockaddr*) &sock_name, sizeof(sock_name)) == -1) {
		PFLOG(LOG_ERR, "Failed to bind IPC socket (bind: %m), aborting!");
		exit(EXIT_FAILURE);
	}

	// NOTE: We only accept a single client, as we can only serve 'em one by one anyway.
	//       Be aware that, in practice, the kernel will round that up, which means that you can successfully connect,
	//       send a request, but only get a reply whenever we actually get to it...
	if (listen(conn_fd, 1) == -1) {
		PFLOG(LOG_ERR, "Failed to listen to IPC socket (listen: %m), aborting!");
		exit(EXIT_FAILURE);
	}

	// Now that we're properly up, write a pidfile
	FILE* pid_f = fopen(KFMON_PID_FILE, "we");
	if (pid_f) {
		fprintf(pid_f, "%ld\n", (long) getpid());
		fclose(pid_f);
	} else {
		PFLOG(LOG_ERR, "Failed to open pidfile (fopen: %m), aborting!");
		exit(EXIT_FAILURE);
	}

	// NOTE: Because of course we can't have nice things, at this point,
	//       Nickel hasn't finished setting up the fb to its liking. To be fair, it hasn't even started yet ;).
	//       On most devices, the fb is probably in a weird rotation and/or bitdepth at this point.
	//       This has two downsides:
	//       this message (as well as a few others in error paths that might trigger before our first inotify event)
	//       may be slightly broken (meaning badly rotated or positioned),
	//       although FBInk should now mitigate most, if not all, aspects of this particular issue.
	//       But more annoyingly: this means we need to later make sure we have up to date fb info,
	//       an issue we handle via fbink_reinit's heuristics ;).
	//       Thankfully, in most cases, stale info will mostly just mess with positioning,
	//       while completely broken info would only cause the MXCFB ioctl to fail, we wouldn't segfault.
	//       (Well, to be perfectly fair, it'd take an utterly broken finfo.smem_len to crash,
	//       and that should never happen).
	// NOTE: To get up to date info, we'll reinit on each new batch of inotify events we catch,
	//       thus ensuring we'll always have an accurate snapshot of the fb state before printing messages.
	if (daemonConfig.with_notifications) {
		fbink_print(FBFD_AUTO, "[KFMon] Successfully initialized. :)", &fbinkConfig);
	}

	// We pretty much want to loop forever...
	while (1) {
		LOG(LOG_INFO, "Beginning the main loop.");

		// Here, on subsequent iterations, we might be printing stuff *before* handle_events or handle_connection,
		// (mainly in error-ish codepaths), so we need to check the fb state right now, too...
		pthread_mutex_lock(&ptlock);
		// NOTE: It went fine once, assume that'll still be the case and skip error checking...
		fbink_reinit(FBFD_AUTO, &fbinkConfig);
		pthread_mutex_unlock(&ptlock);

		// Make sure our target partition is mounted
		if (!is_target_mounted()) {
			LOG(LOG_INFO, "%s isn't mounted, waiting for it to be . . .", KFMON_TARGET_MOUNTPOINT);
			// If it's not, wait for it to be...
			wait_for_target_mountpoint();
		}

		// Reload *watch* configs to see if we have something new to pickup after an USBMS session
		// NOTE: Mainly up there for clarity, otherwise it technically belongs at the end of the loop.
		//       The only minor drawback of having it up there is that it'll run on startup.
		//       On the upside, this ensures the update codepath will see some action, and isn't completely broken ;).
		if (update_watch_configs() == -1) {
			LOG(LOG_ERR, "Failed to check watch configs for updates, aborting!");
			fbink_print(FBFD_AUTO, "[KFMon] Failed to update watch configs!", &fbinkConfig);
			exit(EXIT_FAILURE);
		}

		// Create the file descriptor for accessing the inotify API
		LOG(LOG_INFO, "Initializing inotify.");
		int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		if (fd == -1) {
			PFLOG(LOG_ERR, "Aborting: inotify_init1: %m");
			fbink_print(FBFD_AUTO, "[KFMon] Failed to initialize inotify!", &fbinkConfig);
			exit(EXIT_FAILURE);
		}

		// Flag each of our target files for 'file was opened' and 'file was closed' events
		// NOTE: We don't check for:
		//       IN_MODIFY: Highly unlikely (and sandwiched between an OPEN and a CLOSE anyway)
		//       IN_CREATE: Only applies to directories
		//       IN_DELETE: Only applies to directories
		//       IN_DELETE_SELF: Will trigger an IN_IGNORED, which we already handle
		//       IN_MOVE_SELF: Highly unlikely on a Kobo, and somewhat annoying to handle with our design
		//           (we'd have to forget about it entirely and not try to re-watch for it
		//           on the next iteration of the loop).
		// NOTE: inotify tracks the file's inode, which means that it goes *through* bind mounts, for instance:
		//           When bind-mounting file 'a' to file 'b', and setting up a watch to the path of file 'b',
		//           you won't get *any* event on that watch when unmounting that bind mount, since the original
		//           file 'a' hasn't actually been touched, and, as it is the actual, real file,
		//           that is what inotify is actually tracking.
		//       Relative to the earlier IN_MOVE_SELF mention, that means it'll keep tracking the file with its
		//           new name (provided it was moved to the *same* fs,
		//           as crossing a fs boundary will delete the original).
		for (uint8_t watch_idx = 0U; watch_idx < WATCH_MAX; watch_idx++) {
			// We obviously only care about active watches
			if (!watchConfig[watch_idx].is_active) {
				continue;
			}

			watchConfig[watch_idx].inotify_wd =
			    inotify_add_watch(fd, watchConfig[watch_idx].filename, IN_OPEN | IN_CLOSE);
			if (watchConfig[watch_idx].inotify_wd == -1) {
				// NOTE: Allow running without an actual inotify watch, keeping the action IPC only...
				//       We could limit this behavior to !hidden watches, or hide it behind another config flag,
				//       but it's harmless enough to do it unconditionally ;).
				//       The watch will be released properly if the *config* file gets removed.
				if (errno == ENOENT) {
					// Only account for ENOENT, though ;) (i.e., filename is gone).
					LOG(LOG_NOTICE,
					    "Setup an IPC-only watch for '%s' @ index %hhu.",
					    basename(watchConfig[watch_idx].filename),
					    watch_idx);
				} else {
					PFLOG(LOG_WARNING, "inotify_add_watch: %m");
					LOG(LOG_WARNING,
					    "Cannot watch '%s', discarding it!",
					    watchConfig[watch_idx].filename);
					fbink_printf(FBFD_AUTO,
						     NULL,
						     &fbinkConfig,
						     "[KFMon] Failed to watch %s!",
						     basename(watchConfig[watch_idx].filename));
					// NOTE: We used to abort entirely in case even one target file couldn't be watched,
					//       but that was a bit harsh ;).
					//       Since the inotify watch couldn't be setup,
					//       there's no way for this to cause trouble down the road,
					//       and this allows the user to fix it during an USBMS session,
					//       instead of having to reboot.

					// If that watch isn't currently running, clear it entirely!
					pthread_mutex_lock(&ptlock);
					bool is_watch_spawned = is_watch_already_spawned(watch_idx);
					pthread_mutex_unlock(&ptlock);
					if (is_watch_spawned) {
						LOG(LOG_WARNING,
						    "Cannot release watch slot %hhu (%s => %s), as it's currently running!",
						    watch_idx,
						    basename(watchConfig[watch_idx].filename),
						    basename(watchConfig[watch_idx].action));
					} else {
						watchConfig[watch_idx] = (const WatchConfig){ 0 };
						// NOTE: This should essentially come down to:
						//memset(&watchConfig[watch_idx], 0, sizeof(WatchConfig));
						LOG(LOG_NOTICE, "Released watch slot %hhu.", watch_idx);
					}
				}
			} else {
				LOG(LOG_NOTICE,
				    "Setup an inotify watch for '%s' @ index %hhu.",
				    watchConfig[watch_idx].filename,
				    watch_idx);
			}
		}

		struct pollfd pfds[2] = { 0 };
		nfds_t        nfds    = 2;
		// Inotify input
		pfds[0].fd     = fd;
		pfds[0].events = POLLIN;
		// Connection socket
		pfds[1].fd     = conn_fd;
		pfds[1].events = POLLIN;

		// Wait for events
		LOG(LOG_INFO, "Listening for events.");
		while (1) {
			int poll_num = poll(pfds, nfds, -1);
			if (poll_num == -1) {
				if (errno == EINTR) {
					continue;
				}
				PFLOG(LOG_ERR, "Aborting: poll: %m");
				fbink_print(FBFD_AUTO, "[KFMon] poll failed ?!", &fbinkConfig);
				exit(EXIT_FAILURE);
			}

			if (poll_num > 0) {
				if (pfds[0].revents & POLLIN) {
					// Inotify events are available
					if (handle_events(fd)) {
						// Go back to the main loop if we exited early (because a watch was
						// destroyed automatically after an unmount or an unlink, for instance)
						break;
					}
				}

				if (pfds[1].revents & POLLIN) {
					// There was a new connection attempt
					handle_connection(conn_fd);
				}
			}
		}
		LOG(LOG_INFO, "Stopped listening for events.");

		// Close inotify file descriptor
		close(fd);
	}

	// Close the IPC connection socket. Unreachable.
	close(conn_fd);
	unlink(KFMON_IPC_SOCKET);
	// Release SQLite resources. Also unreachable ;p.
	sqlite3_shutdown();
	// Why, yes, this is unreachable! Good thing it's also optional ;).
	if (daemonConfig.use_syslog) {
		closelog();
	}

	exit(EXIT_SUCCESS);
}
