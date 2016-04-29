CC?=gcc
STRIP?=strip
DEBUG_CFLAGS=-O0 -fno-omit-frame-pointer -pipe -g
OPT_CFLAGS=-O2 -fomit-frame-pointer -pipe

SRCS=kfmon.c

default: all

# We want to link to sqlite3 explicitly statically
LIBS=-l:libsqlite3.a -lpthread -ldl -lm
#LIBS=-lsqlite3

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
LDFLAGS?=-Llib

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

debug:
	$(MAKE) all DEBUG=true

clean:
	rm -rf Release/*.o
	rm -rf Release/kfmon
	rm -rf Debug/*.o
	rm -rf Debug/kfmon

.PHONY: all clean default outdir kfmon strip debug
