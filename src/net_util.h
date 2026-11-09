/* net_util.h - POSIX Socket and Network Utilities */
#ifndef BMTP_NET_UTIL_H
#define BMTP_NET_UTIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef int socklen_t;
  #define CLOSE_SOCKET(s) closesocket(s)
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>
  #define CLOSE_SOCKET(s) close(s)
#endif

typedef struct {
    uint32_t rtt_us;
    uint32_t rto_us;
    uint32_t snd_cwnd;
    uint32_t total_retrans;
    int has_info;
} tcp_info_summary_t;

/**
 * @brief Initialize socket subsystem (WSAStartup on Windows, no-op on POSIX).
 */
int net_init(void);

/**
 * @brief Cleanup socket subsystem.
 */
void net_cleanup(void);

/**
 * @brief Send all bytes over socket, handling partial writes and EINTR.
 * @return 0 on full transmission, -1 on error.
 */
int send_all(int sockfd, const void *buf, size_t len);

/**
 * @brief Receive exact number of bytes from socket, handling partial reads and EINTR.
 * @return 0 on complete read, 1 on clean EOF/disconnect, -1 on error.
 */
int recv_all(int sockfd, void *buf, size_t len);

/**
 * @brief Set TCP_NODELAY (disable Nagle's algorithm) on socket.
 */
int set_tcp_nodelay(int sockfd, int enable);

/**
 * @brief Query SO_SNDBUF and SO_RCVBUF sizes.
 */
void get_socket_buffers(int sockfd, int *sndbuf, int *rcvbuf);

/**
 * @brief Get high-resolution monotonic time in seconds (microsecond precision).
 */
double get_monotonic_time(void);

/**
 * @brief Format IP and port string "IP:PORT" for a socket (local or peer).
 */
void get_socket_addr_str(int sockfd, char *out_str, size_t max_len, int is_peer);

/**
 * @brief Query Linux TCP_INFO if supported.
 */
int get_tcp_info(int sockfd, tcp_info_summary_t *summary);

#endif /* BMTP_NET_UTIL_H */
