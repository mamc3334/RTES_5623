/*
ECEN 5623 - RTES
Author: Mason McGaffin

Based on the code update_complex_state.c

Next, to explore timeouts, use your code from #2 and create a thread that waits on a
MUTEX semaphore for up to 10 seconds and then un-blocks and prints out “No new data
available at <time>” and then loops back to wait for a data update again. Use a variant of
the pthread_mutex_lock called pthread_mutex_timedlock to solve this programming
problem.

Switched so reader waits for 2 seconds before reporting "no new data available"
Write updates data every 2.5 seconds - causing the reader to wait often
*/

#define _GNU_SOURCE
#include <pthread.h>    // threads
#include <time.h>       // time
#include <syslog.h>     // output
#include <unistd.h>     
#include <math.h>       // sin,cos
#include <stdbool.h>    // T/F
#include <stdlib.h>     // EXIT failure, struct
#include <stdio.h>      // printf
#include <string.h>     // strcmp
#include <errno.h>      // ETIMEDOUT
// #include <sched.h>      // sched_getcpu
// #include <sys/sysinfo.h>


#define WRITE_RATE (0.2) //hz
// #define READ_RATE (0.5) //hz
#define READ_TMOUT (4) //s
#define DURATION (180) //s

typedef struct {
    pthread_mutex_t lock;
    struct timespec sample_time;
    double lat;
    double lon;
    double alt;
    double roll;
    double pitch;
    double yaw;
} state_t;

struct timespec time_start;
static state_t shared_state;
volatile static unsigned char running;
volatile static unsigned char new_data;

static double timespec_to_double_s(struct timespec *ts)
{
    return (double)ts->tv_sec + (double)ts->tv_nsec * 1e-9;
}

static void compute_state(state_t *state)
{
    clock_gettime(CLOCK_REALTIME, &state->sample_time);

    double time_elapsed_s = timespec_to_double_s(&state->sample_time) - timespec_to_double_s(&time_start); 

    state->lat = 0.01 * time_elapsed_s;
    state->lon = 0.2 * time_elapsed_s;
    state->alt = 0.25 * time_elapsed_s;
    state->roll = sin(time_elapsed_s);
    state->pitch = cos(time_elapsed_s * time_elapsed_s);
    state->yaw = cos(time_elapsed_s);
}

void *writer_thread(void *arg)
{
    (void)arg;

    int write_count = DURATION * WRITE_RATE;

    for (int i = 0; i < write_count; i++) {
        //lock
        pthread_mutex_lock(&shared_state.lock);

        //sleep for write update  duration
        usleep((1.0 / WRITE_RATE)*1000000);

        compute_state(&shared_state);

        syslog(LOG_INFO, "[%.4f] WRITER (CPU %d) - lat=%.6f lon=%.6f alt=%.6f roll=%.6f pitch=%.6f yaw=%.6f",
            timespec_to_double_s(&shared_state.sample_time),
            sched_getcpu(),
            shared_state.lat,
            shared_state.lon,
            shared_state.alt,
            shared_state.roll,
            shared_state.pitch,
            shared_state.yaw);

        new_data = 1;

        //unlock
        pthread_mutex_unlock(&shared_state.lock);

        //super quick sleep to allow reader to update
        usleep(500);
    }

    running = 0;
    
    return NULL;
}

void *reader_thread(void *arg)
{
    (void)arg;
    int rc;
    struct timespec ts; 

    while(running)
    {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += READ_TMOUT;
        rc = pthread_mutex_timedlock(&shared_state.lock, &ts);

        if(rc == 0)
        {
            if(new_data)
            {
                new_data = 0;
                clock_gettime(CLOCK_REALTIME, &ts);
                syslog(LOG_INFO, "[%.4f] READER (CPU %d) - lat=%.6f lon=%.6f alt=%.6f roll=%.6f pitch=%.6f yaw=%.6f\n",
                        timespec_to_double_s(&ts),
                        sched_getcpu(),
                        shared_state.lat,
                        shared_state.lon,
                        shared_state.alt,
                        shared_state.roll,
                        shared_state.pitch,
                        shared_state.yaw);
            }
            pthread_mutex_unlock(&shared_state.lock);
        }
        else if(rc == ETIMEDOUT)
        {
            clock_gettime(CLOCK_REALTIME, &ts);
            syslog(LOG_INFO, "[%.4f] READER (CPU %d) - No new data available\n", timespec_to_double_s(&ts), sched_getcpu());
        }
    }
    
    return NULL;
}

int main(int argc, char** argv)
{
    pthread_t writer;
    pthread_t reader;

    running = 1;

    printf("Logging to syslog with identifier 'complex_state_watchdog'\n");
    //start log
    openlog("complex_state_watchdog", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "starting complex state update - Duration: %d, Update rate: %.2f, Reader timeout: %ds\n", DURATION, WRITE_RATE, READ_TMOUT);

    //get start time
    clock_gettime(CLOCK_REALTIME, &time_start); 

    // init mutex
    if (0 != pthread_mutex_init(&shared_state.lock, NULL))
    {
        syslog(LOG_ERR, "failed to initialize mutex");

        return EXIT_FAILURE;
    }

    // create worker threads
    if (pthread_create(&writer, NULL, writer_thread, NULL) != 0) {
        syslog(LOG_ERR, "failed to create writer thread");

        return EXIT_FAILURE;
    }

    if (pthread_create(&reader, NULL, reader_thread, NULL) != 0) {
        syslog(LOG_ERR, "failed to create reader thread");
        pthread_join(writer, NULL);
        
        return EXIT_FAILURE;
    }

    pthread_join(writer, NULL);
    pthread_join(reader, NULL);

    //destroy mutex
    pthread_mutex_destroy(&shared_state.lock);

    syslog(LOG_INFO, "finished complex state update example");
    closelog();

    return EXIT_SUCCESS;
}
