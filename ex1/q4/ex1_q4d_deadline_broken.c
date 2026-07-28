/*************************************
    * RTES 5623 - Real-Time Embedded Systems
    * University of Colorado Boulder
    * 
    * Exercise 1 - Question 4 - Part D - Sequencer
    * Author: Mason McGaffin
    *
    * This code is based off the example sequencer/lab1.c
    * provided by Dr. Siewert.
    *
    * However, it was a goal to utilize SCHED_DEADLINE instead of SCHED_FIFO
    * for the main thread and to use syslog for logging instead of printf.

    * The use of SCHED_DEADLINE was not successful due to the inability to set
    * CPU affinity for the threads, which is required to sequence on one core
    * Currently both tasks meet deadlines and execute for desired runtimes
    * but they are not properly sequenced on the same core.

    * The use of syslog was successful and all logging is done through syslog now.
    
**************************************/
#define _GNU_SOURCE
#include <stdio.h>
#include <sys/syscall.h> // for syscall()
#include <sys/sysinfo.h> // for get_nprocs_conf() and get_nprocs()
#include <linux/sched.h> 
#include <pthread.h>
#include <stdint.h>
#include <unistd.h> // for getpid()
#include <time.h> 
#include <syslog.h> // for syslog
#include <signal.h> // for signal handling
#include <stdlib.h> // for system()

#define SCHED_POLICY SCHED_DEADLINE
#define APP_TIME (2) // Run the application for 10 seconds

pthread_t pthreadF10;
pthread_t pthreadF20;

//from deadline.c
struct sched_attr 
{
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

typedef struct service_params
{
    const char *name;
    uint64_t runtime_ns;
    uint64_t deadline_ns;
    uint64_t period_ns;
} service_params;


volatile sig_atomic_t stop = 0;

void signal_handler(int signum) {
   if (signum == SIGINT || signum == SIGTERM)
   {
      syslog(LOG_INFO, "Caught signal, exiting");
      stop = 1;
   }
   else if (signum == SIGALRM)
   {
      syslog(LOG_INFO, "Timer up, exiting");
      stop = 1;
   }
   else
   {
      syslog(LOG_WARNING, "Caught unexpected signal %d", signum);
   }
}

int sched_setattr(pid_t pid, const struct sched_attr *attr, unsigned int flags) 
{
    return syscall(__NR_sched_setattr, pid, attr, flags);
}

uint64_t get_monotonic_ns()
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) perror("clock_gettime");
    return ((uint64_t)(t.tv_sec) * 1000000000LL + (t.tv_nsec));
}

void busy_wait_runtime(uint64_t runtime_ns)
{
    uint64_t start_ns = get_monotonic_ns();
    while(1)
    {
        uint64_t now = get_monotonic_ns();
        if (now - start_ns >= runtime_ns) break;
    }
}

void *service(void *p) 
{
    // This breaks the use of SCHED_DEADLINE because
    // threads cannot be pinned to specific cores with affinity
//    cpu_set_t cpuset;
//
//    CPU_ZERO(&cpuset);
//    CPU_SET(3, &cpuset);
//
//    // pin thread
//    sched_setaffinity(0, sizeof(cpuset), &cpuset);

    service_params *sp = (service_params*)p;

    printf("Service %s running on CPU %d\n", sp->name, sched_getcpu());

    struct sched_attr attr = 
    {
        .size = sizeof (attr),
        .sched_policy = SCHED_POLICY,
	    .sched_flags = 0,
        .sched_runtime = sp->runtime_ns,
        .sched_period = sp->period_ns,
        .sched_deadline = sp->deadline_ns
    };

    if (sched_setattr(0, &attr, 0) != 0) {
        perror("sched_setattr");
        return NULL;
    }

    // uint64_t elapsed_ns;
    unsigned int job = 1;

    while(!stop) 
    {
        uint64_t startTime = get_monotonic_ns();
        syslog(LOG_INFO, "%s JOB %d RELEASE @ %.3f ms\n", sp->name, job, startTime / 1e6);

        busy_wait_runtime(sp->runtime_ns);

        uint64_t finishTime = get_monotonic_ns();
        syslog(LOG_INFO, "%s JOB %d COMPLETE @ %.3f ms\n", sp->name, job, finishTime / 1e6);
        job++;

        sched_yield();
    }
}

int main(int argc, char** argv) 
{
    openlog("ex1_q4d", 0, LOG_USER); // Open syslog

    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGALRM, signal_handler);

    alarm(APP_TIME); // Set a timer to stop application after APP_TIME seconds

    //from professor example
    printf("System has %d processors configured and %d available.\n", get_nprocs_conf(), get_nprocs());

    int main_cpu = sched_getcpu();
    printf("Main thread running on CPU %d\n", main_cpu);

    // Failed attempt at trying to use cgroups to isolate to specific CPU cores,
    // Used AI tools and google to try to get this working but unsuccessful.
    // files are updated but still corrupting sched_setattr() calls 
    // Still sched_setattr is not working
/** char cmd[128];
    snprintf(cmd, sizeof(cmd), "echo %d > /sys/fs/cgroup/ex1q4/cpuset.cpus", main_cpu);
    if(system(cmd) != 0) {
        perror("system");
    }

    snprintf(cmd, sizeof(cmd), "echo %d > /sys/fs/cgroup/ex1q4/cgroup.procs", getpid());
    printf("cmd: %s\n", cmd);
    if (system(cmd) != 0) {
        perror("system");
    }

    sleep(1); // sleep for 1 second to let system commands take effect
    
    printf("Application PID: %d\n", getpid());
*/

    //Affinity does not work with SCHED_DEADLINE
    // pthread_attr_t attr;
    // pthread_attr_init(&attr);
    // pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);

    service_params f10_params = {
        .name = "F10",
        .runtime_ns = 10 * 1000 * 1000, // 10ms
        .deadline_ns = 20 * 1000 * 1000, // 20ms
        .period_ns = 20 * 1000 * 1000 // 20ms
    };

    service_params f20_params = {
        .name = "F20",
        .runtime_ns = 20 * 1000 * 1000, // 20ms
        .deadline_ns = 50 * 1000 * 1000, // 50ms
        .period_ns = 50 * 1000 * 1000 // 50ms
    };

    pthread_create(&pthreadF10, NULL, service, &f10_params);
    pthread_create(&pthreadF20, NULL, service, &f20_params);

    pthread_join(pthreadF10, NULL);
    pthread_join(pthreadF20, NULL);

    return 0;
}
