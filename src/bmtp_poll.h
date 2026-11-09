#ifndef BMTP_POLL_H
#define BMTP_POLL_H

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC system_header
#endif

#ifdef _WIN32
  #include <winsock2.h>
  #define poll WSAPoll
#else
  #include <poll.h>
#endif

#endif /* BMTP_POLL_H */
