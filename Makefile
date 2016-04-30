CC?=gcc
STRIP?=strip
DEBUG_CFLAGS=-O0 -fno-omit-frame-pointer -pipe -g
OPT_CFLAGS=-O2 -fomit-frame-pointer -pipe

SRCS=kfmon.c

default: all

# NOTE: For some weird reason, tabs suddenly confuse the hell out of make outside of targets...
ifdef NILUJE
    LIBS=-lsqlite3
    EXTRA_CFLAGS+=-DNILUJE
else
    # We want to link to sqlite3 explicitly statically
    LIBS=-l:libsqlite3.a -lpthread -ldl -lm
endif

ifeq "$(DEBUG)" "true"
    OUT_DIR=Debug
    CFLAGS:=$(DEBUG_CFLAGS)
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

# FIXME: Figure out where we'll end up putting SQLite3 ;p
CPPFLAGS?=-Iincludes
LDFLAGS?=-Llib -Wl,--as-needed

OBJS:=$(SRCS:%.c=$(OUT_DIR)/%.o)

$(OUT_DIR)/%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ -c $<

outdir:
	mkdir -p $(OUT_DIR)

all: outdir kfmon

kfmon: $(OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) -o$(OUT_DIR)/$@$(BINEXT) $(OBJS) $(LIBS)

strip: all
	$(STRIP) $(STRIP_OPTS) $(OUT_DIR)/kfmon

kobo: strip
	mkdir -p Kobo/usr/local/kfmon/bin Kobo/mnt/onboard/.kobo Kobo/etc/udev/rules.d
	ln -sf $(CURDIR)/resources/koreader.png Kobo/mnt/onboard/koreader.png
	ln -sf $(CURDIR)/Release/kfmon Kobo/usr/local/kfmon/bin/kfmon
	ln -sf $(CURDIR)/scripts/99-kfmon.rules Kobo/etc/udev/rules.d/99-kfmon.rules
	cd Kobo && tar -cvzhf KoboRoot.tgz usr etc && cd ..
	ln -sf $(CURDIR)/Kobo/KoboRoot.tgz Kobo/mnt/onboard/.kobo/KoboRoot.tgz
	cd Kobo/mnt/onboard && zip -r ../../KFMon.zip . && cd ../../..

debug:
	$(MAKE) all DEBUG=true

niluje:
	$(MAKE) all NILUJE=true

clean:
	rm -rf Release/*.o
	rm -rf Release/kfmon
	rm -rf Debug/*.o
	rm -rf Debug/kfmon
	rm -rf Kobo

.PHONY: all clean default outdir kfmon kobo strip debug niluje
