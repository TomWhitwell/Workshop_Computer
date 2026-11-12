/*
 * TELEXo Eurorack Module
 * (c) 2016 Brendon Cassidy
 * MIT License
 */
 
#ifndef _OPS_TELEX_H_
#define _OPS_TELEX_H_
 
// TELEXo

#define TO 0x60
#define TO_0 0x60
#define TO_1 0x61
#define TO_2 0x62
#define TO_3 0x63
#define TO_4 0x64
#define TO_5 0x65
#define TO_6 0x66
#define TO_7 0x67

// TXo Commands

#define TO_TR 0x00
#define TO_TR_TOG 0x01
#define TO_TR_TIME 0x02
#define TO_TR_TIME_S 0x03
#define TO_TR_TIME_M 0x04
#define TO_TR_PULSE 0x05
#define TO_TR_POL 0x06

#define TO_TR_PULSE_DIV 0x07
#define TO_TR_M 0x08
#define TO_TR_M_S 0x09
#define TO_TR_M_M 0x0A
#define TO_TR_M_BPM 0x0B
#define TO_TR_M_ACT 0x0C
#define TO_TR_M_SYNC 0x0D
#define TO_TR_WIDTH 0x0E
#define TO_TR_M_COUNT 0x0F

#define TO_CV 0x10
#define TO_CV_SET 0x11
#define TO_CV_SLEW 0x12
#define TO_CV_SLEW_S 0x13
#define TO_CV_SLEW_M 0x14
#define TO_CV_OFF 0x15

#define TO_KILL 0x20
#define TO_RESET 0x21

#define TO_CV_QT 0x30
#define TO_CV_QT_SET 0x31
#define TO_CV_N 0x32
#define TO_CV_N_SET 0x33
#define TO_CV_SCALE 0x34

#define TO_OSC 0x40
#define TO_OSC_SET 0x41
#define TO_OSC_QT 0x42
#define TO_OSC_QT_SET 0x43
#define TO_OSC_FQ 0x44
#define TO_OSC_FQ_SET 0x45
#define TO_OSC_N 0x46
#define TO_OSC_N_SET 0x47
#define TO_OSC_LFO 0x48
#define TO_OSC_LFO_SET 0x49
#define TO_OSC_WAVE 0x4A
#define TO_OSC_SYNC 0x4B
#define TO_OSC_WIDTH 0x4C
#define TO_OSC_RECT 0x4D
#define TO_OSC_SCALE 0x4E
#define TO_OSC_SLEW 0x4F
#define TO_OSC_SLEW_S 0x50
#define TO_OSC_SLEW_M 0x51
#define TO_OSC_TR_ACT 0x52
#define TO_OSC_PHASE 0x53
#define TO_OSC_CYC 0x54
#define TO_OSC_CYC_S 0x55
#define TO_OSC_CYC_M 0x56
#define TO_OSC_CYC_SET 0x57
#define TO_OSC_CYC_S_SET 0x58
#define TO_OSC_CYC_M_SET 0x59

#define TO_ENV_ACT 0x60
#define TO_ENV_ATT 0x61
#define TO_ENV_ATT_S 0x62
#define TO_ENV_ATT_M 0x63
#define TO_ENV_DEC 0x64
#define TO_ENV_DEC_S 0x65
#define TO_ENV_DEC_M 0x66
#define TO_ENV_TRIG 0x67
#define TO_ENV_CYC 0x68
#define TO_ENV_BPOL 0x69

// TELEXi

#define TI 0x68
#define TI_0 0x68
#define TI_1 0x69
#define TI_2 0x6A
#define TI_3 0x6B
#define TI_4 0x6C
#define TI_5 0x6D
#define TI_6 0x6E
#define TI_7 0x6F

// TXi Commands

#define TI_IN 0x00
#define TI_IN_QT 0x01
#define TI_IN_N 0x02
#define TI_IN_SCALE 0x03
#define TI_IN_TOP 0x04
#define TI_IN_BOT 0x05
#define TI_IN_MAP 0x06

#define TI_PARAM 0x10
#define TI_PARAM_QT 0x11
#define TI_PARAM_N 0x12
#define TI_PARAM_SCALE 0x13
#define TI_PARAM_TOP 0x14
#define TI_PARAM_BOT 0x15
#define TI_PARAM_MAP 0x16

#define TI_IN_CALIB 0x20
#define TI_PARAM_CALIB 0x21
#define TI_STORE 0x22
#define TI_RESET 0x23

#endif
