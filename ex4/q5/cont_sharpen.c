/**
* This code is heavily based on capture.c -  RTES_5623/RTES-ECEE-5623-mcgaffin/simple-capture/capture.c
* Then the pixel effect for sharpening based on sharpen.c - https://o365coloradoedu-my.sharepoint.com/shared?id=%2Fpersonal%2Fsiewerts%5Fcolorado%5Fedu%2FDocuments%2FESEE%2DWeb%2DResources%2FRTES%2D5623%2DCU%2Fcode%2Fsharpen%2Dpsf&listurl=%2Fpersonal%2Fsiewerts%5Fcolorado%5Fedu%2FDocuments&viewid=352a79ce%2Ddcf4%2D42ac%2D896f%2D903d069f746d&csf=1&FolderCTID=0x0120000AC3DE8CB97B0647B9FAC11DCE7EAC00

* This application was expanded on from q4/continuous_brighten to include performance evaluations
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <fcntl.h>              /* low-level i/o */

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include <time.h>
#include <syslog.h>

#define DUMP_FRAMES
#define WRITE_BOTH

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define HRES 640
#define VRES 480
#define HRES_STR "640"
#define VRES_STR "480"

#define FRAME_INIT 8
#define FRAME_COUNT (300 + FRAME_INIT) 
#define SAT 255

#define S_TO_MS (1000)
#define NS_TO_MS (1e-6)

struct buffer {
    void   *start;
    size_t  length;
};

static char            *dev_name = "/dev/video0";
static int              fd = -1;
struct buffer          *buffers;
static unsigned int     n_buffers;
static double           d_start;

static FILE *ffmpeg_pipe = NULL;
static FILE *ffmpeg_pipe2 = NULL;

static double f_time_elapsed(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return ((double)now.tv_sec + (double)now.tv_nsec / 1000000000.0) - d_start;
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

#ifdef DUMP_FRAMES

static char ppm_header[128];
static char ppm_dumpname[64];

static void dump_ppm(const void *p, int size, unsigned int tag, struct timespec *time, const char *prefix)
{
    int written, total, dumpfd;
   
    snprintf(ppm_dumpname, sizeof(ppm_dumpname), "frames/%s_%04d.ppm", prefix, tag);
    dumpfd = open(ppm_dumpname, O_WRONLY | O_NONBLOCK | O_CREAT, 00666);
    if (dumpfd < 0) {
        syslog(LOG_ERR, "Failed to open output file %s", ppm_dumpname);
        return;
    }

    snprintf(ppm_header, sizeof(ppm_header), "P6\n#%010d sec %010d msec \n%s %s\n255\n", 
            (int)time->tv_sec, (int)((time->tv_nsec)/1000000), HRES_STR, VRES_STR);
    
    written = write(dumpfd, ppm_header, strlen(ppm_header));

    total = 0;
    do {
        written = write(dumpfd, (const unsigned char *)p + total, size - total);
        //add error handling
        if (written < 0) {
            if (errno == EINTR) continue;
            syslog(LOG_CRIT, "Write data error");
            break;
        }
        total += written;
    } while(total < size);

    syslog(LOG_INFO, "Frame written at %lf; wrote %d bytes", f_time_elapsed(), total);
    close(dumpfd);
}

#endif


// This is probably the most acceptable conversion from camera YUYV to RGB
//
// Wikipedia has a good discussion on the details of various conversions and cites good references:
// http://en.wikipedia.org/wiki/YUV
//
// Also http://www.fourcc.org/yuv.php
//
// What's not clear without knowing more about the camera in question is how often U & V are sampled compared
// to Y.
//
// E.g. YUV444, which is equivalent to RGB, where both require 3 bytes for each pixel
//      YUV422, which we assume here, where there are 2 bytes for each pixel, with two Y samples for one U & V,
//              or as the name implies, 4Y and 2 UV pairs
//      YUV420, where for every 4 Ys, there is a single UV pair, 1.5 bytes for each pixel or 36 bytes for 24 pixels

void yuv2rgb(int y, int u, int v, unsigned char *r, unsigned char *g, unsigned char *b)
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

// always ignore first 8 frames
static int framecnt = -8;

#define K 4.0

double PSF[9] = {-K/8.0, -K/8.0, -K/8.0, -K/8.0, K+1.0, -K/8.0, -K/8.0, -K/8.0, -K/8.0};

static void sharpen(const unsigned char *rgb_buffer, unsigned char *rgb_sharp_buf)
{
    int temp;
    // Skip first and last row, no neighbors to convolve with
    for(int i=1; i<((VRES)-1); i++)
    {
        //j represents red pixel data (R,G,B) - increments by 3
        // Skip first and last column, no neighbors to convolve with
        for(int j=3; j<((HRES-1)*3); j+=3)
        {
            temp = (PSF[0] * (double)rgb_buffer[((i-1)*HRES*3)+j-3]);
            temp += (PSF[1] * (double)rgb_buffer[((i-1)*HRES*3)+j]);
            temp += (PSF[2] * (double)rgb_buffer[((i-1)*HRES*3)+j+3]);
            temp += (PSF[3] * (double)rgb_buffer[((i)*HRES*3)+j-3]);
            temp += (PSF[4] * (double)rgb_buffer[((i)*HRES*3)+j]);
            temp += (PSF[5] * (double)rgb_buffer[((i)*HRES*3)+j+3]);
            temp += (PSF[6] * (double)rgb_buffer[((i+1)*HRES*3)+j-3]);
            temp += (PSF[7] * (double)rgb_buffer[((i+1)*HRES*3)+j]);
            temp += (PSF[8] * (double)rgb_buffer[((i+1)*HRES*3)+j+3]);
	    if(temp<0.0) temp=0.0;
	    if(temp>255.0) temp=255.0;
	    rgb_sharp_buf[(i*HRES*3)+j]=(unsigned char)temp;

        //green
            temp = (PSF[0] * (double)rgb_buffer[((i-1)*HRES*3)+j-2]);
            temp += (PSF[1] * (double)rgb_buffer[((i-1)*HRES*3)+j+1]);
            temp += (PSF[2] * (double)rgb_buffer[((i-1)*HRES*3)+j+4]);
            temp += (PSF[3] * (double)rgb_buffer[((i)*HRES*3)+j-2]);
            temp += (PSF[4] * (double)rgb_buffer[((i)*HRES*3)+j+1]);
            temp += (PSF[5] * (double)rgb_buffer[((i)*HRES*3)+j+4]);
            temp += (PSF[6] * (double)rgb_buffer[((i+1)*HRES*3)+j-2]);
            temp += (PSF[7] * (double)rgb_buffer[((i+1)*HRES*3)+j+1]);
            temp += (PSF[8] * (double)rgb_buffer[((i+1)*HRES*3)+j+4]);
	    if(temp<0.0) temp=0.0;
	    if(temp>255.0) temp=255.0;
	    rgb_sharp_buf[(i*HRES*3)+j+1]=(unsigned char)temp;

        //blue
            temp = (PSF[0] * (double)rgb_buffer[((i-1)*HRES*3)+j-1]);
            temp += (PSF[1] * (double)rgb_buffer[((i-1)*HRES*3)+j+2]);
            temp += (PSF[2] * (double)rgb_buffer[((i-1)*HRES*3)+j+5]);
            temp += (PSF[3] * (double)rgb_buffer[((i)*HRES*3)+j-1]);
            temp += (PSF[4] * (double)rgb_buffer[((i)*HRES*3)+j+2]);
            temp += (PSF[5] * (double)rgb_buffer[((i)*HRES*3)+j+5]);
            temp += (PSF[6] * (double)rgb_buffer[((i+1)*HRES*3)+j-1]);
            temp += (PSF[7] * (double)rgb_buffer[((i+1)*HRES*3)+j+2]);
            temp += (PSF[8] * (double)rgb_buffer[((i+1)*HRES*3)+j+5]);
	    if(temp<0.0) temp=0.0;
	    if(temp>255.0) temp=255.0;
	    rgb_sharp_buf[(i*HRES*3)+j+2]=(unsigned char)temp;
        }
    }
}

static void process_image(const void *p, int size)
{
    struct timespec frame_time, proc_start, now;
    double proc_dur_ms;
    int y_temp, y2_temp, u_temp, v_temp;
    unsigned char *pptr = (unsigned char *)p;

    unsigned char rgb_buffer[HRES * VRES * 3];
    unsigned char rgb_sharp_buffer[HRES * VRES * 3];

    // transformation start
    clock_gettime(CLOCK_MONOTONIC, &proc_start);

    // record when process was called
    clock_gettime(CLOCK_REALTIME, &frame_time);    
    framecnt++;

    if (framecnt == 0) {
        //get initial start
        struct timespec starttime;
        clock_gettime(CLOCK_MONOTONIC, &starttime);
        d_start = (double)starttime.tv_sec + (double)starttime.tv_nsec / 1000000000.0;
    }

    // Pixels are YU and YV alternating, so YUYV which is 4 bytes
    // We want RGB, so RGBRGB which is 6 bytes
    int rgb_idx = 0;
    for (int i = 0; i < size; i += 4) {
        y_temp  = (int)pptr[i]; 
        u_temp  = (int)pptr[i+1]; 
        y2_temp = (int)pptr[i+2]; 
        v_temp  = (int)pptr[i+3];

        yuv2rgb(y_temp, u_temp, v_temp, &rgb_buffer[rgb_idx], &rgb_buffer[rgb_idx+1], &rgb_buffer[rgb_idx+2]);
        yuv2rgb(y2_temp, u_temp, v_temp, &rgb_buffer[rgb_idx+3], &rgb_buffer[rgb_idx+4], &rgb_buffer[rgb_idx+5]);
        rgb_idx += 6;
    }

    //sharpen
    sharpen(rgb_buffer, rgb_sharp_buffer);

    //transformed frame ready - end transform
    clock_gettime(CLOCK_MONOTONIC, &now);
    proc_dur_ms = (now.tv_sec - proc_start.tv_sec) * S_TO_MS + (now.tv_nsec - proc_start.tv_nsec) * NS_TO_MS;
    syslog(LOG_INFO, "Frame #%d transformed in %lf ms", framecnt, proc_dur_ms);

    //start dump time
    clock_gettime(CLOCK_MONOTONIC, &proc_start);
#ifdef DUMP_FRAMES
    if (framecnt > -1) {
        // syslog(LOG_INFO, "frame %d: Processed YUYV to Brightened RGB size %d", framecnt, size);
        dump_ppm(rgb_sharp_buffer, sizeof(rgb_sharp_buffer), framecnt, &frame_time, "sharp");
    #ifdef WRITE_BOTH
        dump_ppm(rgb_buffer, sizeof(rgb_buffer), framecnt, &frame_time, "orig");
    #endif
    } else {
        syslog(LOG_INFO, "frame %d: ignored", framecnt);
    }
#else
    if (framecnt > -1 && ffmpeg_pipe != NULL) {
        // Stream raw data straight into the pipe
        size_t written = fwrite(rgb_sharp_buffer, 1, sizeof(rgb_sharp_buffer), ffmpeg_pipe);
        
        if (written < sizeof(rgb_sharp_buffer)) {
            syslog(LOG_ERR, "frame %d: Pipe write mismatch or failure", framecnt);
        }
        else
        {
            syslog(LOG_INFO, "frame %d: Pipe write success", framecnt);
        }
    #ifdef WRITE_BOTH
        written = 0;
        if(ffmpeg_pipe2 != NULL)
        {
            written = fwrite(rgb_buffer, 1, sizeof(rgb_buffer), ffmpeg_pipe2);
            if (written < sizeof(rgb_buffer)) {
            syslog(LOG_ERR, "Original Capture - frame %d: Pipe2 write mismatch or failure", framecnt);
            }
            else
            {
                syslog(LOG_INFO, "frame %d: Original Capture pipe2 write success", framecnt);
            }
        }
        else
        {
            syslog(LOG_ERR, "FFMPEG pipe2 is NULL");
        }
    #endif
    }
#endif

    //end write
    clock_gettime(CLOCK_MONOTONIC, &now);
    proc_dur_ms = (now.tv_sec - proc_start.tv_sec) * S_TO_MS + (now.tv_nsec - proc_start.tv_nsec) * NS_TO_MS;
    syslog(LOG_INFO, "Frame #%d wrote in %lf ms", framecnt, proc_dur_ms);
}

static int read_frame(void)
{
    //frame acqusition start
    struct timespec proc_start, now;
    double proc_dur_ms;
    clock_gettime(CLOCK_MONOTONIC, &proc_start);

    struct v4l2_buffer buf;

    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
        if (errno == EAGAIN) return 0;
        syslog(LOG_CRIT, "mmap failure");
        errno_exit("VIDIOC_DQBUF");
    }

    assert(buf.index < n_buffers);

    //frame acquisition end
    clock_gettime(CLOCK_MONOTONIC, &now);
    proc_dur_ms = (now.tv_sec - proc_start.tv_sec) * S_TO_MS + (now.tv_nsec - proc_start.tv_nsec)*NS_TO_MS;
    syslog(LOG_INFO, "Frame #%d acquired in %lf ms", framecnt, proc_dur_ms);

    process_image(buffers[buf.index].start, buf.bytesused);

    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
        errno_exit("VIDIOC_QBUF");

    return 1;
}

static void mainloop(void)
{
    unsigned int remaining_frames = FRAME_COUNT;
    // struct timespec read_delay, time_error;
    int frames_logged = -7;

    // ~30 FPS sequencer constraint (33.33 msec delay)
    // read_delay.tv_sec = 0;
    // read_delay.tv_nsec = 33333300; //slightly less - account for processing time

    double elapsed = 0;
    double last_elapsed = 0;

    while (remaining_frames > 0) {
        for (;;) {
            fd_set fds;
            struct timeval tv;
            int r;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select(fd + 1, &fds, NULL, NULL, &tv);

            if (-1 == r) {
                if (EINTR == errno) continue;
                errno_exit("select");
            }

            if (0 == r) {
                syslog(LOG_CRIT, "select timeout");
                exit(EXIT_FAILURE);
            }

            if (read_frame()) {
                //if (nanosleep(&read_delay, &time_error) != 0) {
                  //  syslog(LOG_ERR, "nanosleep system interrupt caught");
                //} else 
		        if (frames_logged > 0) {
                    last_elapsed = elapsed;
                    elapsed = f_time_elapsed();
                    syslog(LOG_INFO, "Frame #%u read at %lf - Inst FPS=%lf - Avg FPS=%lf", 
                           frames_logged, elapsed, 1.0/(elapsed-last_elapsed), (double)frames_logged / elapsed);
                }                    
                remaining_frames--;
                frames_logged++;
                break;
            }
            if (remaining_frames <= 0) break;
        }
    }
}

static void init_mmap(void)
{
    struct v4l2_requestbuffers req;

    CLEAR(req);
    req.count = 6;
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

    buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) {
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

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

        if (MAP_FAILED == buffers[n_buffers].start)
            errno_exit("mmap");
    }
}

static void init_device(void)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;

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
    syslog(LOG_INFO, "FORCING FORMAT: 640x480 YUYV");
    fmt.fmt.pix.width       = HRES;
    fmt.fmt.pix.height      = VRES;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
            errno_exit("VIDIOC_S_FMT");

    init_mmap();
}

#ifndef DUMP_FRAMES
static void init_pipe()
{
    char ffmpeg_cmd[] = "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size 640x480 -i - -c:v libx264 -pix_fmt yuv420p piped_output.mp4";

    ffmpeg_pipe = popen(ffmpeg_cmd, "w");
    if (ffmpeg_pipe == NULL) {
        syslog(LOG_CRIT, "Failed to open pipe. Ensure ffmpeg is installed.");
        exit(EXIT_FAILURE);
    }

#ifdef WRITE_BOTH
    char ffmpeg2_cmd[] = "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size 640x480 -i - -c:v libx264 -pix_fmt yuv420p piped_original.mp4";
    ffmpeg_pipe2 = popen(ffmpeg2_cmd, "w");
    if (ffmpeg_pipe2 == NULL) {
        syslog(LOG_CRIT, "Failed to open pipe. Ensure ffmpeg is installed.");
        exit(EXIT_FAILURE);
    }
#endif
}

static void close_pipe()
{
    if (ffmpeg_pipe != NULL) {
        pclose(ffmpeg_pipe);
        syslog(LOG_INFO, "FFmpeg pipe closed cleanly.");
    }
    else
    {
        syslog(LOG_INFO, "No pipe to close");
    }

#ifdef WRITE_BOTH
    if (ffmpeg_pipe2 != NULL) {
        pclose(ffmpeg_pipe2);
        syslog(LOG_INFO, "FFmpeg pipe2 closed cleanly.");
    }
    else
    {
        syslog(LOG_INFO, "No pipe2 to close");
    }
#endif
}
#endif

int main(int argc, char **argv)
{
    openlog("video_brightener", LOG_PID|LOG_CONS, LOG_USER);
    syslog(LOG_INFO, "Initializing capture routine loop...");

    // open device
    fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
    if (-1 == fd) {
        syslog(LOG_CRIT, "Cannot open '%s': %d, %s", dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    init_device();

#ifndef DUMP_FRAMES
    init_pipe();
#endif

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

    mainloop();

    // Cleanup and Shutdown
    //stop capturing
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMOFF, &type);
    //uninit
    for (unsigned int i = 0; i < n_buffers; ++i) {
        munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    //close device
    if (-1 == close(fd))
        errno_exit("close");

    fd = -1;

#ifndef DUMP_FRAMES
    close_pipe();
#endif

    syslog(LOG_INFO, "Capture cleanup completed successfully.");
    closelog();

    return 0;
}
