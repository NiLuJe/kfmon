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
#include <mntent.h>
#include <string.h>

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

// Log everything to stderr (which will eventually points to a logfile ;p)
#define LOG(fmt, ...) fprintf(stderr, "[KFMon] [%s] " fmt "\n", get_current_time(), ## __VA_ARGS__);

char *get_current_time(void);
static int is_target_mounted(void);
static void wait_for_target_mountpoint(void);
static void handle_events(int, int);
