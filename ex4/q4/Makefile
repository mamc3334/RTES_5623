INCLUDE_DIRS = 
LIB_DIRS = 
CC=gcc

CDEFS=
CFLAGS= -O0 -g -Wall $(INCLUDE_DIRS) $(CDEFS)
LDFLAGS = -lrt

HFILES= 
CFILES= continuous_brighten.c

SRCS= ${HFILES} ${CFILES}
OBJS= ${CFILES:.c=.o}

all: continuous_brighten

clean:
	-rm -f *.o *.d
	-rm -f continuous_brighten
	-rm -f frames/*.ppm

continuous_brighten: continuous_brighten.o
	$(CC) $(CFLAGS) -o $@ $@.o $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $<
