INCLUDE_DIRS = 
LIB_DIRS = 
CC=gcc

CDEFS=
CFLAGS= -O0 -g -Wall $(INCLUDE_DIRS) $(CDEFS)
LDFLAGS = -lrt

HFILES= 
CFILES= cont_sharpen.c seq_sharpen.c

SRCS= ${HFILES} ${CFILES}
OBJS= ${CFILES:.c=.o}

all: cont_sharpen seq_sharpen

clean:
	-rm -f *.o *.d
	-rm -f cont_sharpen seq_sharpen
	-rm -f frames/*.ppm

cont_sharpen: cont_sharpen.o
	$(CC) $(CFLAGS) -o $@ $@.o $(LDFLAGS)

seq_sharpen: seq_sharpen.o
	$(CC) $(CFLAGS) -o $@ $@.o $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $<
