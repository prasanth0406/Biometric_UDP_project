/* crc32.c - Table-based IEEE 802.3 CRC32 Implementation */
#include "crc32.h"

#define CRC32_POLYNOMIAL 0xEDB88320U

static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

void crc32_init(void) {
    if (crc32_table_initialized) {
        return;
    }
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) {
                c = CRC32_POLYNOMIAL ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        crc32_table[i] = c;
    }
    crc32_table_initialized = 1;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t length) {
    if (!crc32_table_initialized) {
        crc32_init();
    }
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = ~crc;
    for (size_t i = 0; i < length; i++) {
        c = crc32_table[(c ^ p[i]) & 0xFFU] ^ (c >> 8);
    }
    return ~c;
}

uint32_t crc32_calculate(const void *data, size_t length) {
    return crc32_update(0, data, length);
}
