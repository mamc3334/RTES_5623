/**
 * Defines some standard error printing and time stamping methods
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>

#include "util.h"

void errno_exit(const char *s)
{
    syslog(LOG_CRIT, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}