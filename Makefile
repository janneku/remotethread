CC = gcc
CFLAGS = -W -Wall -O2 -g -fPIC -shared -Iinclude 
EXECFLAGS = -W -Wall -O2 -g -Iinclude 
LDFLAGS = 
PREFIX = /usr
LIBPATH = /usr/lib64

LIB_OBJS = lib.o utils.o
SERVER_OBJS = server.o utils.o
TEST_OBJS = test.o

all:	libremotethread.so remotethread-server test

libremotethread.so:	$(LIB_OBJS)
	$(CC) $(CFLAGS) -Wl,-soname,libremotethread.so -o $@ $(LIB_OBJS)

remotethread-server:	$(SERVER_OBJS)
	$(CC) $(EXECFLAGS) -o $@ $(SERVER_OBJS)

test:	$(TEST_OBJS)
	$(CC) $(EXECFLAGS) -o $@ $(TEST_OBJS) -L. -lremotethread -Wl,-rpath,. -lremotethread

install:	
	mkdir -p -m 755 "$(PREFIX)/lib" "$(PREFIX)/bin"
	install -m 644 include/*.h "$(PREFIX)/include/"
	install -m 644 libremotethread.so "$(LIBPATH)"

clean:	
	rm -f *.o *.so
