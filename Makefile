DEBUG = -g
CC = qcc
LD = qcc


#TARGET = -Vgcc_ntox86_64
#TARGET = -Vgcc_ntox86
#TARGET = -Vgcc_ntoarmv7le
TARGET = -Vgcc_ntoaarch64le


CFLAGS += $(DEBUG) $(TARGET) -Wall
CFLAGS += -IC:\Users\lemac\Downloads\target\qnx7\aarch64le\usr\include\wolfssl\curl
LDFLAGS+= $(DEBUG) $(TARGET) -lm
LDFLAGS += -LC:\Users\lemac\Downloads\target\qnx7\aarch64le\usr\include\wolfssl\curl
LDLIBS += -lcurl
BINS = sensor server
all: $(BINS)

clean:
	rm -f *.o $(BINS);
#	cd solutions; make clean
