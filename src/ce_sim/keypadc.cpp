
#include "platform.h"
#include "debug.h"
#include "ce_sim.h"
#include "keypadc.h"

#include "fxcg/system.h"

#if !TARGET_WINSIM
// returns true if the key is down, false if up
bool keyDown_fast(unsigned char keyCode) {
	static const unsigned short* keyboard_register = (unsigned short*)0xA44B0000;

	int row, col, word, bit;
	row = keyCode % 10;
	col = keyCode / 10 - 1;
	word = row >> 1;
	bit = col + 8 * (row & 1);
	return (keyboard_register[word] & (1 << bit));
}
#else
extern bool keyDown_fast(unsigned char keyCode);
#endif

uint8_t kb_Data[8] = { 0 };

/**
 * Keypad Data registers
 * TI-84+ CE
 * | Offset | Bit 0      | Bit 1      | Bit 2      | Bit 3      |  Bit 4     |  Bit 5     |  Bit 6     | Bit 7      |
 * | -------| ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- |
 * | 1      | kb_Graph   | kb_Trace   | kb_Zoom    | kb_Window  | kb_Yequ    | kb_2nd     | kb_Mode    | kb_Del     |
 * | 2      |            | kb_Sto     | kb_Ln      | kb_Log     | kb_Square  | kb_Recip   | kb_Math    | kb_Alpha   |
 * | 3      | kb_0       | kb_1       | kb_4       | kb_7       | kb_Comma   | kb_Sin     | kb_Apps    | kb_GraphVar|
 * | 4      | kb_DecPnt  | kb_2       | kb_5       | kb_8       | kb_LParen  | kb_Cos     | kb_Prgm    | kb_Stat    |
 * | 5      | kb_Chs     | kb_3       | kb_6       | kb_9       | kb_RParen  | kb_Tan     | kb_Vars    |            |
 * | 6      | kb_Enter   | kb_Add     | kb_Sub     | kb_Mul     | kb_Div     | kb_Power   | kb_Clear   |            |
 * | 7      | kb_Down    | kb_Left    | kb_Right   | kb_Up      |            |            |            |            |
 * Prizm:
 * | Offset | Bit 0      | Bit 1      | Bit 2      | Bit 3      |  Bit 4     |  Bit 5     |  Bit 6     | Bit 7      |
 * | -------| ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- |
 * | 1      | F5 (39)    | F4 (49)    | F3 (59)    | F2 (69)    | F1 (79)    | Shift (78) | Optn (68)  | Vars (58)  |
 * | 2      |            | -> (25)    | S<->D (65) | Exit (47)  | Menu (48)  | Frac (75)  | X,O,T (76) | Alpha (77) |
 * | 3      | 0 (71)     | 1 (72)     | 4 (73)     | 7 (74)     | Comma (35) | Sin (46)   | log (66)   | x^2 (67)   |
 * | 4      | . (61)     | 2 (62)     | 5 (63)     | 8 (64)     | ( (55)     | Cos (36)   | ln (56)    | ^ (57)     |
 * | 5      | negate (41)| 3 (52)     | 6 (53)     | 9 (54)     | ) (45)     | Tan (26)   | F6 (29)    |            |
 * | 6      | EXE (31)   | + (42)     | - (32)     | x (43)     | / (33)     | x10 (51)   | Del (44)   |            |
 * | 7      | Down (37)  | Left (38)  | Right (27) | Up (28)    |            |            |            |            |
 */

const int groups[8 * 7] = {
   39, 49, 59, 69, 79, 78, 68, 58,
   00, 25, 65, 47, 48, 75, 76, 77,
   71, 72, 73, 74, 35, 46, 66, 67,
   61, 62, 63, 64, 55, 36, 56, 57,
   41, 52, 53, 54, 45, 26, 29, 0 ,
   31, 42, 32, 43, 33, 51, 44, 0 ,
   37, 38, 27, 28, 0 , 0 , 0 , 0 ,
};

void kb_Scan(void) {
	const int* scan = &groups[0];
	for (int x = 1; x < 8; x++) {
		uint8_t scanValue = 0;
		for (int b = 1; b < 0x100; b <<= 1, scan++) {
			if (*scan && keyDown_fast(*scan)) {
				scanValue |= b;
			}
		}
		kb_Data[x] = scanValue;
	}
}

kb_key_t kb_ScanGroup(uint8_t row) {
	const int* scan = &groups[row-1];
	uint8_t scanValue = 0;
	for (int b = 1; b < 0x100; b <<= 1, scan++) {
		if (*scan && keyDown_fast(*scan)) {
			scanValue |= b;
		}
	}
	return scanValue;
}