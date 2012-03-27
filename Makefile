# Try to be agnostic to the make version and not go into GNU Make
# Skip a lot of auto rules that aply to gnu make
CC=gcc
LD=gcc
QVDCLIENTLIB=libqvdclient.so
SOURCE=qvdclientcore.c debug.c qvdbuffer.c qvdvm.c
OBJ=qvdclientcore.o debug.o qvdbuffer.o qvdvm.o /usr/local/lib/libcurl.a
QVDCLIENT=qvdclient
QVDCLIENTOBJ=$(QVDCLIENT).o
QVDCLIENTLIBLIBS=-L/usr/local/lib -L. -lcrypto -lssl -lldap -lidn -lrt -ljansson -lXcomp
QVDCLIENTLIBS=-L/usr/local/lib -L. -lqvdclient
CFLAGS=-fPIC -g -I/usr/local/include
STATICLIBS=
STDCLIB=

default: all

all: $(QVDCLIENTLIB) $(QVDCLIENT)

android:
	$(eval LIBDIR:=../android/target)
	$(eval CC=arm-linux-androideabi-gcc)
	$(eval LD=arm-linux-androideabi-gcc)
	$(eval RANLIB=arm-linux-androideabi-ranlib)
	$(eval AR=arm-linux-androideabi-ar)
	$(eval CFLAGS=-fPIC -g -I$(LIBDIR)/curl/include -I$(LIBDIR)/jansson/include -I$(LIBDIR)/nxcomp/include -I$(LIBDIR)/openssl/include)
	$(eval QVDCLIENTLIBS= -L. -lqvdclient -lz -lm -llog)
	$(eval QVDCLIENTLIBLIBS= -L. -lz -lm -llog)
	$(eval STATICLIBS=libcrypto libssl libcurl libjansson libXcomp libpng libjpeg)
	$(eval LIBA=$(LIBDIR)/openssl/lib/libcrypto.a $(LIBDIR)/openssl/lib/libssl.a $(LIBDIR)/curl/lib/libcurl.a $(LIBDIR)/jansson/lib/libjansson.a  $(LIBDIR)/nxcomp/lib/libXcomp.a $(LIBDIR)/png/lib/libpng.a $(LIBDIR)/jpeg/lib/libjpeg.a)
	$(eval LIBSTDC=/usr/local/android-toolchain/arm-linux-androideabi/lib/libstdc++.a)
	$(eval STDCLIB=libstdc)
	@echo android $(CC)
	$(MAKE) -e $(STDCLIB) $(STATICLIBS)
	$(eval OBJ=qvdclientcore.o debug.o qvdbuffer.o qvdvm.o */*.o)
	$(MAKE) -e $(QVDCLIENTLIB) qvdclient 


qvdclient: $(QVDCLIENTOBJ) $(QVDCLIENTLIB)
	$(LD) -fPIC  -o $(QVDCLIENT) $(QVDCLIENTLIBS) $(QVDCLIENTOBJ)

$(QVDCLIENTLIB): $(OBJ)
	$(LD) -shared -o $(QVDCLIENTLIB) $(OBJ) $(QVDCLIENTLIBLIBS)

# These two are for android
$(STDCLIB): $(LIBSTDC)
	mkdir $@
	cd $@; ar x $(LIBSTDC); cd ..

$(STATICLIBS): $(LIBA)
	mkdir -p $@
	cd $@; ar x ../$(LIBDIR)/*/lib/$@.a


clean:
	rm -f *~ *.o

distclean: clean
	rm -rf $(QVDCLIENTLIB) $(QVDCLIENT) core lib*

.EXPORT_ALL_VARIABLES:
