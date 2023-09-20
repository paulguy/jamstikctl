#include <stdint.h>

#define B16(BUF, OFF) ((uint16_t)(BUF[OFF]))
#define B32(BUF, OFF) ((uint32_t)(BUF[OFF]))
#define B64(BUF, OFF) ((uint64_t)(BUF[OFF]))

uint8_t decode_packed_uint8(const unsigned char *buf) {
    return(((buf[0] & 0x7F) << 1) |
           ((buf[1] & 0x40) >> 6));
}

uint16_t decode_packed_uint16(const unsigned char *buf) {
    return(((B16(buf, 0) & 0x7F) << 9) |
           ((B16(buf, 1) & 0x7F) << 2) |
           ((B16(buf, 2) & 0x60) >> 5));
}

int16_t decode_packed_int16(const unsigned char *buf) {
    return((int16_t)decode_packed_uint16(buf));
}

uint32_t decode_packed_uint32(const unsigned char *buf) {
    return(((B32(buf, 0) & 0x7F) << 25) |
           ((B32(buf, 1) & 0x7F) << 18) |
           ((B32(buf, 2) & 0x7F) << 11) |
           ((B32(buf, 3) & 0x7F) <<  4) |
           ((B32(buf, 4) & 0x78) >>  3));
}

int32_t decode_packed_int32(const unsigned char *buf) {
    return((int32_t)decode_packed_uint32(buf));
}

uint64_t decode_packed_uint64(const unsigned char *buf) {
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

int64_t decode_packed_int64(const unsigned char *buf) {
    return((int64_t)decode_packed_uint64(buf));
}

void encode_packed_uint8(uint8_t val, unsigned char *buf) {
    buf[0] = (val >> 1) & 0x7F;
    buf[1] = (val << 6) & 0x40;
}

void encode_packed_uint16(uint16_t val, unsigned char *buf) {
    buf[0] = (val >> 9) & 0x7F;
    buf[1] = (val >> 2) & 0x7F;
    buf[2] = (val << 5) & 0x60;
}

void encode_packed_int16(int16_t val, unsigned char *buf) {
    encode_packed_uint16((uint16_t)val, buf);
}

void encode_packed_uint32(uint32_t val, unsigned char *buf) {
    buf[0] = (val >> 25) & 0x7F;
    buf[1] = (val >> 18) & 0x7F;
    buf[2] = (val >> 11) & 0x7F;
    buf[3] = (val >>  4) & 0x7F;
    buf[4] = (val <<  3) & 0x78;
}

void encode_packed_int32(int32_t val, unsigned char *buf) {
    encode_packed_uint32((uint32_t)val, buf);
}

void encode_packed_uint64(uint64_t val, unsigned char *buf) {
    buf[0] = (val >> 57) & 0x7F;
    buf[1] = (val >> 50) & 0x7F;
    buf[2] = (val >> 43) & 0x7F;
    buf[3] = (val >> 36) & 0x7F;
    buf[4] = (val >> 29) & 0x7F;
    buf[5] = (val >> 22) & 0x7F;
    buf[6] = (val >> 15) & 0x7F;
    buf[7] = (val >>  8) & 0x7F;
    buf[8] = (val >>  1) & 0x7F;
    buf[9] = (val <<  6) & 0x40;
}

void encode_packed_int64(int64_t val, unsigned char *buf) {
    encode_packed_uint64((uint64_t)val, buf);
}
