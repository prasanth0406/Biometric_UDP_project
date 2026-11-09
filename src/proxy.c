/* proxy.c - Application-Layer Lossy Network Impairment Proxy in C */
#define _POSIX_C_SOURCE 200809L
#include "net_util.h"
#include "protocol.h"

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void delay_millis(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int main(int argc, char *argv[]) {
    int listen_port = 5000;
    char target_host[64] = "127.0.0.1";
    int target_port = 5001;
    float drop_rate = 0.0f;
    float corrupt_rate = 0.0f;
    int delay_ms = 0;
    int drop_seq = -1;
    int single_session = 1;

    int opt;
    while ((opt = getopt(argc, argv, "l:d:p:m:k:t:s:1?")) != -1) {
        switch (opt) {
            case 'l': listen_port = atoi(optarg); break;
            case 'd': snprintf(target_host, sizeof(target_host), "%s", optarg); break;
            case 'p': target_port = atoi(optarg); break;
            case 'm': drop_rate = (float)atof(optarg); break;
            case 'k': corrupt_rate = (float)atof(optarg); break;
            case 't': delay_ms = atoi(optarg); break;
            case 's': drop_seq = atoi(optarg); break;
            case '1': single_session = 1; break;
            default:
                fprintf(stderr, "Usage: %s [-l listen_port] [-d dest_host] [-p dest_port] [-m drop_rate] [-k corrupt_rate] [-t delay_ms] [-s drop_seq] [-1]\n", argv[0]);
                return 1;
        }
    }

    srand((unsigned int)time(NULL));

    if (net_init() != 0) {
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("proxy socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuse, sizeof(reuse));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((uint16_t)listen_port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        perror("proxy bind");
        CLOSE_SOCKET(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 5) < 0) {
        perror("proxy listen");
        CLOSE_SOCKET(listen_fd);
        return 1;
    }

    printf("[PROXY] Listening on 0.0.0.0:%d -> Forwarding to %s:%d (Drop=%.1f%%, Delay=%dms, Corrupt=%.1f%%)\n",
           listen_port, target_host, target_port, drop_rate * 100.0f, delay_ms, corrupt_rate * 100.0f);

    do {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            break;
        }
        set_tcp_nodelay(client_fd, 1);

        /* Connect to real target */
        int target_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (target_fd < 0) {
            CLOSE_SOCKET(client_fd);
            break;
        }
        set_tcp_nodelay(target_fd, 1);

        struct sockaddr_in t_addr;
        memset(&t_addr, 0, sizeof(t_addr));
        t_addr.sin_family = AF_INET;
        t_addr.sin_port = htons((uint16_t)target_port);
        inet_pton(AF_INET, target_host, &t_addr.sin_addr);

        if (connect(target_fd, (struct sockaddr *)&t_addr, sizeof(t_addr)) < 0) {
            perror("proxy connect target");
            CLOSE_SOCKET(client_fd);
            CLOSE_SOCKET(target_fd);
            break;
        }

        printf("[PROXY] Sender connected. Forwarding traffic...\n");

        int active = 1;
        while (active) {
            struct pollfd fds[2];
            fds[0].fd = client_fd;
            fds[0].events = POLLIN;
            fds[0].revents = 0;

            fds[1].fd = target_fd;
            fds[1].events = POLLIN;
            fds[1].revents = 0;

            int ret = poll(fds, 2, 500);
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }

            if ((fds[0].revents & (POLLHUP | POLLERR)) || (fds[1].revents & (POLLHUP | POLLERR))) {
                active = 0;
                break;
            }

            /* Data from Sender -> Receiver */
            if (fds[0].revents & POLLIN) {
                uint8_t hdr_bytes[BMTP_HEADER_LEN];
                int r = recv_all(client_fd, hdr_bytes, BMTP_HEADER_LEN);
                if (r != 0) {
                    active = 0;
                    break;
                }
                bmtp_header_t hdr;
                bmtp_unpack_header(hdr_bytes, &hdr);

                uint8_t *payload = NULL;
                if (hdr.payload_len > 0) {
                    payload = (uint8_t *)malloc(hdr.payload_len);
                    if (recv_all(client_fd, payload, hdr.payload_len) != 0) {
                        free(payload);
                        active = 0;
                        break;
                    }
                }

                int should_drop = 0;
                int should_corrupt = 0;

                if (hdr.msg_type == BMTP_MSG_DATA) {
                    if (drop_seq == (int)hdr.seq_num) {
                        should_drop = 1;
                        drop_seq = -1; /* Drop only once */
                    } else if (drop_rate > 0.0f && ((float)rand() / (float)RAND_MAX) < drop_rate) {
                        should_drop = 1;
                    }

                    if (!should_drop && corrupt_rate > 0.0f && ((float)rand() / (float)RAND_MAX) < corrupt_rate) {
                        should_corrupt = 1;
                    }
                }

                if (should_drop) {
                    printf("[PROXY] DROPPED Data Seq=%u (simulated network loss)\n", hdr.seq_num);
                    if (payload) free(payload);
                    continue;
                }

                if (should_corrupt && payload && hdr.payload_len > 0) {
                    payload[0] ^= 0xFF; /* Corrupt first payload byte */
                    printf("[PROXY] CORRUPTED Data Seq=%u (injected bit-flip)\n", hdr.seq_num);
                }

                if (delay_ms > 0) {
                    delay_millis(delay_ms);
                }

                send_all(target_fd, hdr_bytes, BMTP_HEADER_LEN);
                if (payload && hdr.payload_len > 0) {
                    send_all(target_fd, payload, hdr.payload_len);
                    free(payload);
                }
            }

            /* Data from Receiver -> Sender (ACK/NACK/ERROR) */
            if (fds[1].revents & POLLIN) {
                uint8_t hdr_bytes[BMTP_HEADER_LEN];
                int r = recv_all(target_fd, hdr_bytes, BMTP_HEADER_LEN);
                if (r != 0) {
                    active = 0;
                    break;
                }
                bmtp_header_t hdr;
                bmtp_unpack_header(hdr_bytes, &hdr);

                uint8_t *payload = NULL;
                if (hdr.payload_len > 0) {
                    payload = (uint8_t *)malloc(hdr.payload_len);
                    if (recv_all(target_fd, payload, hdr.payload_len) != 0) {
                        free(payload);
                        active = 0;
                        break;
                    }
                }

                send_all(client_fd, hdr_bytes, BMTP_HEADER_LEN);
                if (payload && hdr.payload_len > 0) {
                    send_all(client_fd, payload, hdr.payload_len);
                    free(payload);
                }
            }
        }

        CLOSE_SOCKET(client_fd);
        CLOSE_SOCKET(target_fd);
        printf("[PROXY] Session ended.\n");

    } while (!single_session);

    CLOSE_SOCKET(listen_fd);
    net_cleanup();
    return 0;
}
