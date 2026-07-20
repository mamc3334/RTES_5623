/**
* 
*/
#define _GNU_SOURCE
#include <semaphore.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <fcntl.h>              /* low-level i/o */
#include <sys/syslog.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <linux/videodev2.h>

#include <time.h>
#include <syslog.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define HRES 640
#define VRES 480
#define HRES_STR "640"
#define VRES_STR "480"
#define FPS 30

#define FRAME_COUNT 1801 //TODO: change to 1801
#define NUM_BUFFERS 5 //ACQ 5 frames per processing
#define RING_SIZE 10 // should be plenty big
#define FRAME_BYTES (HRES * VRES * 2)

#define PRIO_WB 0
#define PRIO_PROCESS 60
#define PRIO_ACQ 70
#define PRIO_SEQ 80

#define ACQ_F 2
#define PROCESS_F 10

#define DIFF_THRESHOLD 25
#define EDGE_THRESHOLD 25

#define SEQ_T_NS (100000000) //10Hz
#define ACQ_T_NS (500000000) //2Hz
#define PRO_T_NS (1000000000) //1Hz

struct buffer {
    void    *start;
    size_t  length;
};

struct ring {
    uint8_t *buf[RING_SIZE];
    int head; //next read
    int tail; //next write
    int size;
    pthread_mutex_t lock;
};

static char *dev_name = "/dev/video0";
static int fd = -1;
struct buffer *cap_buffers;
static unsigned int n_buffers;
struct ring *in_rb;
struct ring *wb_rb;
static double d_start_ms;

//semaphores
sem_t sem_acq;
sem_t sem_process;
sem_t sem_write;

//thread funcs
pthread_t acq_id;
pthread_t process_id;
pthread_t wb_id;
pthread_t seq_id;

static void *sequencer_thread(void *arg);
static void *acq_thread(void *arg);
static void *process_thread(void *arg);
static void *writer_thread(void *arg);

//global running
static volatile _Atomic uint8_t capture;

static double f_time_elapsed_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return (((double)now.tv_sec * 1000) + ((double)now.tv_nsec / 1000000.0)) - d_start_ms;
}

static void errno_exit(const char *s)
{
    syslog(LOG_CRIT, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg)
{
    int r;

    do {
        r = ioctl(fh, request, arg);

    } while (-1 == r && EINTR == errno);

    return r;
}

//queue functions
static void ring_write(struct ring *r, const void *b, size_t num_bytes)
{
    if (r->size == RING_SIZE) //full
    {
        syslog(LOG_ALERT, "RING IS FULL - CONSIDER LARGER QUEUE");
        exit(EXIT_FAILURE);
    }

    if (num_bytes > FRAME_BYTES)
    {
        syslog(LOG_ALERT, "TOO BIG OF FRAME FOR ALLOCATED SPACE");
        exit(EXIT_FAILURE);
    }

    memcpy(r->buf[r->tail], b, num_bytes);

    //update ring pointers
    r->tail = (r->tail + 1) % RING_SIZE;
    r->size++;
}


/*
This is probably the most acceptable conversion from camera YUYV to RGB

Wikipedia has a good discussion on the details of various conversions and cites good references:
http://en.wikipedia.org/wiki/YUV

Also http://www.fourcc.org/yuv.php

What's not clear without knowing more about the camera in question is how often U & V are sampled compared
to Y.

E.g. YUV444, which is equivalent to RGB, where both require 3 bytes for each pixel
     YUV422, which we assume here, where there are 2 bytes for each pixel, with two Y samples for one U & V,
             or as the name implies, 4Y and 2 UV pairs
     YUV420, where for every 4 Ys, there is a single UV pair, 1.5 bytes for each pixel or 36 bytes for 24 pixels
*/
void yuv2rgb(int y, int u, int v, uint8_t *r, uint8_t *g, uint8_t *b)
{
   int r1, g1, b1;

   // replaces floating point coefficients
   int c = y-16, d = u - 128, e = v - 128;       

   // Conversion that avoids floating point
   r1 = (298 * c           + 409 * e + 128) >> 8;
   g1 = (298 * c - 100 * d - 208 * e + 128) >> 8;
   b1 = (298 * c + 516 * d           + 128) >> 8;

   // Computed values may need clipping.
   if (r1 > 255) r1 = 255;
   if (g1 > 255) g1 = 255;
   if (b1 > 255) b1 = 255;

   if (r1 < 0) r1 = 0;
   if (g1 < 0) g1 = 0;
   if (b1 < 0) b1 = 0;

   *r = r1 ;
   *g = g1 ;
   *b = b1 ;
}


static void dump_ppm(const void *p, int size, unsigned int tag, struct timespec *time)
{
    int written, total, dumpfd;
    char file_name[32];
    char header[64];
   
    snprintf(file_name, sizeof(file_name), "frames/frame_%04d.ppm", tag);
    dumpfd = open(file_name, O_WRONLY | O_NONBLOCK | O_CREAT, 00666);
    if (dumpfd < 0) {
        syslog(LOG_ERR, "Failed to open output file %s", file_name);
        return;
    }

    snprintf(header, sizeof(header), "P6\n#%010d sec %010d msec \n%s %s\n255\n", 
            (int)time->tv_sec, (int)((time->tv_nsec)/1000000), HRES_STR, VRES_STR);
    
    written = write(dumpfd, header, strlen(header));

    total = 0;
    do {
        written = write(dumpfd, (const uint8_t *)p + total, size - total);
        //add error handling
        if (written < 0) {
            if (errno == EINTR) continue;
            syslog(LOG_CRIT, "Write data error");
            break;
        }
        total += written;
    } while(total < size);

    close(dumpfd);
}

static void read_frame(void)
{
    struct v4l2_buffer buf;

    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    while (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
        if (errno == EAGAIN) return;
        syslog(LOG_CRIT, "mmap failure");
        errno_exit("VIDIOC_DQBUF");
    }

    assert(buf.index < n_buffers);

    pthread_mutex_lock(&in_rb->lock);
    ring_write(in_rb, cap_buffers[buf.index].start, buf.bytesused);
    pthread_mutex_unlock(&in_rb->lock);

    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
        errno_exit("VIDIOC_QBUF");
}

static void init_mmap(void)
{
    struct v4l2_requestbuffers req;

    CLEAR(req);
    req.count = NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            syslog(LOG_CRIT, "%s does not support memory mapping", dev_name);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_REQBUFS");
        }
    }

    cap_buffers = calloc(req.count, sizeof(*cap_buffers));
    if (!cap_buffers) {
        syslog(LOG_CRIT, "Out of memory");
        exit(EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        struct v4l2_buffer buf;
        CLEAR(buf);

        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = n_buffers;

        if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
            errno_exit("VIDIOC_QUERYBUF");

        cap_buffers[n_buffers].length = buf.length;
        cap_buffers[n_buffers].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

        if (MAP_FAILED == cap_buffers[n_buffers].start)
            errno_exit("mmap");
    }
}

static void init_device(void)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_streamparm parm;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            syslog(LOG_CRIT, "%s is no V4L2 device", dev_name);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_QUERYCAP");
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        syslog(LOG_CRIT, "%s lacks capture or streaming capacities", dev_name);
        exit(EXIT_FAILURE);
    }

    //force format
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    syslog(LOG_INFO, "FORCING FORMAT: 640x480 YUYV - %d FPS", FPS);
    fmt.fmt.pix.width       = HRES;
    fmt.fmt.pix.height      = VRES;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    //force fps
    CLEAR(parm);
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = FPS;
    xioctl(fd, VIDIOC_S_PARM, &parm);

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
            errno_exit("VIDIOC_S_FMT");

    init_mmap();

    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (unsigned int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) errno_exit("VIDIOC_QBUF");
    }
    if (-1 == xioctl(fd, VIDIOC_STREAMON, &type)) errno_exit("VIDIOC_STREAMON");

    //read first 10 frames
    for(int i=0; i<10; i++)
    {
        read_frame();
    }

    //discard frames - write over them
    in_rb->size=0;
}

static void init_ring()
{
    //init in ring
    in_rb = malloc(sizeof(struct ring));
    in_rb->head = 0;
    in_rb->tail = 0;
    in_rb->size = 0;

    pthread_mutex_init(&in_rb->lock, NULL);

    for(int i=0; i<RING_SIZE;i++)
    {
        in_rb->buf[i] = malloc(FRAME_BYTES);
        if (in_rb->buf[i] == NULL)
        {
            syslog(LOG_ALERT, "ERROR: Malloc failed");
            errno_exit("MALLOC");
        }
    }

    //init wb ring
    wb_rb = malloc(sizeof(struct ring));
    wb_rb->head = 0;
    wb_rb->tail = 0;
    wb_rb->size = 0;
    pthread_mutex_init(&wb_rb->lock, NULL);

    for(int i=0; i<RING_SIZE;i++)
    {
        wb_rb->buf[i] = malloc(FRAME_BYTES);
        if (wb_rb->buf[i] == NULL)
        {
            syslog(LOG_ALERT, "ERROR: Malloc failed");
            errno_exit("MALLOC");
        }
    }
}

//Use spatial gradient and temporal difference to calculate motion score
// identifies sharp different frames
static long get_score(uint8_t *prev, uint8_t *cur)
{
    if (prev == NULL || cur == NULL)
    {
        syslog(LOG_ALERT, "CANNOT GET SHARPNESS OF NULL BUFFER");
        errno_exit("SHARPNESS_NO_BUFFER");
    }

    long score = 0;

    // YUYV uses 2 bytes per pixel
    int stride = HRES * 2;
    for (int y = 0; y < VRES - 1; y++)
    {
        const uint8_t *p_row = prev + (y * stride);
        const uint8_t *c_row = cur + (y * stride);
        const uint8_t *c_next_row = cur + ((y + 1) * stride);

        for (int x = 0; x < HRES - 1; x++)
        {
            int idx = x * 2;

            //change from last frame
            int prev_Y = p_row[idx];
            int cur_Y  = c_row[idx];
            int temporal_diff = abs(cur_Y - prev_Y);

            if (temporal_diff > DIFF_THRESHOLD)
            {
                // sharpness
                int cur_Y_right = c_row[idx + 2];
                int cur_Y_down  = c_next_row[idx];

                int spatial_gradient = abs(cur_Y - cur_Y_right) + abs(cur_Y - cur_Y_down);

                // Only count if it's changing AND it's a sharp edge
                if (spatial_gradient > EDGE_THRESHOLD)
                {
                    // Weighting by spatial gradient penalizes blurry, smeared transitions
                    score += spatial_gradient;
                }
            }
        }
    }

    return score;
}

static void cleanup()
{
    //stop capturing
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMOFF, &type);
    
    //uninit
    for (unsigned int i = 0; i < n_buffers; ++i) {
        munmap(cap_buffers[i].start, cap_buffers[i].length);
    }
    free(cap_buffers);
    cap_buffers = NULL;
    
    //close device
    if (-1 == close(fd))
        errno_exit("close");

    fd = -1;

    //free in_rb and wb_rb
    for(int i = 0; i < RING_SIZE; i++) {
        if (in_rb->buf[i] != NULL) {
            free(in_rb->buf[i]);
            in_rb->buf[i] = NULL;
        }
        if (wb_rb->buf[i] != NULL) {
            free(wb_rb->buf[i]);
            wb_rb->buf[i] = NULL;
        }
    }
}

// GLOBALS
int main(int argc, char **argv)
{
    struct timespec ts;
    cpu_set_t cpuset;
    pthread_attr_t attr;
    struct sched_param param;

    openlog("one_hz", LOG_PID|LOG_CONS, LOG_USER);

    clock_gettime(CLOCK_REALTIME, &ts);

    // Convert raw seconds to calendar time
    struct tm *local_time = localtime(&ts.tv_sec);

     // Format and print the date and time (e.g., YYYY-MM-DD HH:MM:SS)
    char time[64];
    strftime(time, sizeof(time), "%Y-%m-%d %H:%M:%S", local_time);

    syslog(LOG_INFO, "1HZ synchronome for analog clock started at %s - %d frames", time, FRAME_COUNT);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    d_start_ms = ((double)ts.tv_sec * 1000) + ((double)ts.tv_nsec / 1000000.0);

    // open device
    fd = open(dev_name, O_RDWR, 0);
    if (-1 == fd) {
        syslog(LOG_CRIT, "Cannot open '%s': %d, %s", dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    //set global
    capture = 1;

    //initialize semaphores
    sem_init(&sem_acq, 0, 0);
    sem_init(&sem_process, 0, 0);
    sem_init(&sem_write, 0, 0);

    //init ring buffers
    init_ring();

    //init webcam
    init_device();

    // Create wb thread - will set to core 1
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER); // best effort
    param.sched_priority = PRIO_WB;
    pthread_attr_setschedparam(&attr, &param);

    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);

    pthread_create(&wb_id, &attr, writer_thread, NULL);

    //Create rm threads - process, acq, seq
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO); // best effort
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);

    param.sched_priority = PRIO_PROCESS;
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(&process_id, &attr, process_thread, NULL);

    param.sched_priority = PRIO_ACQ;
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(&acq_id, &attr, acq_thread, NULL);

    param.sched_priority = PRIO_SEQ;
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(&seq_id, &attr, sequencer_thread, NULL);

    //join threads
    pthread_join(seq_id, NULL);
    pthread_join(acq_id, NULL);
    pthread_join(process_id, NULL);
    pthread_join(wb_id, NULL);

    //cleanup
    cleanup();

    syslog(LOG_INFO, "ONE_HZ cleanup completed successfully.");

    return 0;
}

static void *sequencer_thread(void *arg)
{
    (void)arg;
    syslog(LOG_INFO, "[SEQ] sequencer started at %.04f on CPU %d, period=%lld ms", f_time_elapsed_ms(), sched_getcpu(), (long long)(SEQ_T_NS/1000000));

    int count = 0;
    struct timespec release;
    clock_gettime(CLOCK_MONOTONIC, &release);

    uint64_t target_ns = (uint64_t)release.tv_sec * 1000000000ULL + release.tv_nsec;
    
    while(capture)
    {
        //get next release time
        target_ns += SEQ_T_NS;

        release.tv_sec = target_ns / 1000000000ULL;
        release.tv_nsec = target_ns % 1000000000ULL;

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &release, NULL);
        count++;

        // Spawn threads
        if(count % ACQ_F == 0)
        {
            sem_post(&sem_acq);
        }
        if(count % PROCESS_F == 0)
        {
            sem_post(&sem_process);
        }
    }

    //release to finish
    sem_post(&sem_acq);
    sem_post(&sem_process);

    return NULL;
}

static void *acq_thread(void *arg)
{
    syslog(LOG_INFO, "[ACQ] frame acquisition started at %.04f on CPU %d", f_time_elapsed_ms(), sched_getcpu());

    while(capture)
    {
        if (sem_wait(&sem_acq) != 0) {
            if (errno == EINTR) continue;
            break;
        }

        if(!capture) break;

        syslog(LOG_INFO, "[ACQ] started reading frame at %.04f", f_time_elapsed_ms());

        read_frame();

        syslog(LOG_INFO, "[ACQ] done reading frame at %.04f", f_time_elapsed_ms());
    }

    return NULL;
}

static void *process_thread(void *arg)
{
    syslog(LOG_INFO, "[PRO] processor started at %.04f on CPU %d", f_time_elapsed_ms(), sched_getcpu());
    
    uint8_t *cur_frame = malloc(FRAME_BYTES);
    uint8_t *best_frame = malloc(FRAME_BYTES);
    uint8_t *last_frame = malloc(FRAME_BYTES);

    uint8_t first_tick = 1;

    if (!cur_frame || !best_frame) {
        syslog(LOG_CRIT, "[PRO] Malloc failed");
        errno_exit("malloc");
    }

    while(capture)
    {
        if (sem_wait(&sem_process) != 0) {
            if (errno == EINTR) continue;
            break;
        }

        if(!capture) break;

        syslog(LOG_INFO, "[PRO] started processing frame at %.04f", f_time_elapsed_ms());

        // uint8_t buffer[FRAME_BYTES];
        long best_score = 0;

        pthread_mutex_lock(&in_rb->lock);
        while(in_rb->size > 0)
        {
            //skip 2 frames - likely to be old second
            while(in_rb->size > 2)
            {
                in_rb->head = (in_rb->head+1) % RING_SIZE;
                in_rb->size--;
            }

            memcpy(cur_frame, in_rb->buf[in_rb->head], FRAME_BYTES);
            in_rb->head = (in_rb->head+1) % RING_SIZE;
            in_rb->size--;
            pthread_mutex_unlock(&in_rb->lock);

            if (first_tick) {
                memcpy(last_frame, cur_frame, FRAME_BYTES);
                first_tick = 0;
            }

            long score = get_score(last_frame, cur_frame);

            if( score > best_score)
            {
                best_score = score;
                memcpy(best_frame, cur_frame, FRAME_BYTES);
            }
        }

        pthread_mutex_lock(&wb_rb->lock);
        ring_write(wb_rb, best_frame, FRAME_BYTES);
        pthread_mutex_unlock(&wb_rb->lock);

        //copy to last frame
        memcpy(last_frame, best_frame, FRAME_BYTES);

        sem_post(&sem_write);

        syslog(LOG_INFO, "[PRO] done processing frame at %.04f", f_time_elapsed_ms());
    }

    free(cur_frame);
    cur_frame = NULL;
    free(best_frame);
    best_frame = NULL;
    free(last_frame);
    last_frame = NULL;

    return NULL;
}

static void *writer_thread(void *arg)
{
    int frame = 1;
    uint8_t local_frame[FRAME_BYTES];
    uint8_t rgb_buffer[HRES * VRES * 3];
    int y_temp, y2_temp, u_temp, v_temp;

    syslog(LOG_INFO, "[WB] writer started at %.04f on CPU %d", f_time_elapsed_ms(), sched_getcpu());

    while(capture)
    {
        if (sem_wait(&sem_write) != 0) {
            if (errno == EINTR) continue;
            break;
        }
        syslog(LOG_INFO, "[WB] started writing frame at %.04f", f_time_elapsed_ms());

        pthread_mutex_lock(&wb_rb->lock);
        if(wb_rb->size == 0) //empty
        {
            continue;
        }

        // copy to local buf
        memcpy(local_frame, wb_rb->buf[wb_rb->head], FRAME_BYTES);
        wb_rb->head = (wb_rb->head+1) % RING_SIZE;
        wb_rb->size--;
        pthread_mutex_unlock(&wb_rb->lock);
   
        int rgb_idx = 0;

        // Pixels are YU and YV alternating, so YUYV which is 4 bytes
        // We want RGB, so RGBRGB which is 6 bytes
        for (int i = 0; i < FRAME_BYTES; i += 4) {
            y_temp  = (int)local_frame[i]; 
            u_temp  = (int)local_frame[i+1]; 
            y2_temp = (int)local_frame[i+2]; 
            v_temp  = (int)local_frame[i+3];

            yuv2rgb(y_temp, u_temp, v_temp, &rgb_buffer[rgb_idx], &rgb_buffer[rgb_idx+1], &rgb_buffer[rgb_idx+2]);
            yuv2rgb(y2_temp, u_temp, v_temp, &rgb_buffer[rgb_idx+3], &rgb_buffer[rgb_idx+4], &rgb_buffer[rgb_idx+5]);
            rgb_idx += 6;
        }
        struct timespec frame_time;
        clock_gettime(CLOCK_REALTIME, &frame_time);
        dump_ppm(rgb_buffer, sizeof(rgb_buffer), frame, &frame_time);

        syslog(LOG_INFO, "[WB] Frame #%d written at %.04f", frame, f_time_elapsed_ms());

        frame++;
        if (frame > FRAME_COUNT)
            capture = 0;
    }

    return NULL;
}