/* capture.c - Live Packet Capture to PCAP using libpcap */
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <pcap.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile int keep_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

int main(int argc, char *argv[]) {
    char iface[64] = "lo";
    int port = 5001;
    char pcap_path[256] = "";

    int opt;
    while ((opt = getopt(argc, argv, "i:p:o:h")) != -1) {
        switch (opt) {
            case 'i': snprintf(iface, sizeof(iface), "%s", optarg); break;
            case 'p': port = atoi(optarg); break;
            case 'o': snprintf(pcap_path, sizeof(pcap_path), "%s", optarg); break;
            case 'h':
            default:
                fprintf(stderr, "Usage: %s [-i iface] [-p port] [-o out.pcap]\n", argv[0]);
                return 1;
        }
    }

    if (strlen(pcap_path) == 0) {
        time_t now = time(NULL);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm_buf);
        snprintf(pcap_path, sizeof(pcap_path), "logs/capture_%s.pcap", ts);
    }

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle = pcap_open_live(iface, 65535, 1, 100, errbuf);
    if (!handle) {
        fprintf(stderr, "[ERROR] Couldn't open device %s: %s\n", iface, errbuf);
        return 1;
    }

    char filter_exp[64];
    snprintf(filter_exp, sizeof(filter_exp), "tcp port %d", port);

    struct bpf_program fp;
    if (pcap_compile(handle, &fp, filter_exp, 0, PCAP_NETMASK_UNKNOWN) == -1) {
        fprintf(stderr, "[ERROR] Couldn't parse filter %s: %s\n", filter_exp, pcap_geterr(handle));
        pcap_close(handle);
        return 1;
    }

    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "[ERROR] Couldn't install filter %s: %s\n", filter_exp, pcap_geterr(handle));
        pcap_freecode(&fp);
        pcap_close(handle);
        return 1;
    }
    pcap_freecode(&fp);

    pcap_dumper_t *dumper = pcap_dump_open(handle, pcap_path);
    if (!dumper) {
        fprintf(stderr, "[ERROR] Couldn't open output file %s: %s\n", pcap_path, pcap_geterr(handle));
        pcap_close(handle);
        return 1;
    }

    printf("[CAPTURE] Capturing on %s with BPF '%s' -> %s\n", iface, filter_exp, pcap_path);
    printf("[CAPTURE] Press Ctrl+C to terminate capture.\n");

    int packet_count = 0;
    while (keep_running) {
        struct pcap_pkthdr *header;
        const u_char *pkt_data;
        int res = pcap_next_ex(handle, &header, &pkt_data);
        if (res == 1) {
            pcap_dump((u_char *)dumper, header, pkt_data);
            packet_count++;
        } else if (res < 0) {
            break;
        }
    }

    pcap_dump_close(dumper);
    pcap_close(handle);

    printf("\n[CAPTURE] Capture finished. %d packets recorded to '%s'.\n", packet_count, pcap_path);
    return 0;
}
