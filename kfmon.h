/*
	KFMon: Kobo inotify-based launcher
	Copyright (C) 2016-2024 NiLuJe <ninuje@gmail.com>
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

#ifndef __KFMON_H
#define __KFMON_H

// For syscall, and the expected versions of strerror_r & basename
#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

#include "FBInk/fbink.h"
#include "inih/ini.h"
#include "openssh/atomicio.h"
#include "str5/str5.h"
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <grp.h>
#include <limits.h>
#include <linux/limits.h>
#include <mntent.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

// NOTE: This is pulled from SQLite's *internal* API, here be dragons!
//       c.f., sqlite/src/main.c
extern const char* sqlite3ErrName(int);

// Fallback version tag...
#ifndef KFMON_VERSION
#	define KFMON_VERSION "v1.4.6"
#endif
// Fallback timestamp...
#ifndef KFMON_TIMESTAMP
#	define KFMON_TIMESTAMP __TIMESTAMP__
#endif

// Do an ifdef check to allow overriding those at compile-time...
#ifndef KFMON_TARGET_MOUNTPOINT
#	define KFMON_TARGET_MOUNTPOINT "/mnt/onboard"
#endif
// Use my debug paths on demand...
#ifndef NILUJE
#	define KOBO_DB_PATH     KFMON_TARGET_MOUNTPOINT "/.kobo/KoboReader.sqlite"
#	define KFMON_LOGFILE    "/usr/local/kfmon/kfmon.log"
#	define KFMON_CONFIGPATH KFMON_TARGET_MOUNTPOINT "/.adds/kfmon/config"
#else
#	define KOBO_DB_PATH     "/home/niluje/Kindle/Staging/KoboReader.sqlite"
#	define KFMON_LOGFILE    "/home/niluje/Kindle/Staging/kfmon.log"
#	define KFMON_CONFIGPATH "/home/niluje/Kindle/Staging/kfmon"
#endif

// Path to our pidfile
#define KFMON_PID_FILE "/var/run/kfmon.pid"

// Path to our IPC Unix socket
#define KFMON_IPC_SOCKET "/tmp/kfmon-ipc.ctl"

// MIN/MAX with no side-effects,
// c.f., https://gcc.gnu.org/onlinedocs/cpp/Duplication-of-Side-Effects.html#Duplication-of-Side-Effects
//     & https://dustri.org/b/min-and-max-macro-considered-harmful.html
#define MIN(X, Y)                                                                                                        \
	({                                                                                                               \
		__auto_type x_ = (X);                                                                                    \
		__auto_type y_ = (Y);                                                                                    \
		(x_ < y_) ? x_ : y_;                                                                                     \
	})

#define MAX(X, Y)                                                                                                        \
	({                                                                                                               \
		__auto_type x__ = (X);                                                                                   \
		__auto_type y__ = (Y);                                                                                   \
		(x__ > y__) ? x__ : y__;                                                                                 \
	})

// Fancy ARRAY_SIZE macro, as found in the Linux kernel
// c.f., http://zubplot.blogspot.com/2015/01/gcc-is-wonderful-better-arraysize-macro.html
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#	define GNUC_VERSION          (__GNUC__ << 16) + __GNUC_MINOR__
#	define GNUC_PREREQ(maj, min) (GNUC_VERSION >= ((maj) << 16) + (min))
#else
#	define GNUC_PREREQ(maj, min) 0
#endif

#define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int : -!!(e) * 1234; }))

#if GNUC_PREREQ(3, 1)
#	define SAME_TYPE(a, b)  __builtin_types_compatible_p(typeof(a), typeof(b))
#	define MUST_BE_ARRAY(a) BUILD_BUG_ON_ZERO(SAME_TYPE((a), &(*a)))
#else
#	define MUST_BE_ARRAY(a) BUILD_BUG_ON_ZERO(sizeof(a) % sizeof(*a))
#endif

#ifdef __cplusplus
template<typename T, size_t N>
char (&ARRAY_SIZE_HELPER(T (&array)[N]))[N];
#	define ARRAY_SIZE(array) (sizeof(ARRAY_SIZE_HELPER(array)))
#else
#	define ARRAY_SIZE(a) ((sizeof(a) / sizeof(*a)) + MUST_BE_ARRAY(a))
#endif

// NOTE: See https://kernelnewbies.org/FAQ/DoWhile0 for the reasoning behind the use of GCC's ({ … }) notation
// Log everything to stderr (which actually points to our logfile)
#define LOG(prio, fmt, ...)                                                                                              \
	({                                                                                                               \
		if (daemonConfig.use_syslog) {                                                                           \
			syslog(prio, fmt, ##__VA_ARGS__);                                                                \
		} else {                                                                                                 \
			fprintf(stderr,                                                                                  \
				"[KFMon] [%s] [%s] " fmt "\n",                                                           \
				get_current_time(),                                                                      \
				get_log_prefix(prio),                                                                    \
				##__VA_ARGS__);                                                                          \
		}                                                                                                        \
	})

// Same, but with __PRETTY_FUNCTION__ right before fmt
#define PFLOG(prio, fmt, ...) ({ LOG(prio, "[%s] " fmt, __PRETTY_FUNCTION__, ##__VA_ARGS__); })

// Slight variation without date/time handling to ensure thread safety
#define MTLOG(prio, fmt, ...)                                                                                            \
	({                                                                                                               \
		if (daemonConfig.use_syslog) {                                                                           \
			syslog(prio, fmt, ##__VA_ARGS__);                                                                \
		} else {                                                                                                 \
			fprintf(stderr, "[KFMon] " fmt "\n", ##__VA_ARGS__);                                             \
		}                                                                                                        \
	})

// Same, but with __PRETTY_FUNCTION__ right before fmt
#define PFMTLOG(prio, fmt, ...) ({ MTLOG(prio, "[%s] " fmt, __PRETTY_FUNCTION__, ##__VA_ARGS__); })

// Some extra verbose stuff is relegated to DEBUG builds... (c.f., https://stackoverflow.com/questions/1644868)
#ifdef DEBUG
#	define DEBUG_LOG 1
#else
#	define DEBUG_LOG 0
#endif
#define DBGLOG(fmt, ...)                                                                                                 \
	({                                                                                                               \
		if (DEBUG_LOG) {                                                                                         \
			LOG(LOG_DEBUG, fmt, ##__VA_ARGS__);                                                              \
		}                                                                                                        \
	})

// Likely/Unlikely branch tagging
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// Max length of a text metadata entry in the database (title, author, comment)
#define DB_SZ_MAX      128
// Max filepath length we bother to handle
// NOTE: PATH_MAX is usually set to 4096, which is fairly overkill here...
//       On the other hand, _POSIX_PATH_MAX is always set to 256,
//       and that happens to (roughly) match Windows's MAX_PATH, which, in turn,
//       matches the FAT32 *filename* length limit.
// NOTE: Possibly *very* roughly, as the FAT32 limit is 255 *Unicode* characters, not bytes ;).
//       c.f., https://docs.microsoft.com/de-de/windows/win32/fileio/filesystem-functionality-comparison#limits
//       Since we operate on a FAT32 partition, and we mostly work one or two folder deep into our target mountpoint,
//       a target mountpoint which itself has a relatively short path,
//       we can relatively safely assume that (_POSIX_PATH_MAX * 2) will do the job just fine for our purpose.
//       This is all in order to cadge a (very) tiny amount of stack space...
// NOTE: We mainly use this for snprintf usage with thumbnail paths.
#define KFMON_PATH_MAX (_POSIX_PATH_MAX * 2)
// NOTE: This is all well and good, but, in practice, we load our paths from an ini file via inih,
//       whose default line buffer is 200 bytes. If we chop the NUL, the CR/LF, the key and the equal sign,
//       that would actually leave us somewhere around 186 bytes.
//       Just chop that down to 128 for symmetry, and we'll warn in case user input doesn't fit.
#define CFG_SZ_MAX     128
// For sscanf
#define CFG_SZ_MAX_STR "128"

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
	time_t processing_ts;
	int    inotify_wd;
	char   filename[CFG_SZ_MAX];
	char   action[CFG_SZ_MAX];
	char   label[CFG_SZ_MAX];
	char   db_title[DB_SZ_MAX];
	char   db_author[DB_SZ_MAX];
	char   db_comment[DB_SZ_MAX];
	bool   hidden;
	bool   skip_db_checks;
	bool   do_db_update;
	bool   block_spawns;
	bool   wd_was_destroyed;
	bool   pending_processing;
	bool   is_active;
} WatchConfig;

// Used for thumbnail munging shenanigans
typedef struct
{
	const char* const suffix;
	const char* const description;
} ThumbnailV4;

typedef struct
{
	const char* const munged_file_path;
	const char* const variant;
} ThumbnailV5;

// Hardcode the max amount of watches we handle
// NOTE: Cannot exceed INT8_MAX!
#define WATCH_MAX 16

// Used to keep track of our spawned processes, by storing their pids, and their watch idx.
// c.f., https://stackoverflow.com/a/35235950 & https://stackoverflow.com/a/8976461
// As well as issue #2 for details of past failures w/ a SIGCHLD handler
struct process_table
{
	pid_t  spawn_pids[WATCH_MAX];
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
#define CALL_SQLITE(f)                                                                                                   \
	({                                                                                                               \
		int i;                                                                                                   \
		i = sqlite3_##f;                                                                                         \
		if (i != SQLITE_OK) {                                                                                    \
			LOG(LOG_CRIT, "%s failed with status %d: %s", #f, i, sqlite3_errmsg(db));                        \
			return is_processed;                                                                             \
		}                                                                                                        \
	})

// Remember stdin/stdout/stderr to restore them in our children
int        origStdin;
int        origStdout;
int        origStderr;
static int daemonize(void);

static struct tm*  get_localtime(struct tm* restrict);
static char*       format_localtime(struct tm* restrict, char* restrict, size_t);
static char*       get_current_time(void);
static char*       get_current_time_r(struct tm* restrict, char* restrict, size_t);
static const char* get_log_prefix(int) __attribute__((const));

static bool is_target_mounted(void);
static void wait_for_target_mountpoint(void);

static int    strtoul_hu(const char*, unsigned short int* restrict);
static int    strtobool(const char* restrict, bool* restrict);
static int    daemon_handler(void*, const char* restrict, const char* restrict, const char* restrict);
static int    watch_handler(void*, const char* restrict, const char* restrict, const char* restrict);
static bool   validate_watch_config(void*);
static bool   validate_and_merge_watch_config(void*, uint8_t, bool*);
static int8_t get_next_available_watch_entry(void);
static int    fts_alphasort(const FTSENT**, const FTSENT**);
static int    load_config(void);
static int    update_watch_configs(void);
// Make our config global, because I'm terrible at C.
DaemonConfig  daemonConfig           = { 0 };
WatchConfig   watchConfig[WATCH_MAX] = { 0 };
FBInkConfig   fbinkConfig            = { 0 };
FBInkState    fbinkState             = { 0 };
bool          need_pen_mode          = false;
uint8_t       fwVersion              = 0U;

// NOTE: Unless we're able to tell FBInk to follow the wb's rotation (i.e., with fbdamage's help),
//       we want to bracket our refreshes in "pen" mode on older sunxi kernels (c.f., FBInk/#64 for more details),
//       so handle the switcheroo in a macro to avoid code duplication...
#define FB_PRINT(msg)                                                                                                    \
	({                                                                                                               \
		if (need_pen_mode) {                                                                                     \
			int fbfd = fbink_open();                                                                         \
			fbink_sunxi_toggle_ntx_pen_mode(fbfd, true);                                                     \
                                                                                                                         \
			fbink_print(fbfd, msg, &fbinkConfig);                                                            \
                                                                                                                         \
			fbink_sunxi_toggle_ntx_pen_mode(fbfd, false);                                                    \
                                                                                                                         \
			fbink_close(fbfd);                                                                               \
		} else {                                                                                                 \
			fbink_print(FBFD_AUTO, msg, &fbinkConfig);                                                       \
		}                                                                                                        \
	})

#define FB_PRINTF(fmt, ...)                                                                                              \
	({                                                                                                               \
		if (need_pen_mode) {                                                                                     \
			int fbfd = fbink_open();                                                                         \
			fbink_sunxi_toggle_ntx_pen_mode(fbfd, true);                                                     \
                                                                                                                         \
			fbink_printf(fbfd, NULL, &fbinkConfig, NULL, fmt, ##__VA_ARGS__);                                \
                                                                                                                         \
			fbink_sunxi_toggle_ntx_pen_mode(fbfd, false);                                                    \
                                                                                                                         \
			fbink_close(fbfd);                                                                               \
		} else {                                                                                                 \
			fbink_printf(FBFD_AUTO, NULL, &fbinkConfig, NULL, fmt, ##__VA_ARGS__);                           \
		}                                                                                                        \
	})

// Cute trick from https://stackoverflow.com/a/7618231
#define BOOL2STR(X) ({ ("false\0\0\0true" + 8 * !!(X)); })

static unsigned int qhash(const unsigned char* restrict, size_t);
static bool         check_fw_4x_thumbnails(const unsigned char*, size_t);
static bool         check_fw_5x_thumbnails(const char*, size_t);
static bool         is_target_processed(uint8_t, bool);

static void* reaper_thread(void*);
static pid_t spawn(char* const*, uint8_t);

static bool  is_watch_already_spawned(uint8_t);
static bool  is_blocker_running(void);
static bool  are_spawns_blocked(void);
static pid_t get_spawn_pid_for_watch(uint8_t);

static bool handle_events(int);
static bool handle_ipc(int);
static void get_process_name(const pid_t, char*);
static void get_user_name(const uid_t, char*);
static void get_group_name(const gid_t, char*);
static void handle_connection(int);

static void sql_errorlogcb(void* __attribute__((unused)), int, const char*);

static bool fw_version_check(void);

#endif
