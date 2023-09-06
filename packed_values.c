#include <stdint.h>

#define B16(BUF, OFF) ((int16_t)(BUF[OFF]))
#define BU16(BUF, OFF) ((uint16_t)(BUF[OFF]))
#define B32(BUF, OFF) ((int32_t)(BUF[OFF]))
#define BU32(BUF, OFF) ((uint32_t)(BUF[OFF]))
#define B64(BUF, OFF) ((int64_t)(BUF[OFF]))
#define BU64(BUF, OFF) ((uint64_t)(BUF[OFF]))

uint8_t decode_packed_uint8(const unsigned char *buf) {
    return(((buf[0] & 0x7F) << 1) |
           ((buf[1] & 0x40) >> 6));
}

uint16_t decode_packed_uint16(const unsigned char *buf) {
    return(((BU16(buf, 0) & 0x7F) << 9) |
           ((BU16(buf, 1) & 0x7F) << 2) |
           ((BU16(buf, 2) & 0x60) >> 5));
}

int16_t decode_packed_int16(const unsigned char *buf) {
    return(((B16(buf, 0) & 0x7F) << 9) |
           ((B16(buf, 1) & 0x7F) << 2) |
           ((B16(buf, 2) & 0x60) >> 5));
}

uint32_t decode_packed_uint32(const unsigned char *buf) {
    return(((BU32(buf, 0) & 0x7F) << 25) |
           ((BU32(buf, 1) & 0x7F) << 18) |
           ((BU32(buf, 2) & 0x7F) << 11) |
           ((BU32(buf, 3) & 0x7F) <<  4) |
           ((BU32(buf, 4) & 0x78) >>  3));
}

int32_t decode_packed_int32(const unsigned char *buf) {
    return(((B32(buf, 0) & 0x7F) << 25) |
           ((B32(buf, 1) & 0x7F) << 18) |
           ((B32(buf, 2) & 0x7F) << 11) |
           ((B32(buf, 3) & 0x7F) <<  4) |
           ((B32(buf, 4) & 0x78) >>  3));
}

uint64_t decode_packed_uint64(const unsigned char *buf) {
    return(((BU64(buf, 0) & 0x7F) << 57) |
           ((BU64(buf, 1) & 0x7F) << 50) |
           ((BU64(buf, 2) & 0x7F) << 43) |
           ((BU64(buf, 3) & 0x7F) << 36) |
           ((BU64(buf, 4) & 0x7F) << 29) |
           ((BU64(buf, 5) & 0x7F) << 22) |
           ((BU64(buf, 6) & 0x7F) << 15) |
           ((BU64(buf, 7) & 0x7F) <<  8) |
           ((BU64(buf, 8) & 0x7F) <<  1) |
           ((BU64(buf, 9) & 0x40) >>  6));
}

int64_t decode_packed_int64(const unsigned char *buf) {
    return(((B64(buf, 0) & 0x7F) << 57) |
           ((B64(buf, 1) & 0x7F) << 50) |
           ((B64(buf, 2) & 0x7F) << 43) |
           ((B64(buf, 3) & 0x7F) << 36) |
           ((B64(buf, 4) & 0x7F) << 29) |
           ((B64(buf, 5) & 0x7F) << 22) |
           ((B64(buf, 6) & 0x7F) << 15) |
           ((B64(buf, 7) & 0x7F) <<  8) |
           ((B64(buf, 8) & 0x7F) <<  1) |
           ((B64(buf, 9) & 0x40) >>  6));
}
