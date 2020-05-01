# SPDX-License-Identifier: GPL-3.0-or-later
#
# Pickup our cross-toolchains automatically...
# c.f., http://trac.ak-team.com/trac/browser/niluje/Configs/trunk/Kindle/Misc/x-compile.sh
#       https://github.com/NiLuJe/crosstool-ng
#       https://github.com/koreader/koxtoolchain
# NOTE: We want the "bare" variant of the TC env, to make sure we vendor the right stuff...
#       i.e., source ~SVN/Configs/trunk/Kindle/Misc/x-compile.sh kobo env bare
ifdef CROSS_TC
	CC:=$(CROSS_TC)-gcc
	STRIP:=$(CROSS_TC)-strip
else
	CC?=gcc
	STRIP?=strip
endif

DEBUG_CFLAGS:=-Og -fno-omit-frame-pointer -pipe -g
# Fallback CFLAGS, we honor the env first and foremost!
OPT_CFLAGS:=-O2 -fomit-frame-pointer -pipe

ifdef NILUJE
	# Use the system's sqlite on my sandbox
	# NOTE: We can't easily check the exit code of a command (outside of a recipe), so, instead,
	#       force verbosity to stdout to check the command's output, since it's blank on success.
	ifeq "$(shell pkg-config --exists --print-errors --errors-to-stdout sqlite3)" ""
		LIBS:=$(shell pkg-config --libs sqlite3)
	else
		# Couldn't find SQLite via pkg-config, shit may go horribly wrong...
		LIBS:=-lsqlite3
	endif
	# And the sandbox's custom paths
	EXTRA_CFLAGS+=-DNILUJE
else
	# We want to link to sqlite3 explicitly statically
	LIBS:=-l:libsqlite3.a
endif

# We need our bundled FBInk ;).
LIBS+=-l:libfbink.a

# NOTE: Remember to use gdb -ex 'set follow-fork-mode child' to debug, since we fork like wild bunnies...
ifdef DEBUG
	OUT_DIR:=Debug
	CFLAGS:=$(DEBUG_CFLAGS)
	EXTRA_CFLAGS+=-DDEBUG
else
	OUT_DIR:=Release
	CFLAGS?=$(OPT_CFLAGS)
endif

# Moar warnings!
EXTRA_CFLAGS+=-Wall
EXTRA_CFLAGS+=-Wextra -Wunused
EXTRA_CFLAGS+=-Wformat=2
EXTRA_CFLAGS+=-Wformat-signedness
# NOTE: This doesn't really play nice w/ FORTIFY, leading to an assload of false-positives, unless LTO is enabled
ifneq (,$(findstring flto,$(CFLAGS)))
	EXTRA_CFLAGS+=-Wformat-truncation=2
else
	EXTRA_CFLAGS+=-Wno-format-truncation
endif
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
ifdef DEBUG
	EXTRA_CFLAGS+=-Wpadded
	EXTRA_CFLAGS+=-Wsuggest-attribute=pure -Wsuggest-attribute=const -Wsuggest-attribute=noreturn -Wsuggest-attribute=format -Wmissing-format-attribute
endif
# And disable this, because it obviously doesn't play well with using goto to handle cleanup on error codepaths...
EXTRA_CFLAGS+=-Wno-jump-misses-init
# Spammy when linking SQLite statically
ifndef NILUJE
	EXTRA_CFLAGS+=-Wno-null-dereference
endif

# A version tag...
KFMON_VERSION:=$(shell git describe)
EXTRA_CFLAGS+=-DKFMON_VERSION='"$(KFMON_VERSION)"'
# A timestamp, formatted according to ISO 8601 (latest commit)...
KFMON_TIMESTAMP:=$(shell git show -s --format=%ci)
# NOTE: We used to use __DATE__ @ __TIME__ (i.e., the build date), which we can format the same way like so:
#       date +'%Y-%m-%d %H:%M:%S %z'
#       If, instead, we'd want to emulate __TIMESTAMP__ (i.e., modification date of the file):
#       date +'%Y-%m-%d %H:%M:%S %z' -r kfmon.c
# NOTE: If we ever need to tweak git's formatting:
#       git show -s --format=%cd --date=format:'%Y-%m-%d @ %H:%M:%S %z' master
EXTRA_CFLAGS+=-DKFMON_TIMESTAMP='"$(KFMON_TIMESTAMP)"'

# NOTE: Always use as-needed to avoid unecessary DT_NEEDED entries :)
LDFLAGS?=-Wl,--as-needed

# Pick up our vendored build of SQLite when asked to
ifdef SQLITE
	EXTRA_CPPFLAGS:=-ISQLiteBuild
	EXTRA_LDFLAGS:=-LSQLiteBuild/.libs
endif

# And pick up FBInk, too.
ifdef DEBUG
	EXTRA_LDFLAGS+=-LFBInk/Debug
else
	EXTRA_LDFLAGS+=-LFBInk/Release
	# We already enforce that in SQLite, so, follow suit everywhere
	EXTRA_CPPFLAGS+=-DNDEBUG
endif

# We use pthreads, let GCC do its thing to do it right (c.f., gcc -dumpspecs | grep pthread).
# NOTE: It mostly consists of passing -D_REENTRANT to the preprocessor, -lpthread to the linker,
#       and setting -fprofile-update to prefer-atomic instead of single.
#       Since we're not doing pgo builds, that last one is irrelevant here, which explains why we can safely and simply
#       emulate the autoconf SQLite builds by simply passing -D_REENTRANT to the preprocessor in the sqlite.built recipe.
EXTRA_CPPFLAGS+=-pthread
LIBS+=-lpthread

# And we need -lrt for clock_gettime, as our TC is targeting glibc 2.15, and it was in there before glibc 2.17...
# Yes, Kobo FW have since moved to glibc 2.19, but that's recent (-ish), and we want binaries that will work on earlier FW than that.
LIBS+=-lrt

# We already enforce that in FBInk & KFMon (& SQLite itself probably does, too), so, follow suit everywhere
EXTRA_CPPFLAGS+=-D_GNU_SOURCE


##
# Now that we're done fiddling with flags, let's build stuff!
SRCS:=kfmon.c
# Jump through a few hoops to be able to silence warnings on third-party code only
INIH_SRCS:=inih/ini.c
# We only need str5cpy
STR5_SRCS:=str5/str5cpy.c
# We always need OpenSSH's neat io wrappers
SSH_SRCS:=openssh/atomicio.c
# Keep our old helpers for socket handling around, even if we don't actually use them anymore
SOCK_SRCS:=utils/sock_utils.c

default: vendored

OBJS:=$(addprefix $(OUT_DIR)/, $(SRCS:.c=.o))
INIH_OBJS:=$(addprefix $(OUT_DIR)/, $(INIH_SRCS:.c=.o))
STR5_OBJS:=$(addprefix $(OUT_DIR)/, $(STR5_SRCS:.c=.o))
SSH_OBJS:=$(addprefix $(OUT_DIR)/, $(SSH_SRCS:.c=.o))
SOCK_OBJS:=$(addprefix $(OUT_DIR)/, $(SOCK_SRCS:.c=.o))

# And now we can silence a few inih-specific warnings
$(INIH_OBJS): QUIET_CFLAGS := -Wno-cast-qual

$(OUT_DIR)/%.o: %.c
	$(CC) $(CPPFLAGS) $(EXTRA_CPPFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) $(QUIET_CFLAGS) -o $@ -c $<

outdir:
	mkdir -p $(OUT_DIR)/inih $(OUT_DIR)/str5 $(OUT_DIR)/openssh $(OUT_DIR)/utils

# Make absolutely sure we create our output directories first, even with unfortunate // timings!
# c.f., https://www.gnu.org/software/make/manual/html_node/Prerequisite-Types.html#Prerequisite-Types
$(OBJS): | outdir
$(INIH_OBJS): | outdir
$(STR5_OBJS): | outdir
$(SSH_OBJS): | outdir
$(SOCK_OBJS): | outdir

all: kfmon

vendored: sqlite.built fbink.built
	$(MAKE) kfmon SQLITE=true

kfmon: $(OBJS) $(INIH_OBJS) $(STR5_OBJS) $(SSH_OBJS)
	$(CC) $(CPPFLAGS) $(EXTRA_CPPFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) $(EXTRA_LDFLAGS) -o$(OUT_DIR)/$@$(BINEXT) $(OBJS) $(INIH_OBJS) $(STR5_OBJS) $(SSH_OBJS) $(LIBS)

shim: | outdir
	$(CC) $(CPPFLAGS) $(EXTRA_CPPFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) $(EXTRA_LDFLAGS) -o$(OUT_DIR)/shim$(BINEXT) utils/shim.c
	$(STRIP) --strip-unneeded $(OUT_DIR)/shim

kfmon-ipc: | outdir $(STR5_OBJS) $(SSH_OBJS)
	$(CC) $(CPPFLAGS) $(EXTRA_CPPFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) $(EXTRA_LDFLAGS) -o$(OUT_DIR)/kfmon-ipc$(BINEXT) utils/kfmon-ipc.c $(STR5_OBJS) $(SSH_OBJS)
	$(STRIP) --strip-unneeded $(OUT_DIR)/kfmon-ipc

strip: all
	$(STRIP) --strip-unneeded $(OUT_DIR)/kfmon

armcheck:
ifeq (,$(findstring arm-,$(CC)))
	$(error You forgot to setup a cross TC, you dummy!)
endif

kobo: armcheck release
	mkdir -p Kobo/usr/local/kfmon/bin Kobo/usr/bin Kobo/etc/udev/rules.d Kobo/etc/init.d
	ln -sf $(CURDIR)/scripts/99-kfmon.rules Kobo/etc/udev/rules.d/99-kfmon.rules
	ln -sf $(CURDIR)/scripts/uninstall/kfmon-uninstall.sh Kobo/usr/local/kfmon/bin/kfmon-update.sh
	ln -sf $(CURDIR)/scripts/uninstall/on-animator.sh Kobo/etc/init.d/on-animator.sh
	tar --exclude="./mnt" --exclude="KFMon-*.zip" --owner=root --group=root -cvzhf Release/KoboRoot.tgz -C Kobo .
	pushd Release && zip ../Kobo/KFMon-Uninstaller.zip KoboRoot.tgz && popd
	rm -f Release/KoboRoot.tgz
	rm -rf Kobo/usr/local/kfmon/bin Kobo/etc/udev/rules.d Kobo/etc/init.d
	mkdir -p Kobo/usr/local/kfmon/bin Kobo/mnt/onboard/.kobo Kobo/etc/udev/rules.d Kobo/etc/init.d Kobo/mnt/onboard/.adds/kfmon/config Kobo/mnt/onboard/.adds/kfmon/bin Kobo/mnt/onboard/.adds/kfmon/log Kobo/mnt/onboard/icons
	ln -f $(CURDIR)/resources/koreader.png Kobo/mnt/onboard/koreader.png
	ln -f $(CURDIR)/resources/plato.png Kobo/mnt/onboard/icons/plato.png
	ln -f $(CURDIR)/resources/kfmon.png Kobo/mnt/onboard/kfmon.png
	ln -f $(CURDIR)/Release/kfmon Kobo/usr/local/kfmon/bin/kfmon
	ln -f $(CURDIR)/Release/shim Kobo/usr/local/kfmon/bin/shim
	ln -f $(CURDIR)/Release/kfmon-ipc Kobo/usr/local/kfmon/bin/kfmon-ipc
	ln -sf /usr/local/kfmon/bin/kfmon-ipc Kobo/usr/bin/kfmon-ipc
	ln -f $(CURDIR)/FBInk/Release/fbink Kobo/usr/local/kfmon/bin/fbink
	ln -f $(CURDIR)/README.md Kobo/usr/local/kfmon/README.md
	ln -f $(CURDIR)/LICENSE Kobo/usr/local/kfmon/LICENSE
	ln -f $(CURDIR)/CREDITS Kobo/usr/local/kfmon/CREDITS
	ln -f $(CURDIR)/scripts/99-kfmon.rules Kobo/etc/udev/rules.d/99-kfmon.rules
	ln -f $(CURDIR)/scripts/kfmon-update.sh Kobo/usr/local/kfmon/bin/kfmon-update.sh
	ln -f $(CURDIR)/scripts/on-animator.sh Kobo/etc/init.d/on-animator.sh
	tar --exclude="./mnt" --exclude="KFMon-*.zip" --owner=root --group=root --hard-dereference -cvzf Release/KoboRoot.tgz -C Kobo .
	ln -sf $(CURDIR)/Release/KoboRoot.tgz Kobo/mnt/onboard/.kobo/KoboRoot.tgz
	ln -sf $(CURDIR)/config/kfmon.ini Kobo/mnt/onboard/.adds/kfmon/config/kfmon.ini
	ln -sf $(CURDIR)/config/koreader.ini Kobo/mnt/onboard/.adds/kfmon/config/koreader.ini
	ln -sf $(CURDIR)/config/plato.ini Kobo/mnt/onboard/.adds/kfmon/config/plato.ini
	ln -sf $(CURDIR)/config/kfmon-log.ini Kobo/mnt/onboard/.adds/kfmon/config/kfmon-log.ini
	ln -sf $(CURDIR)/scripts/kfmon-printlog.sh Kobo/mnt/onboard/.adds/kfmon/bin/kfmon-printlog.sh
	pushd Kobo/mnt/onboard && zip -r ../../KFMon-$(KFMON_VERSION).zip . && popd

niluje:
	$(MAKE) fbink.built NILUJE=true
	$(MAKE) all NILUJE=true

nilujed:
	$(MAKE) fbink.built NILUJE=true DEBUG=true
	$(MAKE) all NILUJE=true DEBUG=true

clean:
	rm -rf Release/inih/*.o
	rm -rf Release/str5/*.o
	rm -rf Release/openssh/*.o
	rm -rf Release/utils/*.o
	rm -rf Release/*.o
	rm -rf Release/kfmon
	rm -rf Release/shim
	rm -rf Release/kfmon-ipc
	rm -rf Release/KoboRoot.tgz
	rm -rf Debug/inih/*.o
	rm -rf Debug/str5/*.o
	rm -rf Debug/openssh/*.o
	rm -rf Debug/utils/*.o
	rm -rf Debug/*.o
	rm -rf Debug/kfmon
	rm -rf Debug/shim
	rm -rf Debug/kfmon-ipc
	rm -rf Kobo

sqlite.built:
	mkdir -p SQLiteBuild
	cd sqlite && \
	patch -p1 < ../tools/sqlite-need_err_name.patch && \
	autoreconf -fi && \
	cd ../SQLiteBuild && \
	env CPPFLAGS="$(CPPFLAGS) \
	-DNDEBUG \
	-D_GNU_SOURCE \
	-DSQLITE_DQS=0 \
	-DSQLITE_DEFAULT_MEMSTATUS=0 \
	-DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1 \
	-DSQLITE_LIKE_DOESNT_MATCH_BLOBS \
	-DSQLITE_MAX_EXPR_DEPTH=0 \
	-DSQLITE_OMIT_DECLTYPE \
	-DSQLITE_OMIT_DEPRECATED \
	-DSQLITE_OMIT_PROGRESS_CALLBACK \
	-DSQLITE_OMIT_SHARED_CACHE \
	-DSQLITE_USE_ALLOCA \
	-DSQLITE_OMIT_AUTOINIT \
	-DSQLITE_OMIT_UTF16 \
	-DSQLITE_NEED_ERR_NAME" \
	../sqlite/configure $(if $(CROSS_TC),--host=$(CROSS_TC),) \
	--disable-amalgamation \
	--enable-static \
	--disable-static-shell \
	--disable-shared \
	--disable-threadsafe \
	--disable-load-extension \
	--disable-editline \
	--disable-readline \
	--disable-tcl \
	--enable-tempstore=yes \
	--disable-releasemode && \
	$(MAKE) sqlite3.h libsqlite3.la
	touch sqlite.built

ifdef NILUJE
fbink.built:
	cd FBInk && \
	$(MAKE) strip LINUX=true MINIMAL=true DEBUG=$(DEBUG)
	touch fbink.built
else
fbink.built:
	cd FBInk && \
	$(MAKE) strip KOBO=true MINIMAL=true
	touch fbink.built
endif

release: sqlite.built fbink.built shim kfmon-ipc
	$(MAKE) strip SQLITE=true

debug: sqlite.built
	cd FBInk && \
	$(MAKE) debug KOBO=true MINIMAL=true
	touch fbink.built
	$(MAKE) all DEBUG=true SQLITE=true

fbinkclean:
	cd FBInk && \
	$(MAKE) clean

sqliteclean:
	cd sqlite && \
	git reset --hard && \
	git clean -fxdq

distclean: clean sqliteclean fbinkclean
	rm -rf SQLiteBuild
	rm -rf sqlite.built
	rm -rf fbink.built

.PHONY: default outdir all vendored kfmon shim kfmon-ipc strip armcheck kobo debug niluje nilujed clean release fbinkclean sqliteclean distclean
