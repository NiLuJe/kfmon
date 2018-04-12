# Pickup our cross-toolchains automatically...
ifdef CROSS_TC
	CC=$(CROSS_TC)-gcc
	STRIP=$(CROSS_TC)-strip
else
	CC?=gcc
	STRIP?=strip
endif

DEBUG_CFLAGS=-O0 -fno-omit-frame-pointer -pipe -g
OPT_CFLAGS=-O2 -fomit-frame-pointer -pipe

SRCS=kfmon.c inih/ini.c

default: all

# NOTE: For some weird reason, tabs suddenly confuse the hell out of make outside of targets...
ifdef NILUJE
    LIBS=-lsqlite3
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

# A version tag...
KFMON_VERSION=$(shell git describe)
EXTRA_CFLAGS+=-DKFMON_VERSION='"$(KFMON_VERSION)"'

# NOTE: Always use as-needed to avoid unecessary DT_NEEDED entries with our funky SQLite linking :)
LDFLAGS?=-Wl,--as-needed

# Pick up our vendored build of SQLite at worse...
ifeq "$(SQLITE)" "true"
    EXTRA_CPPFLAGS=-ISQLiteBuild
    EXTRA_LDFLAGS=-LSQLiteBuild/.libs
endif

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
	ln -sf $(CURDIR)/scripts/on-animator.sh Kobo/etc/init.d/on-animator.sh
	cd Kobo && tar -cvzhf KoboRoot.tgz usr etc && cd ..
	ln -sf $(CURDIR)/Kobo/KoboRoot.tgz Kobo/mnt/onboard/.kobo/KoboRoot.tgz
	ln -sf $(CURDIR)/config/kfmon.ini Kobo/mnt/onboard/.adds/kfmon/config/kfmon.ini
	ln -sf $(CURDIR)/config/koreader.ini Kobo/mnt/onboard/.adds/kfmon/config/koreader.ini
	cd Kobo/mnt/onboard && zip -r ../../KFMon-$(KFMON_VERSION).zip . && cd ../../..

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
	../sqlite/configure $(if $(CROSS_TC),--host=$(CROSS_TC),) --enable-static --disable-shared --enable-threadsafe --disable-load-extension --disable-readline --disable-tcl --enable-releasemode && \
	$(MAKE) SHELL_OPT=""
	touch sqlite.built

release: sqlite.built
	$(MAKE) strip SQLITE=true

distclean: clean
	rm -rf SQLiteBuild
	rm -rf sqlite/manifest sqlite/manifest.uuid
	rm -rf sqlite.built

.PHONY: default outdir all kfmon strip kobo debug niluje nilujed clean release distclean
