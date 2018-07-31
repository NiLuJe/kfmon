/*
	KFMon: Kobo inotify-based launcher
	Copyright (C) 2016-2018 NiLuJe <ninuje@gmail.com>

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

#ifndef __KFMON_H
#define __KFMON_H

// For syscall, and the expected versions of strerror_r & basename
#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

#include "FBInk/fbink.h"
#include "inih/ini.h"
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <limits.h>
#include <linux/limits.h>
#include <mntent.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

// Fallback version tag...
#ifndef KFMON_VERSION
#	define KFMON_VERSION "v1.2.5"
#endif

// Do an ifdef check to allow overriding those at compile-time...
#ifndef KFMON_TARGET_MOUNTPOINT
#	define KFMON_TARGET_MOUNTPOINT "/mnt/onboard"
#endif
// Use my debug paths on demand...
#ifndef NILUJE
#	define KOBO_DB_PATH KFMON_TARGET_MOUNTPOINT "/.kobo/KoboReader.sqlite"
#	define KFMON_LOGFILE "/usr/local/kfmon/kfmon.log"
#	define KFMON_CONFIGPATH KFMON_TARGET_MOUNTPOINT "/.adds/kfmon/config"
#else
#	define KOBO_DB_PATH "/home/niluje/Kindle/Staging/KoboReader.sqlite"
#	define KFMON_LOGFILE "/home/niluje/Kindle/Staging/kfmon.log"
#	define KFMON_CONFIGPATH "/home/niluje/Kindle/Staging/kfmon"
#endif

// NOTE: See https://kernelnewbies.org/FAQ/DoWhile0 for the reasoning behind the use of GCC's ({ … }) notation
// Log everything to stderr (which actually points to our logfile)
#define LOG(prio, fmt, ...)                                                                                                      \
	({                                                                                                                       \
		if (daemon_config.use_syslog) {                                                                                  \
			syslog(prio, fmt "\n", ##__VA_ARGS__);                                                                   \
		} else {                                                                                                         \
			fprintf(stderr, "[KFMon] [%s] [%s] " fmt "\n", get_current_time(), get_log_prefix(prio), ##__VA_ARGS__); \
		}                                                                                                                \
	})

// Slight variation without date/time handling to ensure thread safety
#define MTLOG(fmt, ...)                                                                                                          \
	({                                                                                                                       \
		if (daemon_config.use_syslog) {                                                                                  \
			syslog(LOG_NOTICE, fmt "\n", ##__VA_ARGS__);                                                             \
		} else {                                                                                                         \
			fprintf(stderr, "[KFMon] " fmt "\n", ##__VA_ARGS__);                                                     \
		}                                                                                                                \
	})

// Some extra verbose stuff is relegated to DEBUG builds... (c.f., https://stackoverflow.com/questions/1644868)
#ifdef DEBUG
#	define DEBUG_LOG 1
#else
#	define DEBUG_LOG 0
#endif
#define DBGLOG(fmt, ...)                                                                                                         \
	({                                                                                                                       \
		if (DEBUG_LOG) {                                                                                                 \
			LOG(LOG_DEBUG, fmt, ##__VA_ARGS__);                                                                      \
		}                                                                                                                \
	})

// Max length of a text metadata entry in the database (title, author, comment)
#define DB_SZ_MAX 128
// Max filepath length we bother to handle
// NOTE: PATH_MAX is usually set to 4096, which is fairly overkill here...
//       On the other hand, _POSIX_PATH_MAX is always set to 256,
//       and that happens to (roughly) match Windows's MAX_PATH, which, in turn,
//       matches the FAT32 *filename* length limit.
//       Since we operate on a FAT32 partition, and we mostly work one or two folder deep into our target mountpoint,
//       a target mountpoint which itself has a relatively short path,
//       we can relatively safely assume that (_POSIX_PATH_MAX * 2) will do the job just fine for our purpose.
//       This is all in order to cadge a (very) tiny amount of stack space...
#define KFMON_PATH_MAX (_POSIX_PATH_MAX * 2)

// What the daemon config should look like
typedef struct
{
	unsigned short int db_timeout;
	bool               use_syslog;
	bool               with_notifications;
} DaemonConfig;

// What a watch config should look like
typedef struct
{
	char filename[KFMON_PATH_MAX];
	char action[KFMON_PATH_MAX];
	char db_title[DB_SZ_MAX];
	char db_author[DB_SZ_MAX];
	char db_comment[DB_SZ_MAX];
	int  inotify_wd;
	bool skip_db_checks;
	bool do_db_update;
	bool block_spawns;
	bool wd_was_destroyed;
} WatchConfig;

// Hardcode the max amount of watches we handle
// NOTE: Cannot exceed INT8_MAX!
#define WATCH_MAX 16

// Used to keep track of our spawned processes, by storing their pids, and their watch idx.
// c.f., https://stackoverflow.com/a/35235950 & https://stackoverflow.com/a/8976461
// As well as issue #2 for details of past failures w/ a SIGCHLD handler
struct process_table
{
	pid_t spawn_pids[WATCH_MAX];
	// NOTE: Needs to be signed because we use -1 as a special value meaning 'available'.
	int8_t spawn_watchids[WATCH_MAX];
} PT;
pthread_mutex_t ptlock = PTHREAD_MUTEX_INITIALIZER;
static void     init_process_table(void);
static int8_t   get_next_available_pt_entry(void);
static void     add_process_to_table(uint8_t, pid_t, uint8_t);
static void     remove_process_from_table(uint8_t);

static void init_fbink_config(void);

// SQLite macros inspired from http://www.lemoda.net/c/sqlite-insert/ :)
#define CALL_SQLITE(f)                                                                                                           \
	({                                                                                                                       \
		int i;                                                                                                           \
		i = sqlite3_##f;                                                                                                 \
		if (i != SQLITE_OK) {                                                                                            \
			LOG(LOG_CRIT, "%s failed with status %d: %s", #f, i, sqlite3_errmsg(db));                                \
			return is_processed;                                                                                     \
		}                                                                                                                \
	})

// Remember stdin/stdout/stderr to restore them in our children
int        orig_stdin;
int        orig_stdout;
int        orig_stderr;
static int daemonize(void);

struct tm*  get_localtime(struct tm*);
char*       format_localtime(struct tm*, char*, size_t);
char*       get_current_time(void);
char*       get_current_time_r(struct tm*, char*, size_t);
const char* get_log_prefix(int) __attribute__((const));

static bool is_target_mounted(void);
static void wait_for_target_mountpoint(void);

static int  strtoul_hu(const char*, unsigned short int*);
static int  strtobool(const char*, bool*);
static int  daemon_handler(void*, const char*, const char*, const char*);
static int  watch_handler(void*, const char*, const char*, const char*);
static bool validate_watch_config(void*);
static int  load_config(void);
// Ugly global. Remember how many watches we set up...
uint8_t watch_count = 0;
// Make our config global, because I'm terrible at C.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
DaemonConfig           daemon_config           = { 0 };
WatchConfig            watch_config[WATCH_MAX] = { 0 };
FBInkConfig            fbink_config            = { 0 };
#pragma GCC diagnostic push

static unsigned int qhash(const unsigned char*, size_t);
static bool         is_target_processed(uint8_t, bool);

void*        reaper_thread(void*);
static pid_t spawn(char* const*, uint8_t);

static bool  is_watch_already_spawned(uint8_t);
static bool  is_blocker_running(void);
static pid_t get_spawn_pid_for_watch(uint8_t);

static bool handle_events(int);

#endif
