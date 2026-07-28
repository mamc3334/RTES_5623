INCLUDE_DIRS = 
LIB_DIRS = 
CC=gcc

CDEFS=
CFLAGS= -O0 -g -Wall $(INCLUDE_DIRS) $(CDEFS)
LIBS= 

HFILES= 
CFILES= ex1_q4d.c

SRCS= ${HFILES} ${CFILES}
OBJS= ${CFILES:.c=.o}

all:	ex1_q4d

clean:
	-rm -f *.o *.d
	-rm -f ex1_q4d

ex1_q4d: ex1_q4d.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $@.o -lpthread

.c.o:
	$(CC) $(CFLAGS) -c $<
