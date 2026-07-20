#ifndef ONE_HZ
#define ONE_HZ

#include "util.h"

//WEBCAM
#define WEBCAM "/dev/video0"

int read_frame(const int fd);
void init_device(const int fd);

#endif // ONE_HZ