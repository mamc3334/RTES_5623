/*************************************
 * RTES 5623 - Real-Time Embedded Systems
 * University of Colorado Boulder
 *
 * Exercise 1 - Question 4 - Part D - Sequencer (SCHED_FIFO)
 * Author: Mason McGaffin
 *
 * Sequencer pattern: a high-priority SCHED_FIFO sequencer thread
 * wakes on every tick (10ms) and releases service threads
 * via semaphores at their correct sub-intervals.  All threads are
 * pinned to CPU 3 so sequencing is enforced on a single core.
 *
 * Services
 *   F10 – C1=10 msec, T1=20 msec, D1=T1
 *   F20 – C2=20 msec, T2=50 msec, D2=T2
 * 
 * LCM(20,50) = 100ms  →  sequencer tick = 10ms, 10 ticks per LCM frame
 *************************************/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <sys/sysinfo.h>

#define APP_TIME_SEC    1           /* SIGALRM fires after this many seconds */
#define SEQ_TICK_NS     (10*1000*1000ULL)   /* 10 ms sequencer tick            */
#define TICKS_PER_LCM   10          /* LCM(20ms,50ms)/10ms = 10        */

#define F10_RUNTIME_NS  (10*1000*1000ULL)   /* 10 ms */
#define F20_RUNTIME_NS  (20*1000*1000ULL)   /* 20 ms */

volatile sig_atomic_t stop = 0;

sem_t sem_f10;
sem_t sem_f20;

uint64_t startTime;

typedef struct {
    const char *name;
    int         cpu;
    int         prio;
    uint64_t    runtime_ns;
    sem_t      *sem;
} params_t;

//Signal handler for clean shutdown on SIGINT, SIGTERM, and SIGALRM
//SIGALARM used by app timer to stop after a fixed duration - APP_TIME_SEC
static void signal_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
        syslog(LOG_CRIT, "Caught signal %d – stopping", sig);
    else if (sig == SIGALRM)
        syslog(LOG_CRIT, "App timer expired – stopping");
    else 
        syslog(LOG_WARNING, "Caught unexpected signal %d", sig);
    stop = 1;
}

// clock monotonic for timestamps
static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

// cputime for busy wait loops 
static uint64_t now_cpu_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static void busy_wait_ns(uint64_t ns)
{
    uint64_t end = now_cpu_ns() + ns;
    while (now_cpu_ns() < end);
}

static void pin_to_core(int core)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0)
    {
        syslog(LOG_WARNING, "setaffinity failed: %s", strerror(errno));
        perror("setaffinity");
    }
}

static void set_fifo_prio(int prio)
{
    struct sched_param sp = 
    {
        .sched_priority = prio
    };

    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
    {
        syslog(LOG_WARNING, "setschedparam(FIFO,%d) failed: %s", prio, strerror(errno));
        perror("setschedparam");
    }
}

static void *sequencer(void *arg)
{
    params_t *p = (params_t *)arg;

    pin_to_core(p->cpu);
    set_fifo_prio(p->prio);

    syslog(LOG_INFO, "SEQ started on CPU %d", sched_getcpu());

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    sem_post(&sem_f10);
    sem_post(&sem_f20);

    unsigned int tick = 0;

    while (!stop)
    {
        /* advance deadline by one tick */
        next.tv_nsec += SEQ_TICK_NS;
        if (next.tv_nsec >= 1000000000L) 
        {
            next.tv_sec  += 1;
            next.tv_nsec -= 1000000000L;
        }

        // sleep until next tick
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        tick++;

        if (stop) break;

        //release F10 every 2 ticks (20ms period) */
        if (tick % 2 == 0)
            sem_post(&sem_f10);

        // release F20 every 5 ticks (50ms period)
        if (tick % 5 == 0)
            sem_post(&sem_f20);

    }

    // wake blocked service threads so they can exit
    sem_post(&sem_f10);
    sem_post(&sem_f20);

    return NULL;
}

static void *service(void *arg)
{
    params_t *p = (params_t *)arg;

    pin_to_core(p->cpu);
    set_fifo_prio(p->prio);

    syslog(LOG_INFO, "%s started on CPU %d", p->name, sched_getcpu());

    unsigned int job = 1;

    while (1)
    {
        sem_wait(p->sem);
        if (stop) break;

        uint64_t release = now_ns() - startTime;
        syslog(LOG_INFO, "%s - JOB %u - RELEASE @ %.3f ms", p->name, job, release / 1e6);

        // busy wait for runtime minus 0.2 ms to ensure we finish before next release
        busy_wait_ns(p->runtime_ns - 200000ULL); 

        uint64_t finish = now_ns() - startTime;
        syslog(LOG_INFO, "%s - JOB %u - COMPLETE @ %.3f ms  (complete %.2f after release)",
               p->name, job, finish / 1e6, (finish - release) / 1e6);
        job++;
    }

    return NULL;
}

int main(void)
{
    openlog("ex1_q4d", LOG_PID, LOG_USER);

    int cpu = sched_getcpu();

    printf("System has %d processors configured and %d available.\n",
           get_nprocs_conf(), get_nprocs());
    printf("Pinning all threads to CPU %d\n", cpu);
    printf("Running for %d seconds. Press Ctrl-C to stop early.\n", APP_TIME_SEC);
    printf("Logs will be written to /var/log/syslog\n");

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGALRM, signal_handler);

    sem_init(&sem_f10, 0, 0);
    sem_init(&sem_f20, 0, 0);

    //get max priority
    int prio_max = sched_get_priority_max(SCHED_FIFO);

    /* Service thread params */
    params_t seq = {"SEQ", cpu, prio_max, 0, NULL};
    params_t f10 = { "F10", cpu, prio_max-1, F10_RUNTIME_NS, &sem_f10 };
    params_t f20 = { "F20", cpu, prio_max-2, F20_RUNTIME_NS, &sem_f20 };

    pthread_t th_seq, th_f10, th_f20;

    /* Create service threads first (they block on semaphores immediately) */
    pthread_create(&th_f10, NULL, service, &f10);
    pthread_create(&th_f20, NULL, service, &f20);

    //sleep for 50 ms to alow service threads to start and block on semaphores before sequencer starts posting
    struct timespec sleep_time = {0, 50000000}; // 50 ms
    nanosleep(&sleep_time, NULL);

    startTime = now_ns();

    // alarm to stop application
    alarm(APP_TIME_SEC);

    /* Sequencer last so it doesn't fire before services are ready */
    pthread_create(&th_seq, NULL, sequencer, &seq);

    pthread_join(th_seq, NULL);
    pthread_join(th_f10, NULL);
    pthread_join(th_f20, NULL);

    sem_destroy(&sem_f10);
    sem_destroy(&sem_f20);
    closelog();
    return 0;
}