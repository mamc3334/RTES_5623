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

all: one_hz

clean:
	-rm -f *.o *.d
	-rm -f one_hz
	-rm -f frames/*

one_hz: one_hz.o
	$(CC) $(CFLAGS) -o $@ $@.o $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $<
