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

// Small shim to launch FBInk under a different process name, to make it masquerade as on-animator.sh
// This is basically a hardcoded exec -a invocation, since busybox unfortunately doesn't support that exec flag :/.

// Because we're pretty much Linux-bound ;).
#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

int
    main(void)
{
	// NOTE: Another viable approach to the on-animator death situation would be to make sure we get killed if our parent dies,
	//       which is something we can achieve via a prctl flag:
	//       prctl(PR_SET_PDEATHSIG, SIGTERM);
	//       We'd just need to stick it here, before a standard execv() call, or in FBInk itself.
	//       The least racy approach would probably be here,
	//       because we probably don't want to make that behavior mandatory in FBInk, so we'd have to do it after getopt()...
	//       e.g.,
	//       char* const argv[] = { "/usr/local/kfmon/bin/fbink", "-Z", NULL };
	//       execv(*argv, argv);
	//       And finally, go back to a non-exec call in on-animator.sh ;).

	// We'll launch our own FBInk binary under an assumed name, and with the options necessary to do on-animator's job ;).
	// c.f., https://stackoverflow.com/a/31747301
	// i.e., we just fudge argv[0] to be different from the actual binary filename...
#pragma GCC diagnostic   push
#pragma GCC diagnostic   ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic   ignored "-Wdiscarded-qualifiers"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"
	char* const argv[] = { "on-animator.sh", "-Z", NULL };
#pragma GCC diagnostic pop
	execv("/usr/local/kfmon/bin/fbink", argv);

	return EXIT_SUCCESS;
}
