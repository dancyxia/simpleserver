all: server

server:  server.o pollserver.o socket.o
	    $(notquiet)echo "CC  $@"
	    $(quiet)$(CC) $(CFLAGS) -o pollserver pollserver.o socket.o $(LDFLAGS) 
	    $(quiet)$(CC) $(CFLAGS) -o epollserver server.o socket.o $(LDFLAGS) 

client: client.o
	    $(notquiet)echo "CC  $@"
	    $(quiet)$(CC) $(CFLAGS) -o client client.o $(LDFLAGS) 
#OBJS = debug.o arraylist.o json_object.o json_tokener.o json_util.o linkhash.o printbuf.o

include Makefile.inc

-include $(OBJS:.o=.d)

clean:
		$(notquiet)echo "  CLEAN $(PWD)"
		$(quiet)rm -f *.a *.o *.so *.d
		$(quiet)rm -rf TAGS

cover test:
	@echo "nothing to $@"
