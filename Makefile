# Pickup our cross-toolchains automatically...
# c.f., http://trac.ak-team.com/trac/browser/niluje/Configs/trunk/Kindle/Misc/x-compile.sh
#       https://github.com/NiLuJe/crosstool-ng
#       https://github.com/koreader/koxtoolchain
# NOTE: We want the "bare" variant of the TC env, to make sure we vendor the right stuff...
#       i.e., source ~SVN/Configs/trunk/Kindle/Misc/x-compile.sh kobo env bare
ifdef CROSS_TC
	CC=$(CROSS_TC)-gcc
	STRIP=$(CROSS_TC)-strip
else
	CC?=gcc
	STRIP?=strip
endif

DEBUG_CFLAGS=-Og -fno-omit-frame-pointer -pipe -g
# Fallback CFLAGS, we honor the env first and foremost!
OPT_CFLAGS=-O2 -fomit-frame-pointer -pipe

ifdef NILUJE
	# Use the system's sqlite on my sandbox
	# NOTE: We can't easily check the exit code of a command (outside of a recipe), so, instead,
	#       force verbosity to stdout to check the command's output, since it's blank on success.
	ifeq "$(shell pkg-config --exists --print-errors --errors-to-stdout sqlite3)" ""
		LIBS=$(shell pkg-config --libs sqlite3)
	else
		# Couldn't find SQLite via pkg-config, shit may go horribly wrong...
		LIBS:=-lsqlite3
	endif
	# And the sandbox's custom paths
	EXTRA_CFLAGS+=-DNILUJE
else
	# We want to link to sqlite3 explicitly statically
	LIBS=-l:libsqlite3.a
	# We also want FBInk when targeting real devices ;).
	LIBS+=-l:libfbink.a
endif

# NOTE: Remember to use gdb -ex 'set follow-fork-mode child' to debug, since we fork like wild bunnies...
ifeq "$(DEBUG)" "true"
	OUT_DIR=Debug
	CFLAGS:=$(DEBUG_CFLAGS)
	EXTRA_CFLAGS+=-DDEBUG
else
	OUT_DIR=Release
	CFLAGS?=$(OPT_CFLAGS)
endif

# Moar warnings!
EXTRA_CFLAGS+=-Wall -Wformat=2 -Wformat-signedness -Wformat-truncation=2
EXTRA_CFLAGS+=-Wextra -Wunused
EXTRA_CFLAGS+=-Wnull-dereference
EXTRA_CFLAGS+=-Wuninitialized
EXTRA_CFLAGS+=-Wduplicated-branches -Wduplicated-cond
EXTRA_CFLAGS+=-Wundef
EXTRA_CFLAGS+=-Wbad-function-cast
EXTRA_CFLAGS+=-Wwrite-strings
EXTRA_CFLAGS+=-Wjump-misses-init
EXTRA_CFLAGS+=-Wlogical-op
EXTRA_CFLAGS+=-Wstrict-prototypes -Wold-style-definition
EXTRA_CFLAGS+=-Wshadow
EXTRA_CFLAGS+=-Wmissing-prototypes -Wmissing-declarations
EXTRA_CFLAGS+=-Wnested-externs
EXTRA_CFLAGS+=-Winline
EXTRA_CFLAGS+=-Wcast-qual
# NOTE: GCC 8 introduces -Wcast-align=strict to warn regardless of the target architecture (i.e., like clang)
EXTRA_CFLAGS+=-Wcast-align
EXTRA_CFLAGS+=-Wconversion
# Output padding info when debugging (NOTE: Clang is slightly more verbose)
# As well as function attribute hints
ifeq "$(DEBUG)" "true"
	EXTRA_CFLAGS+=-Wpadded
	EXTRA_CFLAGS+=-Wsuggest-attribute=pure -Wsuggest-attribute=const -Wsuggest-attribute=noreturn -Wsuggest-attribute=format -Wmissing-format-attribute
endif

# A version tag...
KFMON_VERSION=$(shell git describe)
EXTRA_CFLAGS+=-DKFMON_VERSION='"$(KFMON_VERSION)"'

# NOTE: Always use as-needed to avoid unecessary DT_NEEDED entries :)
LDFLAGS?=-Wl,--as-needed

# Pick up our vendored build of SQLite when asked to
ifeq "$(SQLITE)" "true"
	EXTRA_CPPFLAGS=-ISQLiteBuild
	EXTRA_LDFLAGS=-LSQLiteBuild/.libs
	# Explicitly ask to link libgcc statically, because it *should* be safe (no C++, so no exceptions).
	# I'm not quite sure why, on Kobo builds, GCC actually wants to pull the shared version in the first place,
	# since it's only pulling a few conversion/division instrinsics from it...
	# Anyway, enforce using the static version so we pull our actual GCC builtins,
	# since we're using a significantly newer version than the Kobo's system...
	# NOTE: This feels like a terribly stupid idea, GCC probably has a very good reason for pulling the shared version. :D.
	EXTRA_LDFLAGS+=-static-libgcc
endif

# And pick up FBInk, too.
ifndef NILUJE
	ifeq "$(DEBUG)" "true"
		EXTRA_LDFLAGS+=-LFBInk/Debug
	else
		EXTRA_LDFLAGS+=-LFBInk/Release
	endif
endif

# We use pthreads, let GCC do its thing to do it right (c.f., gcc -dumpspecs | grep pthread).
# NOTE: It mostly consists of passing -D_REENTRANT to the preprocessor, -lpthread to the linker,
#       and setting -fprofile-update to prefer-atomic instead of single.
#       Since we're not doing pgo builds, that last one is irrelevant here, which explains why we can safely and simply
#       emulate the autoconf SQLite builds by simply passing -D_REENTRANT to the preprocessor in the sqlite.built recipe.
EXTRA_CPPFLAGS+=-pthread
LIBS+=-lpthread


##
# Now that we're done fiddling with flags, let's build stuff!
SRCS=kfmon.c inih/ini.c

default: all

OBJS:=$(SRCS:%.c=$(OUT_DIR)/%.o)

$(OUT_DIR)/%.o: %.c
	$(CC) $(CPPFLAGS) $(EXTRA_CPPFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ -c $<

outdir:
	mkdir -p $(OUT_DIR)/inih

all: outdir kfmon

kfmon: $(OBJS)
	$(CC) $(CPPFLAGS) $(EXTRA_CPPFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) $(EXTRA_LDFLAGS) -o$(OUT_DIR)/$@$(BINEXT) $(OBJS) $(LIBS)

strip: all
	$(STRIP) --strip-unneeded $(OUT_DIR)/kfmon

kobo: release
	mkdir -p Kobo/usr/local/kfmon/bin Kobo/mnt/onboard/.kobo Kobo/etc/udev/rules.d Kobo/etc/init.d Kobo/mnt/onboard/.adds/kfmon/config Kobo/mnt/onboard/.adds/kfmon/bin
	ln -sf $(CURDIR)/resources/koreader.png Kobo/mnt/onboard/koreader.png
	ln -sf $(CURDIR)/resources/kfmon.png Kobo/mnt/onboard/kfmon.png
	ln -sf $(CURDIR)/Release/kfmon Kobo/usr/local/kfmon/bin/kfmon
	ln -sf $(CURDIR)/FBInk/Release/fbink Kobo/usr/local/kfmon/bin/fbink
	ln -sf $(CURDIR)/README.md Kobo/usr/local/kfmon/README.md
	ln -sf $(CURDIR)/LICENSE Kobo/usr/local/kfmon/LICENSE
	ln -sf $(CURDIR)/CREDITS Kobo/usr/local/kfmon/CREDITS
	ln -sf $(CURDIR)/scripts/99-kfmon.rules Kobo/etc/udev/rules.d/99-kfmon.rules
	ln -sf $(CURDIR)/scripts/kfmon-update.sh Kobo/usr/local/kfmon/bin/kfmon-update.sh
	ln -sf $(CURDIR)/scripts/on-animator.sh Kobo/etc/init.d/on-animator.sh
	tar --exclude="./mnt" --exclude="KFMon-*.zip" --owner=root --group=root -cvzhf Release/KoboRoot.tgz -C Kobo .
	ln -sf $(CURDIR)/Release/KoboRoot.tgz Kobo/mnt/onboard/.kobo/KoboRoot.tgz
	ln -sf $(CURDIR)/config/kfmon.ini Kobo/mnt/onboard/.adds/kfmon/config/kfmon.ini
	ln -sf $(CURDIR)/config/koreader.ini Kobo/mnt/onboard/.adds/kfmon/config/koreader.ini
	ln -sf $(CURDIR)/config/kfmon-log.ini Kobo/mnt/onboard/.adds/kfmon/config/kfmon-log.ini
	ln -sf $(CURDIR)/scripts/kfmon-printlog.sh Kobo/mnt/onboard/.adds/kfmon/bin/kfmon-printlog.sh
	pushd Kobo/mnt/onboard && zip -r ../../KFMon-$(KFMON_VERSION).zip . && popd


niluje:
	$(MAKE) all NILUJE=true

nilujed:
	$(MAKE) all NILUJE=true DEBUG=true

clean:
	rm -rf Release/inih/*.o
	rm -rf Release/*.o
	rm -rf Release/kfmon
	rm -rf Release/KoboRoot.tgz
	rm -rf Debug/inih/*.o
	rm -rf Debug/*.o
	rm -rf Debug/kfmon
	rm -rf Kobo

sqlite.built:
	mkdir -p SQLiteBuild
	cd sqlite && \
	../sqlite-export/create-fossil-manifest && \
	cd ../SQLiteBuild && \
	env CPPFLAGS="$(CPPFLAGS) -D_REENTRANT=1" ../sqlite/configure $(if $(CROSS_TC),--host=$(CROSS_TC),) --enable-static --disable-shared --enable-threadsafe --disable-load-extension --disable-readline --disable-tcl --enable-releasemode && \
	$(MAKE) SHELL_OPT=""
	touch sqlite.built

fbink.built:
	cd FBInk && \
	$(MAKE) strip MINIMAL=true
	touch fbink.built

release: sqlite.built fbink.built
	$(MAKE) strip SQLITE=true

debug: sqlite.built fbink.built
	cd FBInk && \
	$(MAKE) debug
	$(MAKE) all DEBUG=true SQLITE=true

fbinkclean:
	cd FBInk && \
	$(MAKE) clean

distclean: clean fbinkclean
	rm -rf SQLiteBuild
	rm -rf sqlite/manifest sqlite/manifest.uuid
	rm -rf sqlite.built
	rm -rf fbink.built

.PHONY: default outdir all kfmon strip kobo debug niluje nilujed clean release fbinkclean distclean
