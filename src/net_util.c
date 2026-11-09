/* net_util.c - Socket Handling, Time, and Kernel Metrics */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "net_util.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int net_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    return 0;
#endif
}

void net_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

int send_all(int sockfd, const void *buf, size_t len) {
    const uint8_t *ptr = (const uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
#ifdef _WIN32
        int sent = send(sockfd, (const char *)ptr, (int)remaining, 0);
#else
        ssize_t sent = send(sockfd, ptr, remaining, 0);
#endif
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        ptr += sent;
        remaining -= (size_t)sent;
    }
    return 0;
}

int recv_all(int sockfd, void *buf, size_t len) {
    uint8_t *ptr = (uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
#ifdef _WIN32
        int n = recv(sockfd, (char *)ptr, (int)remaining, 0);
#else
        ssize_t n = recv(sockfd, ptr, remaining, 0);
#endif
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            /* EOF: peer closed */
            return (remaining == len) ? 1 : -1;
        }
        ptr += n;
        remaining -= (size_t)n;
    }
    return 0;
}

int set_tcp_nodelay(int sockfd, int enable) {
    int opt = enable ? 1 : 0;
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const void *)&opt, sizeof(opt));
}

void get_socket_buffers(int sockfd, int *sndbuf, int *rcvbuf) {
    socklen_t optlen = sizeof(int);
    *sndbuf = -1;
    *rcvbuf = -1;
    getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (void *)sndbuf, &optlen);
    optlen = sizeof(int);
    getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (void *)rcvbuf, &optlen);
}

double get_monotonic_time(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1e9);
}

void get_socket_addr_str(int sockfd, char *out_str, size_t max_len, int is_peer) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    int res = is_peer ? getpeername(sockfd, (struct sockaddr *)&addr, &addr_len)
                      : getsockname(sockfd, (struct sockaddr *)&addr, &addr_len);
    if (res == 0) {
        char ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
            snprintf(out_str, max_len, "%s:%d", ip, ntohs(addr.sin_port));
            return;
        }
    }
    snprintf(out_str, max_len, "unknown:0");
}

int get_tcp_info(int sockfd, tcp_info_summary_t *summary) {
    summary->has_info = 0;
    summary->rtt_us = 0;
    summary->rto_us = 0;
    summary->snd_cwnd = 0;
    summary->total_retrans = 0;

#if defined(__linux__) && defined(TCP_INFO)
    struct tcp_info info;
    socklen_t len = sizeof(info);
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, &info, &len) == 0) {
        summary->rtt_us = info.tcpi_rtt;
        summary->rto_us = info.tcpi_rto;
        summary->snd_cwnd = info.tcpi_snd_cwnd;
        summary->total_retrans = info.tcpi_total_retrans;
        summary->has_info = 1;
        return 0;
    }
#else
    (void)sockfd;
#endif
    return -1;
}
