LDFLAGS+=-L$(SDKSTAGE)/opt/vc/lib/ -lGLESv2 -lEGL -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lavformat -lavutil -lavcodec

INC=-I$(SDKSTAGE)/opt/vc/include/ -I$(SDKSTAGE)/opt/vc/include/interface/vcos/pthreads

CFLAGS+=-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi -O2 -std=c99 $(INC)


BIN=omxtranscode

OBJS=demux.o encode.o main.o omx.o packet_queue.o video.o 

all: $(BIN)

$(BIN): $(OBJS)
	gcc $(LDFLAGS) -o $(BIN) $(OBJS)

vcodec_omx.o: vcodec_omx.c vcodec_omx.h codec.h
	$(CC) $(INCLUDES) $(CFLAGS) -c -o vcodec_omx.o vcodec_omx.c

clean:
	rm -f $(BIN) $(OBJS)
