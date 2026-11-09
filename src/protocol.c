/* protocol.c - BMTP Framing, Serialization, and Socket Operations */
#include "protocol.h"
#include "crc32.h"
#include "net_util.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
#endif

static uint64_t hton64(uint64_t val) {
    uint32_t high = htonl((uint32_t)(val >> 32));
    uint32_t low  = htonl((uint32_t)(val & 0xFFFFFFFFULL));
    return ((uint64_t)low << 32) | high;
}

static uint64_t ntoh64(uint64_t val) {
    return hton64(val); /* Symmetric swap */
}

void bmtp_pack_header(uint8_t *out_buf, const bmtp_header_t *hdr) {
    out_buf[0] = hdr->magic[0];
    out_buf[1] = hdr->magic[1];
    out_buf[2] = hdr->magic[2];
    out_buf[3] = hdr->magic[3];
    out_buf[4] = hdr->version;
    out_buf[5] = hdr->msg_type;

    uint32_t net_seq = htonl(hdr->seq_num);
    uint32_t net_len = htonl(hdr->payload_len);
    uint32_t net_crc = htonl(hdr->crc32);

    memcpy(&out_buf[6],  &net_seq, 4);
    memcpy(&out_buf[10], &net_len, 4);
    memcpy(&out_buf[14], &net_crc, 4);
}

int bmtp_unpack_header(const uint8_t *in_buf, bmtp_header_t *hdr) {
    if (in_buf[0] != BMTP_MAGIC_0 || in_buf[1] != BMTP_MAGIC_1 ||
        in_buf[2] != BMTP_MAGIC_2 || in_buf[3] != BMTP_MAGIC_3) {
        return -1;
    }
    if (in_buf[4] != BMTP_VERSION) {
        return -1;
    }
    hdr->magic[0] = in_buf[0];
    hdr->magic[1] = in_buf[1];
    hdr->magic[2] = in_buf[2];
    hdr->magic[3] = in_buf[3];
    hdr->version  = in_buf[4];
    hdr->msg_type = in_buf[5];

    uint32_t net_seq, net_len, net_crc;
    memcpy(&net_seq, &in_buf[6],  4);
    memcpy(&net_len, &in_buf[10], 4);
    memcpy(&net_crc, &in_buf[14], 4);

    hdr->seq_num     = ntohl(net_seq);
    hdr->payload_len = ntohl(net_len);
    hdr->crc32       = ntohl(net_crc);
    return 0;
}

void bmtp_pack_meta(uint8_t *out_buf, const bmtp_meta_t *meta) {
    memset(out_buf, 0, BMTP_META_PAYLOAD_LEN);
    size_t off = 0;

    memcpy(&out_buf[off], meta->filename, BMTP_MAX_FILENAME);
    off += BMTP_MAX_FILENAME;

    uint64_t net_filesize = hton64(meta->filesize);
    memcpy(&out_buf[off], &net_filesize, 8);
    off += 8;

    uint32_t net_chunk_size = htonl(meta->chunk_size);
    memcpy(&out_buf[off], &net_chunk_size, 4);
    off += 4;

    uint32_t net_total_chunks = htonl(meta->total_chunks);
    memcpy(&out_buf[off], &net_total_chunks, 4);
    off += 4;

    memcpy(&out_buf[off], meta->sha256, 32);
    off += 32;

    uint32_t net_mode = htonl(meta->mode);
    memcpy(&out_buf[off], &net_mode, 4);
    off += 4;

    uint32_t net_win = htonl(meta->window_size);
    memcpy(&out_buf[off], &net_win, 4);
}

int bmtp_unpack_meta(const uint8_t *in_buf, size_t len, bmtp_meta_t *meta) {
    if (len < BMTP_META_PAYLOAD_LEN) {
        return -1;
    }
    size_t off = 0;
    memcpy(meta->filename, &in_buf[off], BMTP_MAX_FILENAME);
    meta->filename[BMTP_MAX_FILENAME - 1] = '\0';
    off += BMTP_MAX_FILENAME;

    uint64_t net_filesize;
    memcpy(&net_filesize, &in_buf[off], 8);
    meta->filesize = ntoh64(net_filesize);
    off += 8;

    uint32_t net_chunk_size, net_total_chunks, net_mode, net_win;
    memcpy(&net_chunk_size, &in_buf[off], 4);
    meta->chunk_size = ntohl(net_chunk_size);
    off += 4;

    memcpy(&net_total_chunks, &in_buf[off], 4);
    meta->total_chunks = ntohl(net_total_chunks);
    off += 4;

    memcpy(meta->sha256, &in_buf[off], 32);
    off += 32;

    memcpy(&net_mode, &in_buf[off], 4);
    meta->mode = ntohl(net_mode);
    off += 4;

    memcpy(&net_win, &in_buf[off], 4);
    meta->window_size = ntohl(net_win);
    return 0;
}

int bmtp_send_frame(int sockfd, uint8_t msg_type, uint32_t seq_num,
                    const void *payload, uint32_t payload_len, int corrupt_crc) {
    uint32_t crc = 0;
    if (payload && payload_len > 0) {
        crc = crc32_calculate(payload, payload_len);
        if (corrupt_crc) {
            crc ^= 0xFFFFFFFFU;
        }
    }

    bmtp_header_t hdr;
    hdr.magic[0] = BMTP_MAGIC_0;
    hdr.magic[1] = BMTP_MAGIC_1;
    hdr.magic[2] = BMTP_MAGIC_2;
    hdr.magic[3] = BMTP_MAGIC_3;
    hdr.version  = BMTP_VERSION;
    hdr.msg_type = msg_type;
    hdr.seq_num  = seq_num;
    hdr.payload_len = payload_len;
    hdr.crc32    = crc;

    uint8_t hdr_bytes[BMTP_HEADER_LEN];
    bmtp_pack_header(hdr_bytes, &hdr);

    if (send_all(sockfd, hdr_bytes, BMTP_HEADER_LEN) != 0) {
        return -1;
    }
    if (payload && payload_len > 0) {
        if (send_all(sockfd, payload, payload_len) != 0) {
            return -1;
        }
    }
    return 0;
}

int bmtp_recv_frame(int sockfd, bmtp_header_t *hdr, uint8_t **payload_out, int *crc_match) {
    uint8_t hdr_bytes[BMTP_HEADER_LEN];
    int res = recv_all(sockfd, hdr_bytes, BMTP_HEADER_LEN);
    if (res != 0) {
        return res; /* 1 on EOF, -1 on socket error */
    }

    if (bmtp_unpack_header(hdr_bytes, hdr) != 0) {
        return -1;
    }

    *crc_match = 1;
    if (payload_out) {
        *payload_out = NULL;
    }

    if (hdr->payload_len > 0) {
        /* Bounded allocation to prevent memory exhaustion from corrupt length */
        if (hdr->payload_len > 10 * 1024 * 1024) {
            return -1;
        }
        uint8_t *buf = (uint8_t *)malloc(hdr->payload_len);
        if (!buf) {
            return -1;
        }
        if (recv_all(sockfd, buf, hdr->payload_len) != 0) {
            free(buf);
            return -1;
        }
        uint32_t actual_crc = crc32_calculate(buf, hdr->payload_len);
        if (actual_crc != hdr->crc32) {
            *crc_match = 0;
        }
        if (payload_out) {
            *payload_out = buf;
        } else {
            free(buf);
        }
    }
    return 0;
}

const char *bmtp_msg_type_str(uint8_t type) {
    switch (type) {
        case BMTP_MSG_META:  return "META";
        case BMTP_MSG_DATA:  return "DATA";
        case BMTP_MSG_ACK:   return "ACK";
        case BMTP_MSG_NACK:  return "NACK";
        case BMTP_MSG_END:   return "END";
        case BMTP_MSG_ERROR: return "ERROR";
        default:             return "UNKNOWN";
    }
}
