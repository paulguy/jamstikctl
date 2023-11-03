/*
 * Copyright 2023 paulguy <paulguy119@gmail.com>
 *
 * This file is part of jamstikctl.
 *
 * jamstikctl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * jamstikctl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with jamstikctl.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>

uint8_t decode_packed_uint8(const unsigned char *buf);
uint16_t decode_packed_uint16(const unsigned char *buf);
int16_t decode_packed_int16(const unsigned char *buf);
uint32_t decode_packed_uint32(const unsigned char *buf);
int32_t decode_packed_int32(const unsigned char *buf);
uint64_t decode_packed_uint64(const unsigned char *buf);
int64_t decode_packed_int64(const unsigned char *buf);
void encode_packed_uint8(uint8_t val, const unsigned char *buf);
void encode_packed_uint16(uint16_t val, const unsigned char *buf);
void encode_packed_int16(int16_t val, const unsigned char *buf);
void encode_packed_uint32(uint32_t val, const unsigned char *buf);
void encode_packed_int32(int32_t val, const unsigned char *buf);
void encode_packed_uint64(uint64_t val, const unsigned char *buf);
void encode_packed_int64(int64_t val, const unsigned char *buf);
