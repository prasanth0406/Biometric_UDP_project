/* protocol.h - Biometric Transport Protocol (BMTP) Specification */
#ifndef BMTP_PROTOCOL_H
#define BMTP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define BMTP_MAGIC_0 0x42 /* 'B' */
#define BMTP_MAGIC_1 0x4D /* 'M' */
#define BMTP_MAGIC_2 0x54 /* 'T' */
#define BMTP_MAGIC_3 0x50 /* 'P' */
#define BMTP_VERSION 1

#define BMTP_HEADER_LEN 18

/* Message Types */
typedef enum {
    BMTP_MSG_META  = 1,
    BMTP_MSG_DATA  = 2,
    BMTP_MSG_ACK   = 3,
    BMTP_MSG_NACK  = 4,
    BMTP_MSG_END   = 5,
    BMTP_MSG_ERROR = 6
} bmtp_msg_type_t;

/* Header representation */
typedef struct {
    uint8_t magic[4];
    uint8_t version;
    uint8_t msg_type;
    uint32_t seq_num;
    uint32_t payload_len;
    uint32_t crc32;
} bmtp_header_t;

/* META Payload format */
#define BMTP_MAX_FILENAME 256

typedef struct {
    char filename[BMTP_MAX_FILENAME];
    uint64_t filesize;
    uint32_t chunk_size;
    uint32_t total_chunks;
    uint8_t sha256[32];
    uint32_t mode;        /* 0: stop-and-wait, 1: sliding-window */
    uint32_t window_size; /* Window N */
} bmtp_meta_t;

#define BMTP_META_PAYLOAD_LEN (BMTP_MAX_FILENAME + 8 + 4 + 4 + 32 + 4 + 4)

/**
 * @brief Serialize BMTP header into 18 bytes network buffer.
 */
void bmtp_pack_header(uint8_t *out_buf, const bmtp_header_t *hdr);

/**
 * @brief Deserialize 18 bytes from network buffer into BMTP header struct.
 * @return 0 on success, -1 on invalid magic or version.
 */
int bmtp_unpack_header(const uint8_t *in_buf, bmtp_header_t *hdr);

/**
 * @brief Serialize META struct into payload buffer.
 */
void bmtp_pack_meta(uint8_t *out_buf, const bmtp_meta_t *meta);

/**
 * @brief Deserialize payload buffer into META struct.
 */
int bmtp_unpack_meta(const uint8_t *in_buf, size_t len, bmtp_meta_t *meta);

/**
 * @brief Send a complete BMTP frame over TCP socket.
 */
int bmtp_send_frame(int sockfd, uint8_t msg_type, uint32_t seq_num,
                    const void *payload, uint32_t payload_len, int corrupt_crc);

/**
 * @brief Read a complete BMTP frame from TCP socket.
 * Allocates *payload_out if payload_len > 0 (caller must free).
 * @return 0 on success, 1 on EOF, -1 on socket error, -2 on CRC mismatch.
 */
int bmtp_recv_frame(int sockfd, bmtp_header_t *hdr, uint8_t **payload_out, int *crc_match);

const char *bmtp_msg_type_str(uint8_t type);

#endif /* BMTP_PROTOCOL_H */
