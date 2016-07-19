CC=gcc
ifndef TOPLEVEL
	TOPLEVEL=$(shell pwd)
endif

GLIBCFLAGS = $(shell pkg-config glib-2.0 gthread-2.0 gmodule-2.0 --cflags)
GLIBLDFLAGS = $(shell pkg-config glib-2.0 gthread-2.0 gmodule-2.0 --libs)
CFLAGS_GLIB = $(GLIBCFLAGS)

CFLAGS = -I. $(CFLAGS_GLIB) -g
LDFLAGS = -L/usr/local/lib -L. -lwiringPi $(GLIBLDFLAGS)
#LDFLAGS = -L/usr/local/lib -L. -ltcmalloc -lprofiler $(GLIBLDFLAGS)

quiet=@
notquiet=@

%.o:    %.c
	$(notquiet)echo "CC    $@"
	$(quiet)$(CC) -MM $(CFLAGS) $*.c > $*.d
	$(quiet)$(CC) $(CFLAGS) -c $*.c -o $*.o


