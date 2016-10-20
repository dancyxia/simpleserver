CC=gcc
ifndef TOPLEVEL
	TOPLEVEL=$(shell pwd)
endif

GLIBCFLAGS = $(shell pkg-config glib-2.0 gthread-2.0 gmodule-2.0 --cflags)
GLIBLDFLAGS = $(shell pkg-config glib-2.0 gthread-2.0 gmodule-2.0 --libs)
CFLAGS_GLIB = $(GLIBCFLAGS)
CFLAGS = -I. -Iinclude/. $(CFLAGS_GLIB) -std=gnu99 -g
LDFLAGS = -L/usr/local/lib -L. $(GLIBLDFLAGS)
ifdef PI
LDFLAGS += -lwiringPi
endif
LDFLAGS += $(GLIBLDFLAGS)
#LDFLAGS = -L/usr/local/lib -L. -ltcmalloc -lprofiler $(GLIBLDFLAGS)

quiet=@
notquiet=@

%.o:    %.c
	$(notquiet)echo "CC    $@"
	$(quiet)$(CC) -MM $(CFLAGS) $*.c > $*.d
	$(quiet)$(CC) $(CFLAGS) -c $*.c -o $*.o


