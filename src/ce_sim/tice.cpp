
#include "platform.h"
#include "debug.h"
#include "ce_sim.h"
#include "tice.h"

#include "fxcg/system.h"

extern "C" {
	extern bool keyDown_fast(unsigned char keyCode);
};

static uint8_t findKey() {
	for (int32 i = 27; i <= 79; i++) {
		if (keyDown_fast(i)) {
			return i;
		}
	}
	return 0;
}

void delay(uint16_t msec) {
	CMT_Delay_100micros(msec * 10);
}

sk_key_t os_GetCSC(void) {
	return findKey();
}

int24_t os_RealToInt24(const real_t *arg) {
	return 0;
}

void os_SetCursorPos(uint8_t curRow, uint8_t curCol) {
	if (curRow == 0 && curCol == 0) {
		reset_printf();
	} else {
		// only the one case supported right now
	}
}

uint24_t os_PutStrFull(const char *string) {
	printf("%s", string);
	return 1;
}
