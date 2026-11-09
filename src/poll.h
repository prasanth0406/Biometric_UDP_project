#ifndef BMTP_POLL_H
#define BMTP_POLL_H

#ifdef _WIN32
  #include <winsock2.h>
  #define poll WSAPoll
#else
  #include_next <poll.h>
#endif

#endif /* BMTP_POLL_H */
