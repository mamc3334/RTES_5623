INCLUDE_DIRS = 
LIB_DIRS = 
CC=gcc

CDEFS=
CFLAGS= -O0 -g -Wall $(INCLUDE_DIRS) $(CDEFS)
LDFLAGS = -lpthread -lm

HFILES= 
CFILES= update_complex_state.c

SRCS= ${HFILES} ${CFILES}
OBJS= ${CFILES:.c=.o}

all:	update_complex_state

clean:
	-rm -f *.o *.d
	-rm -f update_complex_state

update_complex_state: update_complex_state.o
	$(CC) $(CFLAGS) -o $@ $@.o $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $<
