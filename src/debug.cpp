#include "platform.h"
#include "debug.h"

#include "calctype/fonts/arial_small/arial_small.c"

static int printY = 0;
void reset_printf() {
	printY = 0;
}

void ScreenPrint(char* buffer) {
	int x = 5;
	bool newline = buffer[strlen(buffer) - 1] == '\n';
	if (newline) buffer[strlen(buffer) - 1] = 0;
	CalcType_Draw(&arial_small, buffer, x, printY + 2, COLOR_WHITE, 0, 0);
	printY = (printY + arial_small.height) % 224;
}

#if DEBUG
#define BORDER "<><><><><><><><><><><><><><><><><><><><>\n"

void failedAssert(const char* assertion) {
	OutputLog("Failed assert:\n");
	OutputLog(assertion);
	OutputLog("\n");

#if TARGET_WINSIM
	DebugBreak();
#endif
}

#endif

