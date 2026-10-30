#pragma once

// Engine addition (Android port): <sys/timeb.h> for bionic, which does not have it (ftime
// left POSIX in 2008). Wild Magic's System::GetTime reads the wall clock through it; the
// Android build puts this directory on Wild Magic's include path
// (cmake/deps/wildmagic/CMakeLists.txt).

#include <time.h>

struct timeb
{
  time_t time;
  unsigned short millitm;
  short timezone;
  short dstflag;
};

static inline int ftime(struct timeb* const tp)
{
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  tp->time = now.tv_sec;
  tp->millitm = (unsigned short)(now.tv_nsec / 1000000);
  tp->timezone = 0;
  tp->dstflag = 0;
  return 0;
}
