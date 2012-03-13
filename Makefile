# Try to be agnostic to the make version and not go into GNU Make
# Skip a lot of auto rules that aply to gnu make
CC=gcc
LD=gcc
QVDCLIENTLIB=libqvdclient.so
SOURCE=qvdclientcore.c debug.c qvdbuffer.c qvdvm.c
OBJ=qvdclientcore.o debug.o qvdbuffer.o qvdvm.o /usr/local/lib/libcurl.a
QVDCLIENT=qvdclient
QVDCLIENTOBJ=$(QVDCLIENT).o
QVDCLIENTLIBS=-L/usr/local/lib -L. -lqvdclient -lcrypto -lssl -lldap -lidn -lrt -ljansson -lXcomp
CFLAGS=-fPIC -g -I/usr/local/include

# Agnostic to the make version
#.phony: ALL
all: $(QVDCLIENTLIB) $(QVDCLIENT)

qvdclient: $(QVDCLIENTOBJ) $(QVDCLIENTLIB)
	$(LD) -o $(QVDCLIENT) $(QVDCLIENTLIBS) $(QVDCLIENTOBJ)

$(QVDCLIENTLIB): $(OBJ)
	$(LD) -shared -o $(QVDCLIENTLIB) $(OBJ)

clean:
	rm -f *~ *.o

distclean: clean
	rm -f $(QVDCLIENTLIB) $(QVDCLIENT) core
