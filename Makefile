all: socket

socket:  server.o pollserver.o
	    $(notquiet)echo "CC  $@"
	    $(quiet)$(CC) $(CFLAGS) -o socket1 server.o $(LDFLAGS) 
	    $(quiet)$(CC) $(CFLAGS) -o socket2 pollserver.o $(LDFLAGS) 

#OBJS = debug.o arraylist.o json_object.o json_tokener.o json_util.o linkhash.o printbuf.o

include Makefile.inc

-include $(OBJS:.o=.d)

clean:
		$(notquiet)echo "  CLEAN $(PWD)"
		$(quiet)rm -f *.a *.o *.so *.d
		$(quiet)rm -rf TAGS

cover test:
	@echo "nothing to $@"
