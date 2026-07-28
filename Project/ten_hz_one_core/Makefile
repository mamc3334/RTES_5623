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

all: ten_hz

clean:
	-rm -f *.o *.d
	-rm -f ten_hz
	-rm -f frames/*

ten_hz: ten_hz.o
	$(CC) $(CFLAGS) -o $@ $@.o $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $<
