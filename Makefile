# Pickup our cross-toolchains automatically...
# c.f., http://trac.ak-team.com/trac/browser/niluje/Configs/trunk/Kindle/Misc/x-compile.sh
#       https://github.com/NiLuJe/crosstool-ng
#       https://github.com/koreader/koxtoolchain
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
	LIBS=-lsqlite3
	LIBS+=-lpthread
	# And the sandbox's custom paths
	EXTRA_CFLAGS+=-DNILUJE
else
	# We want to link to sqlite3 explicitly statically
	LIBS=-l:libsqlite3.a
	# Depending on how SQLite was built, we might need...
	LIBS+=-lpthread
	# And in turn...
	LIBS+=-ldl
	LIBS+=-lm
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
EXTRA_CFLAGS+=-Wall -Wformat -Wformat-security
EXTRA_CFLAGS+=-Wextra -Wunused
EXTRA_CFLAGS+=-Wshadow
EXTRA_CFLAGS+=-Wmissing-prototypes
EXTRA_CFLAGS+=-Wcast-qual
EXTRA_CFLAGS+=-Wcast-align
EXTRA_CFLAGS+=-Wconversion
# Output padding info when debugging (Clang is slightly more verbose)
ifeq "$(DEBUG)" "true"
	EXTRA_CFLAGS+=-Wpadded
endif

# A version tag...
KFMON_VERSION=$(shell git describe)
EXTRA_CFLAGS+=-DKFMON_VERSION='"$(KFMON_VERSION)"'

# NOTE: Always use as-needed to avoid unecessary DT_NEEDED entries with our funky SQLite linking :)
LDFLAGS?=-Wl,--as-needed

# Pick up our vendored build of SQLite when asked to
ifeq "$(SQLITE)" "true"
	EXTRA_CPPFLAGS=-ISQLiteBuild
	EXTRA_LDFLAGS=-LSQLiteBuild/.libs
	# Explicitly ask to link libgcc statically, because it *should* be safe (no C++, so no exceptions/throw).
	# I'm not quite sure why the GCC driver actually wants to pull the shared version on Kobo builds in the first place,
	# since it's only pulling a few conversion/division instrinsics from it...
	# Anyway, enforce using the static version so we pull our actual GCC builtins,
	# since we're using a significantly newer version than the Kobo's system...
	EXTRA_LDFLAGS+=-static-libgcc
endif

# We use pthreads, let GCC do its thing to do it right.
EXTRA_CPPFLAGS+=-pthread


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
	mkdir -p Kobo/usr/local/kfmon/bin Kobo/mnt/onboard/.kobo Kobo/etc/udev/rules.d Kobo/etc/init.d Kobo/mnt/onboard/.adds/kfmon/config
	ln -sf $(CURDIR)/resources/koreader.png Kobo/mnt/onboard/koreader.png
	ln -sf $(CURDIR)/Release/kfmon Kobo/usr/local/kfmon/bin/kfmon
	ln -sf $(CURDIR)/README.md Kobo/usr/local/kfmon/README.md
	ln -sf $(CURDIR)/LICENSE Kobo/usr/local/kfmon/LICENSE
	ln -sf $(CURDIR)/CREDITS Kobo/usr/local/kfmon/CREDITS
	ln -sf $(CURDIR)/scripts/99-kfmon.rules Kobo/etc/udev/rules.d/99-kfmon.rules
	ln -sf $(CURDIR)/scripts/kfmon-update.sh Kobo/usr/local/kfmon/bin/kfmon-update.sh
	ln -sf $(CURDIR)/scripts/on-animator.sh Kobo/etc/init.d/on-animator.sh
	tar --show-transformed-names --transform "s,^Kobo/,./,S" --owner=root --group=root -cvzhf Kobo/KoboRoot.tgz Kobo/usr Kobo/etc
	ln -sf $(CURDIR)/Kobo/KoboRoot.tgz Kobo/mnt/onboard/.kobo/KoboRoot.tgz
	ln -sf $(CURDIR)/config/kfmon.ini Kobo/mnt/onboard/.adds/kfmon/config/kfmon.ini
	ln -sf $(CURDIR)/config/koreader.ini Kobo/mnt/onboard/.adds/kfmon/config/koreader.ini
	pushd Kobo/mnt/onboard && zip -r ../../KFMon-$(KFMON_VERSION).zip . && popd

debug:
	$(MAKE) all DEBUG=true

niluje:
	$(MAKE) all NILUJE=true

nilujed:
	$(MAKE) all NILUJE=true DEBUG=true

clean:
	rm -rf Release/inih/*.o
	rm -rf Release/*.o
	rm -rf Release/kfmon
	rm -rf Debug/inih/*.o
	rm -rf Debug/*.o
	rm -rf Debug/kfmon
	rm -rf Kobo

sqlite.built:
	mkdir -p SQLiteBuild
	cd sqlite && \
	../sqlite-export/create-fossil-manifest && \
	cd ../SQLiteBuild && \
	env CPPFLAGS="$(CPPFLAGS) $(EXTRA_CPPFLAGS)" ../sqlite/configure $(if $(CROSS_TC),--host=$(CROSS_TC),) --enable-static --disable-shared --enable-threadsafe --disable-load-extension --disable-readline --disable-tcl --enable-releasemode && \
	$(MAKE) SHELL_OPT=""
	touch sqlite.built

release: sqlite.built
	$(MAKE) strip SQLITE=true

distclean: clean
	rm -rf SQLiteBuild
	rm -rf sqlite/manifest sqlite/manifest.uuid
	rm -rf sqlite.built

.PHONY: default outdir all kfmon strip kobo debug niluje nilujed clean release distclean
