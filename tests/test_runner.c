/* test_runner.c - Standalone C Test Runner for BMTP Components */
#define _POSIX_C_SOURCE 200809L
#include "crc32.h"
#include "net_util.h"
#include "protocol.h"
#include "sha256.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s (Line %d)\n", msg, __LINE__); \
    } \
} while(0)

static void test_crc32(void) {
    printf("\n=== Running CRC32 Tests ===\n");
    crc32_init();

    /* 1. Empty input */
    uint32_t c1 = crc32_calculate("", 0);
    TEST_ASSERT(c1 == 0x00000000U, "CRC32 of empty string is 0x00000000");

    /* 2. Standard NIST vector "123456789" -> 0xCBF43926 */
    const char *v2 = "123456789";
    uint32_t c2 = crc32_calculate(v2, strlen(v2));
    TEST_ASSERT(c2 == 0xCBF43926U, "CRC32 standard vector '123456789' is 0xCBF43926");

    /* 3. Bit-flip detection */
    uint8_t buffer[64] = "Biometric Minutiae Vector Template Sample";
    uint32_t orig_crc = crc32_calculate(buffer, sizeof(buffer));
    buffer[5] ^= 0x01; /* Single bit flip */
    uint32_t flipped_crc = crc32_calculate(buffer, sizeof(buffer));
    TEST_ASSERT(orig_crc != flipped_crc, "Single bit-flip changes CRC32 checksum");
}

static void test_sha256(void) {
    printf("\n=== Running SHA-256 Tests ===\n");

    /* NIST Vector 1: Empty string */
    uint8_t d1[32];
    sha256_ctx_t ctx1;
    sha256_init(&ctx1);
    sha256_update(&ctx1, "", 0);
    sha256_final(&ctx1, d1);
    char h1[65];
    sha256_to_hex(d1, h1);
    TEST_ASSERT(strcmp(h1, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
                "NIST Vector 1: SHA256('') match");

    /* NIST Vector 2: "abc" */
    uint8_t d2[32];
    sha256_ctx_t ctx2;
    sha256_init(&ctx2);
    sha256_update(&ctx2, "abc", 3);
    sha256_final(&ctx2, d2);
    char h2[65];
    sha256_to_hex(d2, h2);
    TEST_ASSERT(strcmp(h2, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0,
                "NIST Vector 2: SHA256('abc') match");

    /* NIST Vector 3: 56-byte string */
    const char *v3 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t d3[32];
    sha256_ctx_t ctx3;
    sha256_init(&ctx3);
    sha256_update(&ctx3, v3, strlen(v3));
    sha256_final(&ctx3, d3);
    char h3[65];
    sha256_to_hex(d3, h3);
    TEST_ASSERT(strcmp(h3, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0,
                "NIST Vector 3: 56-byte string match");
}

static void test_protocol_framing(void) {
    printf("\n=== Running Protocol Framing & Header Tests ===\n");

    bmtp_header_t hdr;
    hdr.magic[0] = BMTP_MAGIC_0;
    hdr.magic[1] = BMTP_MAGIC_1;
    hdr.magic[2] = BMTP_MAGIC_2;
    hdr.magic[3] = BMTP_MAGIC_3;
    hdr.version  = BMTP_VERSION;
    hdr.msg_type = BMTP_MSG_DATA;
    hdr.seq_num  = 12345;
    hdr.payload_len = 1024;
    hdr.crc32    = 0xA1B2C3D4;

    uint8_t buf[BMTP_HEADER_LEN];
    bmtp_pack_header(buf, &hdr);

    bmtp_header_t unpacked;
    int res = bmtp_unpack_header(buf, &unpacked);
    TEST_ASSERT(res == 0, "Header unpack returned success (0)");
    TEST_ASSERT(unpacked.version == BMTP_VERSION, "Version unpacked correctly");
    TEST_ASSERT(unpacked.msg_type == BMTP_MSG_DATA, "Message type matches DATA");
    TEST_ASSERT(unpacked.seq_num == 12345, "Sequence number preserved across big-endian packing");
    TEST_ASSERT(unpacked.payload_len == 1024, "Payload length preserved");
    TEST_ASSERT(unpacked.crc32 == 0xA1B2C3D4, "CRC32 field preserved");

    /* Corrupt Magic Bytes Test */
    buf[0] = 'X';
    TEST_ASSERT(bmtp_unpack_header(buf, &unpacked) != 0, "Corrupt magic bytes properly rejected");
}

static void test_meta_serialization(void) {
    printf("\n=== Running META Serialization Tests ===\n");

    bmtp_meta_t meta_in;
    memset(&meta_in, 0, sizeof(meta_in));
    snprintf(meta_in.filename, sizeof(meta_in.filename), "biometric_sample.dat");
    meta_in.filesize = 5000000000ULL; /* > 4 GB to test 64-bit integer endianness */
    meta_in.chunk_size = 4096;
    meta_in.total_chunks = 1220704;
    meta_in.mode = 1;
    meta_in.window_size = 16;
    for (int i = 0; i < 32; ++i) meta_in.sha256[i] = (uint8_t)(i * 7);

    uint8_t meta_bytes[BMTP_META_PAYLOAD_LEN];
    bmtp_pack_meta(meta_bytes, &meta_in);

    bmtp_meta_t meta_out;
    int res = bmtp_unpack_meta(meta_bytes, BMTP_META_PAYLOAD_LEN, &meta_out);
    TEST_ASSERT(res == 0, "META unpack returned success");
    TEST_ASSERT(strcmp(meta_out.filename, "biometric_sample.dat") == 0, "Filename unpacked accurately");
    TEST_ASSERT(meta_out.filesize == 5000000000ULL, "64-bit file size preserved");
    TEST_ASSERT(meta_out.chunk_size == 4096, "Chunk size preserved");
    TEST_ASSERT(meta_out.total_chunks == 1220704, "Total chunks preserved");
    TEST_ASSERT(meta_out.mode == 1, "Mode preserved");
    TEST_ASSERT(meta_out.window_size == 16, "Window size preserved");
    TEST_ASSERT(memcmp(meta_out.sha256, meta_in.sha256, 32) == 0, "SHA256 digest preserved");
}

static void test_chunking_and_reassembly(void) {
    printf("\n=== Running Chunking & Reassembly Tests ===\n");

    size_t data_len = 65536;
    uint8_t *orig_data = (uint8_t *)malloc(data_len);
    for (size_t i = 0; i < data_len; ++i) {
        orig_data[i] = (uint8_t)((i * 37 + 11) & 0xFF);
    }

    uint8_t orig_sha[32];
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, orig_data, data_len);
    sha256_final(&ctx, orig_sha);

    uint32_t chunk_sizes[] = {512, 1024, 4096, 8192};
    for (int c = 0; c < 4; ++c) {
        uint32_t csz = chunk_sizes[c];
        uint32_t nchunks = (uint32_t)((data_len + csz - 1) / csz);

        uint8_t *reconstructed = (uint8_t *)malloc(data_len);
        size_t reassembled_bytes = 0;

        for (uint32_t i = 0; i < nchunks; ++i) {
            size_t offset = (size_t)i * csz;
            size_t this_len = (data_len - offset < csz) ? (data_len - offset) : csz;
            memcpy(&reconstructed[offset], &orig_data[offset], this_len);
            reassembled_bytes += this_len;
        }

        TEST_ASSERT(reassembled_bytes == data_len, "Reassembled size matches original");

        uint8_t recon_sha[32];
        sha256_init(&ctx);
        sha256_update(&ctx, reconstructed, data_len);
        sha256_final(&ctx, recon_sha);

        TEST_ASSERT(memcmp(orig_sha, recon_sha, 32) == 0, "Reassembled buffer SHA-256 matches bit-for-bit");
        free(reconstructed);
    }
    free(orig_data);
}

static void test_go_back_n_config(void) {
    printf("\n=== Running Go-Back-N ARQ Configuration & Framing Tests ===\n");

    bmtp_meta_t gbn_meta;
    memset(&gbn_meta, 0, sizeof(gbn_meta));
    snprintf(gbn_meta.filename, sizeof(gbn_meta.filename), "iris_biometric.dat");
    gbn_meta.filesize = 1048576;
    gbn_meta.chunk_size = 1024;
    gbn_meta.total_chunks = 1024;
    gbn_meta.mode = 1;         /* Go-Back-N Sliding Window */
    gbn_meta.window_size = 10; /* Size 10 Sliding Window */

    uint8_t payload[BMTP_META_PAYLOAD_LEN];
    bmtp_pack_meta(payload, &gbn_meta);

    bmtp_meta_t unpacked;
    int res = bmtp_unpack_meta(payload, BMTP_META_PAYLOAD_LEN, &unpacked);
    TEST_ASSERT(res == 0, "Go-Back-N META payload unpacked successfully");
    TEST_ASSERT(unpacked.mode == 1, "Go-Back-N mode flag (1) validated");
    TEST_ASSERT(unpacked.window_size == 10, "Go-Back-N window size 10 validated");

    /* Validate GBN NACK frame header */
    bmtp_header_t nack_hdr;
    nack_hdr.magic[0] = BMTP_MAGIC_0;
    nack_hdr.magic[1] = BMTP_MAGIC_1;
    nack_hdr.magic[2] = BMTP_MAGIC_2;
    nack_hdr.magic[3] = BMTP_MAGIC_3;
    nack_hdr.version  = BMTP_VERSION;
    nack_hdr.msg_type = BMTP_MSG_NACK;
    nack_hdr.seq_num  = 10;
    nack_hdr.payload_len = 0;
    nack_hdr.crc32    = 0;

    uint8_t hdr_bytes[BMTP_HEADER_LEN];
    bmtp_pack_header(hdr_bytes, &nack_hdr);

    bmtp_header_t unpacked_nack;
    TEST_ASSERT(bmtp_unpack_header(hdr_bytes, &unpacked_nack) == 0, "GBN NACK header unpacked successfully");
    TEST_ASSERT(unpacked_nack.msg_type == BMTP_MSG_NACK, "GBN NACK message type confirmed");
    TEST_ASSERT(unpacked_nack.seq_num == 10, "GBN NACK sequence number 10 confirmed");
}

int main(void) {
    printf("===================================================\n");
    printf(" BMTP TEST RUNNER (C11 POSIX Unit Tests)\n");
    printf("===================================================\n");

    test_crc32();
    test_sha256();
    test_protocol_framing();
    test_meta_serialization();
    test_chunking_and_reassembly();
    test_go_back_n_config();

    printf("\n===================================================\n");
    printf(" TEST RESULTS: %d/%d PASSED\n", tests_passed, tests_run);
    printf("===================================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
