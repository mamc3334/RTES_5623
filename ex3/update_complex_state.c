/*
ECEN 5623 - RTES
Author: Mason McGaffin

Now, using a MUTEX, provide an example using RT-Linux Pthreads that does a thread
safe update of a complex state (3 or more numbers – e.g., Latitude, Longitude and
Altitude of a location) with a timestamp (pthread_mutex_lock). Your code should
include two threads and one should update a timespec structure contained in a structure
that includes a double precision position and attitude state of {Lat, Long, Altitude and
Roll, Pitch, Yaw at Sample_Time} and the other should read it and never disagree on the
values as function of time. You can just make up values for the navigational state using
math library function generators (e.g., use simple periodic functions for Roll, Pitch, Yaw
sin(x), cos(x^2), and cos(x), where x=time and linear functions for Lat, Long, Alt) and see
http://linux.die.net/man/3/clock_gettime for how to add a precision timestamp. The
second thread should read the times-stamped state without the possibility of data
corruption (partial update of one of the 6 floating point values). There should be no
disagreement between the functions and the state reader for any point in time. Run this
for 180 seconds with a 1 Hz update rate and a 0.1 Hz read rate. Make sure the 18 values
read are correct.
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
// #include <sched.h>      // sched_getcpu
// #include <sys/sysinfo.h>


#define WRITE_RATE (1.0) //hz
#define READ_RATE (0.1) //hz
#define DURATION (180) //s
#define TEST_SIZE (18) // 180*0.1
#define READ_INTERVAL (WRITE_RATE/READ_RATE)

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

static bool test_mode = false;
static state_t test_write_state[TEST_SIZE];
static state_t test_read_state[TEST_SIZE];

static double timespec_to_double_s(struct timespec *ts)
{
    return (double)ts->tv_sec + (double)ts->tv_nsec * 1e-9;
}

/**
  * Updates the 
  */
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
    int index = 0;
    int count = 0;

    for (int i = 0; i < write_count; i++) {
        //lock
        pthread_mutex_lock(&shared_state.lock);
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

        if (test_mode){
            count++;

            if(count > READ_INTERVAL - 1) {
                test_write_state[index] = shared_state;
                index++;
                count = 0;
            }
        }

        //unlock
        pthread_mutex_unlock(&shared_state.lock);
        

        sleep(1.0 / WRITE_RATE);
    }
    
    return NULL;
}

void *reader_thread(void *arg)
{
    (void)arg;

    int read_count = DURATION * READ_RATE;

    for (int i = 0; i < read_count; i++) {
        sleep(1.0 / READ_RATE);

        pthread_mutex_lock(&shared_state.lock);

        syslog(LOG_INFO, "[%.4f] READER (CPU %d) - lat=%.6f lon=%.6f alt=%.6f roll=%.6f pitch=%.6f yaw=%.6f",
            timespec_to_double_s(&shared_state.sample_time),
            sched_getcpu(),
            shared_state.lat,
            shared_state.lon,
            shared_state.alt,
            shared_state.roll,
            shared_state.pitch,
            shared_state.yaw);

        if(test_mode)
            test_read_state[i] = shared_state;

        pthread_mutex_unlock(&shared_state.lock);

    }
    
    return NULL;
}

int compare_state(int index, state_t *write_state, state_t *read_state)
{
    // use fabs and epsilon for double comparison - more precise than '=='
    double eps = 1e-9;
    if (fabs(write_state->lat - read_state->lat) > eps ||
        fabs(write_state->lon - read_state->lon) > eps ||
        fabs(write_state->alt - read_state->alt) > eps ||
        fabs(write_state->roll - read_state->roll) > eps ||
        fabs(write_state->pitch - read_state->pitch) > eps ||
        fabs(write_state->yaw - read_state->yaw) > eps) {
        
        printf("[FAILED] TEST %d - State mismatch\n", index);

        printf("WRITE: \tlat=%.6f lon=%.6f alt=%.6f roll=%.6f pitch=%.6f yaw=%.6f\n",
            write_state->lat,
            write_state->lon,
            write_state->alt,
            write_state->roll,
            write_state->pitch,
            write_state->yaw);

        printf("READ: \tlat=%.6f lon=%.6f alt=%.6f roll=%.6f pitch=%.6f yaw=%.6f\n",
            read_state->lat,
            read_state->lon,
            read_state->alt,
            read_state->roll,
            read_state->pitch,
            read_state->yaw);

        return EXIT_FAILURE;
    }

    printf("[SUCCESS] TEST %d - All 6 members match\n", index);
    
    return EXIT_SUCCESS;
}

int compare_tests()
{
    int result = EXIT_SUCCESS;
    
    for(int i=0; i<TEST_SIZE; i++)
    {
        int res = compare_state(i, &test_write_state[i], &test_read_state[i]);

        //continue comparison but fail
        if(res != EXIT_SUCCESS)
            result = res;
    }

    return result;
}

int main(int argc, char** argv)
{
    pthread_t writer;
    pthread_t reader;

    printf("Logging to syslog with identifier 'update_complex_state'\n");
    //start log
    openlog("update_complex_state", LOG_PID, LOG_USER);

    if((argc==2) && strcmp(argv[1], "-t") == 0)
    {
        test_mode = true;
        printf("Beginning update_complex_state test - Duration: %d, Update rate: %.2f, Read rate: %.2f\n", DURATION, WRITE_RATE, READ_RATE);
        syslog(LOG_INFO, "starting complex state update test - Duration: %d, Update rate: %.2f, Read rate: %.2f\n", DURATION, WRITE_RATE, READ_RATE);
    }
    else
    {
        syslog(LOG_INFO, "starting complex state update - Duration: %d, Update rate: %.2f, Read rate: %.2f\n", DURATION, WRITE_RATE, READ_RATE);
    }

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

    int success = EXIT_SUCCESS;
    if(test_mode){
        success = compare_tests();
    }

    syslog(LOG_INFO, "finished complex state update example");
    closelog();

    return success;
}
