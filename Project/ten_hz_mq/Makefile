INCLUDE_DIRS = 
LIB_DIRS = 
CC=gcc

CDEFS=
CFLAGS= -O0 -g -Wall $(INCLUDE_DIRS) $(CDEFS)
LDFLAGS = -lrt

HFILES= 
CFILES= main.c

SRCS= ${HFILES} ${CFILES}
OBJS= ${CFILES:.c=.o}

all: ten_hz_mq

clean:
	-rm -f *.o *.d
	-rm -f ten_hz_mq
	-rm -f frames/*

ten_hz_mq: ten_hz_mq.o
	$(CC) $(CFLAGS) -o $@ $@.o $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $<
