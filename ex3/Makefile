INCLUDE_DIRS = 
LIB_DIRS = 
CC=gcc

CDEFS=
CFLAGS= -O0 -g -Wall $(INCLUDE_DIRS) $(CDEFS)
LDFLAGS = -lpthread -lm

HFILES= 
CFILES= update_complex_state.c priority_inversion_fix.c complex_state_watchdog.c

SRCS= ${HFILES} ${CFILES}
OBJS= ${CFILES:.c=.o}

all:	update_complex_state priority_inversion_fix complex_state_watchdog

clean:
	-rm -f *.o *.d
	-rm -f update_complex_state priority_inversion_fix complex_state_watchdog

update_complex_state: update_complex_state.o
	$(CC) $(CFLAGS) -o $@ $@.o $(LDFLAGS)

priority_inversion_fix: priority_inversion_fix.o
	$(CC) $(CFLAGS) -o $@ $@.o $(LDFLAGS)

complex_state_watchdog: complex_state_watchdog.o
	$(CC) $(CFLAGS) -o $@ $@.o $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $<
