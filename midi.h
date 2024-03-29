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

#include <pthread.h>

#include <jack/jack.h>

#define MIDI_MAX_BUFFER_SIZE (32768) /* should be plenty, I guess */

#define MIDI_CMD (0)
#define MIDI_SYSEX (0xF0)
#define MIDI_SYSEX_DUMMY_LEN (0x55)
#define MIDI_SYSEX_END (0xF7)
#define MIDI_SYSEX_VENDOR (1)
#define MIDI_SYSEX_VENDOR_LEN (3)
#define MIDI_SYSEX_BODY (MIDI_SYSEX_VENDOR + MIDI_SYSEX_VENDOR_LEN)
#define MIDI_SYSEX_HEAD (4)
#define MIDI_SYSEX_TAIL (2)

#define MIDI_CMD_MASK (0xF0)
#define MIDI_CHANNEL_MASK (0x0F)

#define MIDI_CMD_NOTE_OFF (0x80)
#define MIDI_CMD_NOTE_ON (0x90)
/* applie to above */
#define MIDI_CMD_NOTE_VEL (2)
#define MIDI_CMD_NOTE_SIZE (MIDI_CMD_NOTE_VEL+1)
#define MIDI_CMD_POLYTOUCH (0xA0)
#define MIDI_CMD_POLYTOUCH_PRESSURE (2)
#define MIDI_CMD_POLYTOUCH_SIZE (MIDI_CMD_POLYTOUCH_PRESSURE+1)
/* applies to above 3 */
#define MIDI_CMD_NOTE (1)

#define MIDI_CMD_CC (0xB0)
#define MIDI_CMD_CC_CONTROL (1)
#define MIDI_CMD_CC_VALUE (2)
#define MIDI_CMD_CC_SIZE (MIDI_CMD_CC_VALUE+1)

#define MIDI_CMD_PROGCH (0xC0)
#define MIDI_CMD_PROGCH_PROGRAM (1)
#define MIDI_CMD_PROGCH_SIZE (MIDI_CMD_PROGCH_PROGRAM+1)

#define MIDI_CMD_CHANTOUCH (0xD0)
#define MIDI_CMD_CHANTOUCH_PRESSURE (1)
#define MIDI_CMD_CHANTOUCH_SIZE (MIDI_CMD_CHANTOUCH_PRESSURE+1)

#define MIDI_CMD_PITCHBEND (0xE0)
#define MIDI_CMD_PITCHBEND_LOW (1)
#define MIDI_CMD_PITCHBEND_HIGH (2)
#define MIDI_CMD_PITCHBEND_SIZE (MIDI_CMD_PITCHBEND_HIGH+1)
#define MIDI_CMD_PITCHBEND_OFFSET (8192)

#define MIDI_2BYTE_WORD(HIGH, LOW)  ((LOW) | ((HIGH) << 7))
#define MIDI_2BYTE_WORD_LOW(X)      ((X) & 0x7F)
#define MIDI_2BYTE_WORD_HIGH(X)     (((X) >> 7) & 0x7F)
#define MIDI_2BYTE_WORD_MAX         MIDI_2BYTE_WORD(0x7F, 0x7F)

#define MIDI_CC_BANK_SELECT_MSB         (0)
#define MIDI_CC_MOD_WHEEL_MSB           (1)
#define MIDI_CC_BREATH_CONTROL_MSB      (2)
#define MIDI_CC_UNDEFINED_1_MSB         (3)
#define MIDI_CC_FOOT_PEDAL_MSB          (4)
#define MIDI_CC_PORTAMENTO_TIME_MSB     (5)
#define MIDI_CC_DATA_ENTRY_MSB          (6)
#define MIDI_CC_VOLUME_MSB              (7)
#define MIDI_CC_BALANCE_MSB             (8)
#define MIDI_CC_UNDEFINED_2_MSB         (9)
#define MIDI_CC_PAN_MSB                 (10)
#define MIDI_CC_EXPRESSION_MSB          (11)
#define MIDI_CC_EFFECT_CONTROL_1_MSB    (12)
#define MIDI_CC_EFFECT_CONTROL_2_MSB    (13)
#define MIDI_CC_UNDEFINED_3_MSB         (14)
#define MIDI_CC_UNDEFINED_4_MSB         (15)
#define MIDI_CC_GENERAL_PURPOSE_1_MSB   (16)
#define MIDI_CC_GENERAL_PURPOSE_2_MSB   (17)
#define MIDI_CC_GENERAL_PURPOSE_3_MSB   (18)
#define MIDI_CC_GENERAL_PURPOSE_4_MSB   (19)
#define MIDI_CC_UNDEFINED_5_MSB         (20)
#define MIDI_CC_UNDEFINED_6_MSB         (21)
#define MIDI_CC_UNDEFINED_7_MSB         (22)
#define MIDI_CC_UNDEFINED_8_MSB         (23)
#define MIDI_CC_UNDEFINED_9_MSB         (24)
#define MIDI_CC_UNDEFINED_10_MSB        (25)
#define MIDI_CC_UNDEFINED_11_MSB        (26)
#define MIDI_CC_UNDEFINED_12_MSB        (27)
#define MIDI_CC_UNDEFINED_13_MSB        (28)
#define MIDI_CC_UNDEFINED_14_MSB        (29)
#define MIDI_CC_UNDEFINED_15_MSB        (30)
#define MIDI_CC_UNDEFINED_16_MSB        (31)
#define MIDI_CC_BANK_SELECT_LSB         (32)
#define MIDI_CC_MOD_WHEEL_LSB           (33)
#define MIDI_CC_BREATH_CONTROL_LSB      (34)
#define MIDI_CC_UNDEFINED_1_LSB         (35)
#define MIDI_CC_FOOT_PEDAL_LSB          (36)
#define MIDI_CC_PORTAMENTO_TIME_LSB     (37)
#define MIDI_CC_DATA_ENTRY_LSB          (38)
#define MIDI_CC_VOLUME_LSB              (39)
#define MIDI_CC_BALANCE_LSB             (40)
#define MIDI_CC_UNDEFINED_2_LSB         (41)
#define MIDI_CC_PAN_LSB                 (42)
#define MIDI_CC_EXPRESSION_LSB          (43)
#define MIDI_CC_EFFECT_CONTROL_1_LSB    (44)
#define MIDI_CC_EFFECT_CONTROL_2_LSB    (45)
#define MIDI_CC_UNDEFINED_3_LSB         (46)
#define MIDI_CC_UNDEFINED_4_LSB         (47)
#define MIDI_CC_GENERAL_PURPOSE_1_LSB   (48)
#define MIDI_CC_GENERAL_PURPOSE_2_LSB   (49)
#define MIDI_CC_GENERAL_PURPOSE_3_LSB   (50)
#define MIDI_CC_GENERAL_PURPOSE_4_LSB   (51)
#define MIDI_CC_UNDEFINED_5_LSB         (52)
#define MIDI_CC_UNDEFINED_6_LSB         (53)
#define MIDI_CC_UNDEFINED_7_LSB         (54)
#define MIDI_CC_UNDEFINED_8_LSB         (55)
#define MIDI_CC_UNDEFINED_9_LSB         (56)
#define MIDI_CC_UNDEFINED_10_LSB        (57)
#define MIDI_CC_UNDEFINED_11_LSB        (58)
#define MIDI_CC_UNDEFINED_12_LSB        (59)
#define MIDI_CC_UNDEFINED_13_LSB        (60)
#define MIDI_CC_UNDEFINED_14_LSB        (61)
#define MIDI_CC_UNDEFINED_15_LSB        (62)
#define MIDI_CC_UNDEFINED_16_LSB        (63)
#define MIDI_CC_DAMPER_MODE             (64)
#define MIDI_CC_PORTAMENTO_MODE         (65)
#define MIDI_CC_SOSTENUDO_MODE          (66)
#define MIDI_CC_SOFT_MODE               (67)
#define MIDI_CC_LEGATO_MODE             (68)
#define MIDI_CC_HOLD_2_MODE             (69)
#define MIDI_CC_SOUND_CONTROL_1         (70)
#define MIDI_CC_SOUND_CONTROL_2         (71)
#define MIDI_CC_SOUND_CONTROL_3         (72)
#define MIDI_CC_SOUND_CONTROL_4         (73)
#define MIDI_CC_SOUND_CONTROL_5         (74)
#define MIDI_CC_SOUND_CONTROL_6         (75)
#define MIDI_CC_SOUND_CONTROL_7         (76)
#define MIDI_CC_SOUND_CONTROL_8         (77)
#define MIDI_CC_SOUND_CONTROL_9         (78)
#define MIDI_CC_SOUND_CONTROL_10        (79)
#define MIDI_CC_GENERAL_PURPOSE_5       (80)
#define MIDI_CC_GENERAL_PURPOSE_6       (81)
#define MIDI_CC_GENERAL_PURPOSE_7       (82)
#define MIDI_CC_GENERAL_PURPOSE_8       (83)
#define MIDI_CC_PORTAMENTO              (84)
#define MIDI_CC_UNDEFINED_17            (85)
#define MIDI_CC_UNDEFINED_18            (86)
#define MIDI_CC_UNDEFINED_19            (87)
#define MIDI_CC_HIRES_VELOCITY_PREFIX   (88)
#define MIDI_CC_UNDEFINED_20            (89)
#define MIDI_CC_UNDEFINED_21            (90)
#define MIDI_CC_FX_1_DEPTH              (91)
#define MIDI_CC_FX_2_DEPTH              (92)
#define MIDI_CC_FX_3_DEPTH              (93)
#define MIDI_CC_FX_4_DEPTH              (94)
#define MIDI_CC_FX_5_DEPTH              (95)
#define MIDI_CC_DATA_INCREMENT          (96)
#define MIDI_CC_DATA_DECREMENT          (97)
#define MIDI_CC_NRPN_LSB                (98)
#define MIDI_CC_NRPN_MSB                (99)
#define MIDI_CC_RPN_LSB                 (100)
#define MIDI_CC_RPN_MSB                 (101)
#define MIDI_CC_UNDEFINED_22            (102)
#define MIDI_CC_UNDEFINED_23            (103)
#define MIDI_CC_UNDEFINED_24            (104)
#define MIDI_CC_UNDEFINED_25            (105)
#define MIDI_CC_UNDEFINED_26            (106)
#define MIDI_CC_UNDEFINED_27            (107)
#define MIDI_CC_UNDEFINED_28            (108)
#define MIDI_CC_UNDEFINED_29            (109)
#define MIDI_CC_UNDEFINED_30            (110)
#define MIDI_CC_UNDEFINED_31            (111)
#define MIDI_CC_UNDEFINED_32            (112)
#define MIDI_CC_UNDEFINED_33            (113)
#define MIDI_CC_UNDEFINED_34            (114)
#define MIDI_CC_UNDEFINED_35            (115)
#define MIDI_CC_UNDEFINED_36            (116)
#define MIDI_CC_UNDEFINED_37            (117)
#define MIDI_CC_UNDEFINED_38            (118)
#define MIDI_CC_UNDEFINED_39            (119)
#define MIDI_CC_ALL_SOUND_OFF           (120)
#define MIDI_CC_RESET_ALL_CONTROLLERS   (121)
#define MIDI_CC_LOCAL_CONTROL_MODE      (122)
#define MIDI_CC_ALL_NOTES_OFF           (123)
#define MIDI_CC_OMNI_MODE_OFF           (124)
#define MIDI_CC_OMNI_MODE_ON            (125)
#define MIDI_CC_MONO_MODE_ON            (126)
#define MIDI_CC_POLY_MODE_ON            (127)

#define MIDI_RPN_PITCH_BEND_SENSITIVITY     MIDI_2BYTE_WORD(0, 0)
#define MIDI_RPN_CHANNEL_FINE_TUNING        MIDI_2BYTE_WORD(0, 1)
#define MIDI_RPN_CHANNEL_COARSE_TUNING      MIDI_2BYTE_WORD(0, 2)
#define MIDI_RPN_TUNING_PROGRAM_CHANGE      MIDI_2BYTE_WORD(0, 3)
#define MIDI_RPN_TUNING_BANK_SELECT         MIDI_2BYTE_WORD(0, 4)
#define MIDI_RPN_MODULATION_DEPTH_CHANGE    MIDI_2BYTE_WORD(0, 5)
#define MIDI_RPN_MPE_CONFIGURATION_MESSAGE  MIDI_2BYTE_WORD(0, 6)
#define MIDI_RPN_3D_AZIMUTH                 MIDI_2BYTE_WORD(0x3D, 0)
#define MIDI_RPN_3D_ELEVATION               MIDI_2BYTE_WORD(0x3D, 1)
#define MIDI_RPN_3D_GAIN                    MIDI_2BYTE_WORD(0x3D, 2)
#define MIDI_RPN_3D_DISTANCE_RATIO          MIDI_2BYTE_WORD(0x3D, 3)
#define MIDI_RPN_3D_MAXIMUM_DISTANCE        MIDI_2BYTE_WORD(0x3D, 4)
#define MIDI_RPN_3D_GAIN_AT_MAX_DISTANCE    MIDI_2BYTE_WORD(0x3D, 5)
#define MIDI_RPN_3D_REFERENCE_DISTANCE_RATIO MIDI_2BYTE_WORD(0x3D, 6)
#define MIDI_RPN_3D_PAN_SPREAD_ANGLE        MIDI_2BYTE_WORD(0x3D, 7)
#define MIDI_RPN_3D_ROLL_ANGLE              MIDI_2BYTE_WORD(0x3D, 8)
#define MIDI_RPN_NULL                       MIDI_2BYTE_WORD(0x7F, 0x7F)

void print_hex(size_t size, unsigned char *buffer);
char *midi_copy_string(const char *src);

int midi_setup(const char *client_name, const char *inport_name,
               const char *outport_name, const char *thruport_name,
               int filter_sysex, pthread_t pid);
char *midi_find_port(const char *pattern, unsigned long flags);
int midi_ready();
void midi_cleanup();
int midi_activated();
int midi_read_event(size_t size, unsigned char *buffer);
int midi_write_event(size_t size, unsigned char *buffer);
int midi_attach_in_port_by_name(const char *name);
int midi_attach_out_port_by_name(const char *name);
int midi_num_to_note(size_t size, char *buf, unsigned int note, int flat);
const char *midi_cc_to_string(unsigned int cc);
const char *midi_rpn_to_string(unsigned short rpn);
int midi_parse_rpn(unsigned char channel, unsigned short rpn, unsigned short data);
