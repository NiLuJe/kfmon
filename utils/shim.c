/*
	KFMon: Kobo inotify-based launcher
	Copyright (C) 2016-2019 NiLuJe <ninuje@gmail.com>

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

// Small shim to launch FBInk under a different process name, to make it masquerade as on-animator.sh
// This is basically a hardcoded exec -a invocation, since busybox unfortunately doesn't support that exec flag :/.

// Because we're pretty much Linux-bound ;).
#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
    main(void)
{
	// We'll launch our own FBInk binary under an assumed name, and with the options necessary to do on-animator's job ;).
	// c.f., https://stackoverflow.com/a/31747301
	execl("/usr/local/kfmon/bin/fbink", "on-animator.sh", "-Z", (char*) NULL);

	return EXIT_SUCCESS;
}
