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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <mntent.h>
#include <string.h>
#include <linux/limits.h>
#include <sqlite3.h>

// Do an ifdef check to allow overriding those at compile-time...
#ifndef KFMON_TARGET_MOUNTPOINT
#define KFMON_TARGET_MOUNTPOINT "/mnt/onboard"
#endif
#ifndef KFMON_TARGET_FILE
#define KFMON_TARGET_FILE KFMON_TARGET_MOUNTPOINT "/koreader.png"
#endif
#ifndef KFMON_TARGET_SCRIPT
#define KFMON_TARGET_SCRIPT KFMON_TARGET_MOUNTPOINT "/.adds/koreader/koreader.sh"
#endif
// Use my debug paths on demand...
#ifndef NILUJE
#define KOBO_DB_PATH "/mnt/onboard/.kobo/KoboReader.sqlite"
#define KFMON_LOGFILE "/usr/local/kfmon/kfmon.log"
#else
#define KOBO_DB_PATH "/home/niluje/Kindle/Staging/KoboReader.sqlite"
#define KFMON_LOGFILE "/home/niluje/Kindle/Staging/kfmon.log"
#endif

// Log everything to stderr (which actually points to our logfile)
#define LOG(fmt, ...) fprintf(stderr, "[KFMon] [%s] " fmt "\n", get_current_time(), ## __VA_ARGS__);

// SQLite macros inspired from http://www.lemoda.net/c/sqlite-insert/ :)
#define CALL_SQLITE(f)					\
{							\
	int i;						\
	i = sqlite3_ ## f;				\
	if (i != SQLITE_OK) {				\
		LOG("%s failed with status %d: %s",	\
			#f, i, sqlite3_errmsg(db));	\
		return is_processed;			\
	}						\
}							\

static int daemonize(void);

char *get_current_time(void);

static int is_target_mounted(void);
static void wait_for_target_mountpoint(void);

static unsigned int qhash(const char *, size_t);
static int is_target_processed(int, int);

// Ugly global. Used to remember the pid of our last spawn...
pid_t last_spawn_pid = 0;
static pid_t spawn(char **);
void reaper(int);

static int handle_events(int, int);
