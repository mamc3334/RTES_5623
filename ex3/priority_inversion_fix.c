/* 
This code is based on the example RTES-ECEE-5623-mcgaffin/example-sync-updated-2/pthread3amp.c

The original file is super messy and has the following issues

//  The `pthread3amp.c` code had a bug that the low priority task calls `simple_task` and then gets stuck after spawning the thread waiting for it to enter
//  the critical section, but this code doesn't use `criticalsection_task` for the low priority thread, so it just infinitely spin locks in the main thread after
//  completing the low priority task, preventing the medium and high priority tasks from even running (i'm assuming this was not the intent). 

//  The intent for this code is to use priority amplification to break the unbounded inversion.

Author: Mason McGaffin
ECEN 5623 - RTES
*/


#define _GNU_SOURCE

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <pthread.h>
#include <stdio.h>
#include <sched.h>
#include <time.h>
#include <stdlib.h>

#define NUM_THREADS		4
#define START_SERVICE 		0
#define HIGH_PRIO_SERVICE 	1
#define MID_PRIO_SERVICE 	2
#define LOW_PRIO_SERVICE 	3
#define CS_LENGTH 		10


// 1 for priority inheritance. 0 for priority ceiling emulation
// #define PRIO_PROTOCOL PTHREAD_PRIO_PROTECT
#define PRIO_PROTOCOL PTHREAD_PRIO_INHERIT

#define HIGH_PRIORITY 50
#define MID_PRIORITY  20
#define LOW_PRIORITY  10


pthread_t threads[NUM_THREADS];
pthread_attr_t rt_sched_attr;
// pthread_attr_t nrt_sched_attr;
int rt_max_prio, rt_min_prio;
struct sched_param rt_param;
struct sched_param nrt_param;

typedef struct
{
    int threadIdx;
} threadParams_t;

threadParams_t threadParams[NUM_THREADS];

pthread_mutex_t sharedMemSem;
pthread_mutexattr_t sharedMemSemAttr;

volatile int runInterference=0, CScnt=0;
volatile unsigned idleCount[NUM_THREADS];
int intfTime=0;

int numberOfProcessors;
struct timespec timeNow, timeStartTest;

unsigned const int fibLength = 47;  // if number is too large, unsigned will overflow
unsigned const int fibComputeSequences = 100000;   // to add more time, increase iterations

// Helper functions
void fibCycleBurner(unsigned seqCnt, unsigned iterCnt, int traceOn);
void *startService(void *threadid);
double dTime(struct timespec now, struct timespec start);
void print_scheduler(void);

// function entry points for 2 tasks used in this demonstration
void *simpleTask(void *threadp);
void *criticalSectionTask(void *threadp);


int main (int argc, char *argv[])
{
    int rc, scope, semProtocol, semCeiling;
    cpu_set_t threadcpu;

    if(argc < 2)
    {
        printf("Usage: pthread interfere-seconds\n");
        exit(-1);
    }
    else if(argc >= 2)
    {
        sscanf(argv[1], "%d", &intfTime);
        printf("interference time = %d secs\n", intfTime);
        printf("unsafe mutex will be created\n");
    }

    //    rc=sched_getparam(getpid(), &nrt_param);

    
    printf("Fibonacci Cycle Burner test ...\n");
    fibCycleBurner(47, 2, 1);
    printf("\ndone\n");


    rt_max_prio = sched_get_priority_max(SCHED_FIFO);
    rt_min_prio = sched_get_priority_min(SCHED_FIFO);

    printf("Setting affinity to core 0\n");
    CPU_ZERO(&threadcpu);
    CPU_SET(0, &threadcpu);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &threadcpu);

    pthread_attr_init(&rt_sched_attr);
    pthread_attr_setinheritsched(&rt_sched_attr, PTHREAD_EXPLICIT_SCHED);
    rc = pthread_attr_setschedpolicy(&rt_sched_attr, SCHED_FIFO);
    if (rc) 
    {
        printf("ERROR - run with sudo; sched_setscheduler rc is %d\n", rc);
        perror(NULL);
        exit(-1);
    }

    print_scheduler();

    pthread_attr_setaffinity_np(&rt_sched_attr, sizeof(cpu_set_t), &threadcpu);

    //    pthread_attr_init(&nrt_sched_attr);
    //    pthread_attr_setinheritsched(&nrt_sched_attr, PTHREAD_EXPLICIT_SCHED);
    //    pthread_attr_setschedpolicy(&nrt_sched_attr, SCHED_RR);


    printf("min prio = %d, max prio = %d\n", rt_min_prio, rt_max_prio);
    pthread_attr_getscope(&rt_sched_attr, &scope);

    printf("pthread_attr_getscope is ");
    if(scope == PTHREAD_SCOPE_SYSTEM)
        printf("PTHREAD SCOPE SYSTEM\n");
    else if (scope == PTHREAD_SCOPE_PROCESS)
        printf("PTHREAD SCOPE PROCESS\n");
    else
        printf("PTHREAD SCOPE UNKNOWN\n");

    pthread_mutexattr_init(&sharedMemSemAttr);

    rc=pthread_mutexattr_setprotocol(&sharedMemSemAttr, PRIO_PROTOCOL);

    if (rc < 0)
    {
        printf("ERROR; pthread_mutexattr_setprotocol() or getprotocol rc is %d\n", rc);
        perror(NULL);
        exit(-1);
    }
    else
    {
        printf("******************sharedMemSem attributes set\n");
        pthread_mutexattr_getprotocol(&sharedMemSemAttr, &semProtocol);
        printf("pthread_mutexattr_getprotocol is ");
        if(semProtocol == PTHREAD_PRIO_NONE) printf("PTHREAD_PRIO_NONE\n");
        else if(semProtocol == PTHREAD_PRIO_INHERIT) printf("PTHREAD_PRIO_INHERIT\n");
        else if(semProtocol == PTHREAD_PRIO_PROTECT) 
        {
            printf("PTHREAD_PRIO_PROTECT\n");
            rc=pthread_mutexattr_setprioceiling(&sharedMemSemAttr, HIGH_PRIORITY);
            if (rc < 0)
            {
                printf("ERROR; pthread_mutexattr_setprioceiling rc is %d\n", rc);
                perror(NULL);
                exit(-1);
            }
            else
            {
                pthread_mutexattr_getprioceiling(&sharedMemSemAttr, &semCeiling);
                printf("pthread_mutexattr_getprioceiling is %d\n", semCeiling);
            }
        }
        else printf("PTHREAD_PRIO_UNKNOWN\n");
    }

    // Init mutex with specified attribute
    pthread_mutex_init(&sharedMemSem, &sharedMemSemAttr);

    //Allow starter to be max priority
    rt_param.sched_priority = rt_max_prio;
    pthread_attr_setschedparam(&rt_sched_attr, &rt_param);

    printf("\nCreating RT STARTER SERVICE thread %d\n", START_SERVICE);
    threadParams[START_SERVICE].threadIdx=START_SERVICE;
    rc = pthread_create(&threads[START_SERVICE], &rt_sched_attr, startService, (void *)&threadParams[START_SERVICE]);
    if (rc)
    {
        printf("ERROR - run with sudo; pthread_create() rc is %d\n", rc);
        perror(NULL);
        exit(-1);
    }
    printf("Start services thread spawned. Waiting ...\n");

    if(pthread_join(threads[START_SERVICE], NULL) == 0)
        printf("START SERVICE joined\n");
    else
        perror("START SERVICE");


    if(pthread_mutex_destroy(&sharedMemSem) != 0)
    perror("mutex destroy");

    printf("All threads done\n");

    exit(0);
}


void *startService(void *threadid)
{
    int rc, busyWaitCnt=0;

    runInterference=intfTime;
    clock_gettime(CLOCK_REALTIME, &timeStartTest);

    // CREATE L Thread as lowest prio RT thread and make sure it enters the C.S. before starting H
    rt_param.sched_priority = LOW_PRIORITY;
    pthread_attr_setschedparam(&rt_sched_attr, &rt_param);

    printf("\nCreating LOW PRIO SERVICE thread %d\n", LOW_PRIO_SERVICE);
    threadParams[LOW_PRIO_SERVICE].threadIdx=LOW_PRIO_SERVICE;
    rc = pthread_create(&threads[LOW_PRIO_SERVICE], &rt_sched_attr, criticalSectionTask, (void *)&threadParams[LOW_PRIO_SERVICE]);

    if (rc)
    {
        printf("ERROR - run with sudo; pthread_create() rc is %d\n", rc);
        perror(NULL);
        exit(-1);
    }

    clock_gettime(CLOCK_REALTIME, &timeNow);
    printf("Low prio %d thread SPAWNED at %lf sec\n", LOW_PRIO_SERVICE, dTime(timeNow, timeStartTest));

    // spin until L enters the critical section
    while(CScnt < 1)
    {
        busyWaitCnt++; 
        if((busyWaitCnt % 10000) == 0) printf(".");
        usleep(10); //yield cpu so L can enter critical section
    }
    printf("CScnt=%d\n", CScnt);


    // CREATE H Thread as RT thread at highest priority, but it will block on C.S. semaphore held by L until
    // L finishes the C.S.
    //
    rt_param.sched_priority = HIGH_PRIORITY;
    pthread_attr_setschedparam(&rt_sched_attr, &rt_param);

    printf("\nCreating HIGH PRIO SERVICE thread %d, CScnt=%d\n", HIGH_PRIO_SERVICE, CScnt);
    threadParams[HIGH_PRIO_SERVICE].threadIdx=HIGH_PRIO_SERVICE;
    //rc = pthread_create(&threads[HIGH_PRIO_SERVICE], &rt_sched_attr, simpleTask, (void *)&threadParams[HIGH_PRIO_SERVICE]);
    rc = pthread_create(&threads[HIGH_PRIO_SERVICE], &rt_sched_attr, criticalSectionTask, (void *)&threadParams[HIGH_PRIO_SERVICE]);

    if (rc)
    {
        printf("ERROR - run with sudo; pthread_create() rc is %d\n", rc);
        perror(NULL);
        exit(-1);
    }
    //pthread_detach(threads[HIGH_PRIO_SERVICE]);
    clock_gettime(CLOCK_REALTIME, &timeNow);
    printf("High prio %d thread SPAWNED at %lf sec\n", HIGH_PRIO_SERVICE, dTime(timeNow, timeStartTest));

    
    // CREATE M Thread as RT thread at any lower priority than H, but higher than L so that L is interfered with and
    // cannot complete the C.S. until M is done with any amount of computation (unbounded).
    //
    if(runInterference > 0)
    {
        rt_param.sched_priority = MID_PRIORITY;
        pthread_attr_setschedparam(&rt_sched_attr, &rt_param);

        printf("\nCreating MID PRIO SERVICE thread %d\n", MID_PRIO_SERVICE);
        threadParams[MID_PRIO_SERVICE].threadIdx=MID_PRIO_SERVICE;
        rc = pthread_create(&threads[MID_PRIO_SERVICE], &rt_sched_attr, simpleTask, (void *)&threadParams[MID_PRIO_SERVICE]);
        if (rc)
        {
            printf("ERROR - run with sudo; pthread_create() rc is %d\n", rc);
            perror(NULL);
            exit(-1);
        }

        clock_gettime(CLOCK_REALTIME, &timeNow);
        printf("Middle prio %d thread SPAWNED at %lf sec\n", MID_PRIO_SERVICE, dTime(timeNow, timeStartTest));
    }

    if(pthread_join(threads[HIGH_PRIO_SERVICE], NULL) == 0)
        printf("HIGH PRIO joined\n");
    else
        perror("HIGH PRIO");

    if(runInterference > 0)
    {
        if(pthread_join(threads[MID_PRIO_SERVICE], NULL) == 0)
            printf("MID PRIO joined\n");
        else
            perror("MID PRIO");
    }


    if(pthread_join(threads[LOW_PRIO_SERVICE], NULL) == 0)
        printf("LOW PRIO joined\n");
    else
        perror("LOW PRIO");


    pthread_exit(NULL);

}

//helpers unchanged

double dTime(struct timespec now, struct timespec start)
{
    double nowReal=0.0, startReal=0.0;

    nowReal = (double)now.tv_sec + ((double)now.tv_nsec / 1000000000.0);
    startReal = (double)start.tv_sec + ((double)start.tv_nsec / 1000000000.0);

    return (nowReal-startReal);
}


void fibCycleBurner(unsigned seqCnt, unsigned iterCnt, int traceOn)
{
    volatile unsigned int fib = 0, fib0 = 0, fib1 = 1;
    int idx, jdx=1;

    if(traceOn) printf("%u %u ", fib0, fib1);

    for(idx=0; idx < iterCnt; idx++)    
    {                                   
        fib = fib0 + fib1;               
        if(traceOn) printf("%u ", fib);

        while(jdx < seqCnt)              
        {                                
            fib0 = fib1;                 
            fib1 = fib;                  
            fib = fib0 + fib1;            
            if(traceOn) printf("%u ", fib);
            jdx++;                        
        }                                
        jdx=1; 
        fib = 0, fib0 = 0, fib1 = 1;                        
        if(traceOn && (idx < iterCnt-1)) printf("\n\n%u %u ", fib0, fib1);
    }                                   
}

void print_scheduler(void)
{
    int schedType;

    schedType = sched_getscheduler(getpid());

    switch(schedType)
    {
        case SCHED_FIFO:
        printf("Pthread Policy is SCHED_FIFO\n");
        break;
        case SCHED_OTHER:
        printf("Pthread Policy is SCHED_OTHER\n");
        break;
        case SCHED_RR:
        printf("Pthread Policy is SCHED_RR\n");
        break;
        default:
        printf("Pthread Policy is UNKNOWN\n");
    }
}


void *simpleTask(void *threadp)
{
    struct timespec timeNow;
    threadParams_t *threadParams = (threadParams_t *)threadp;
    int idleIdx = threadParams->threadIdx, cpucore;

    cpucore=sched_getcpu();

    do
    {
        fibCycleBurner(fibLength, fibComputeSequences, 0);
        idleCount[idleIdx]++;
        if(idleIdx == LOW_PRIO_SERVICE) printf("L%u ", idleCount[idleIdx]);
        else if(idleIdx == MID_PRIO_SERVICE) printf("M%u ", idleCount[idleIdx]);
        else if(idleIdx == HIGH_PRIO_SERVICE) printf("H%u ", idleCount[idleIdx]);
    } while(idleCount[idleIdx] < runInterference);

    clock_gettime(CLOCK_REALTIME, &timeNow);

    if(idleIdx == LOW_PRIO_SERVICE)
        printf("\n**** LOW PRIO %d on core %d INTERFERE NO SEM COMPLETED at %lf sec\n", idleIdx, cpucore, dTime(timeNow, timeStartTest));
    else if(idleIdx == MID_PRIO_SERVICE)
        printf("\n**** MID PRIO %d on core %d INTERFERE NO SEM COMPLETED at %lf sec\n", idleIdx, cpucore, dTime(timeNow, timeStartTest));
    else if(idleIdx == HIGH_PRIO_SERVICE)
        printf("\n**** HIGH PRIO %d on core %d INTERFERE NO SEM COMPLETED at %lf sec\n", idleIdx, cpucore, dTime(timeNow, timeStartTest));

    pthread_exit(NULL);
}

void *criticalSectionTask(void *threadp)
{
    struct timespec timeNow;
    threadParams_t *threadParams = (threadParams_t *)threadp;
    int idleIdx = threadParams->threadIdx, cpucore;

    cpucore=sched_getcpu();

    if(idleIdx == LOW_PRIO_SERVICE) printf("\nCS-L REQUEST - core %d\n", cpucore);
    else if(idleIdx == MID_PRIO_SERVICE) printf("\nCS-M REQUEST - core %d\n", cpucore);
    else if(idleIdx == HIGH_PRIO_SERVICE) printf("\nCS-H REQUEST - core %d\n", cpucore);

    pthread_mutex_lock(&sharedMemSem);
    CScnt++;

    if(idleIdx == LOW_PRIO_SERVICE) printf("\nCS-L ENTRY %u\n", CScnt);
    else if(idleIdx == MID_PRIO_SERVICE) printf("\nCS-M ENTRY %u\n", CScnt);
    else if(idleIdx == HIGH_PRIO_SERVICE) printf("\nCS-H ENTRY %u\n", CScnt);

    idleCount[idleIdx]=0;

    do
    {
        fibCycleBurner(fibLength, fibComputeSequences, 0);
        idleCount[idleIdx]++;
        if(idleIdx == LOW_PRIO_SERVICE) printf("CS-L%u ", idleCount[idleIdx]);
        else if(idleIdx == MID_PRIO_SERVICE) printf("CS-M%u ", idleCount[idleIdx]);
        else if(idleIdx == HIGH_PRIO_SERVICE) printf("CS-H%u ", idleCount[idleIdx]);
    } while(idleCount[idleIdx] < CS_LENGTH);

    if(idleIdx == LOW_PRIO_SERVICE) printf("\nCS-L LEAVING\n");
    else if(idleIdx == MID_PRIO_SERVICE) printf("\nCS-M LEAVING\n");
    else if(idleIdx == HIGH_PRIO_SERVICE) printf("\nCS-H LEAVING\n");

    pthread_mutex_unlock(&sharedMemSem);

    if(idleIdx == LOW_PRIO_SERVICE) printf("\nCS-L EXIT\n");
    else if(idleIdx == MID_PRIO_SERVICE) printf("\nCS-M EXIT\n");
    else if(idleIdx == HIGH_PRIO_SERVICE) printf("\nCS-H EXIT\n");

    clock_gettime(CLOCK_REALTIME, &timeNow);

    if(idleIdx == LOW_PRIO_SERVICE)
        printf("\n**** LOW PRIO %d on core %d CRIT SECTION WORK COMPLETED at %lf sec\n", idleIdx, cpucore, dTime(timeNow, timeStartTest));
    else if(idleIdx == MID_PRIO_SERVICE)
        printf("\n**** MID PRIO %d on core %d CRIT SECTION WORK COMPLETED at %lf sec\n", idleIdx, cpucore, dTime(timeNow, timeStartTest));
    else if(idleIdx == HIGH_PRIO_SERVICE)
        printf("\n**** HIGH PRIO %d on core %d CRIT SECTION WORK COMPLETED at %lf sec\n", idleIdx, cpucore, dTime(timeNow, timeStartTest));

    pthread_exit(NULL);
}

