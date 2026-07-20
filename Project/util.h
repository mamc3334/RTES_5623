#ifndef UTIL_H
#define UTIL_H

//INPUT BUFFER
#define RING_BUFFER_SIZE 8  // Must be larger than V4L2's req.count (6)
#define HRES 480
#define VRES 360

struct RingBuffer{
    unsigned char buf_idx[RING_BUFFER_SIZE];
    unsigned char head;
    unsigned char tail;
};



void errno_exit(const char *s);

#endif /* UTIL_H */
