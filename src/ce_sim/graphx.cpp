
#include "platform.h"
#include "debug.h"
#include "ce_sim.h"
#include "graphx.h"
#include "keypadc.h"

#include "calctype/fonts/arial_small/arial_small.h"

#include "fxcg/display.h"

// the back buffer exits in VRAM area on the calculator and we use DMA to blit it, but
// we need an extra buffer for the simulator since it doesn't use DMA
static uint16_t* BackBuffer() {
#if TARGET_PRIZM
	return (uint16_t*) GetVRAMAddress();
#else
	static uint16_t StaticBuffer[gfx_lcdWidth*gfx_lcdHeight];
	return StaticBuffer;
#endif
}

#if TARGET_PRIZM
#define LCD_GRAM	0x202
#define LCD_BASE	0xB4000000
#define SYNCO() __asm__ volatile("SYNCO\n\t":::"memory");

// DMA0 operation register
#define DMA0_DMAOR	(volatile unsigned short*)0xFE008060
#define DMA0_CHCR_0	(volatile unsigned*)0xFE00802C
#define DMA0_SAR_0	(volatile unsigned*)0xFE008020
#define DMA0_DAR_0	(volatile unsigned*)0xFE008024
#define DMA0_TCR_0	(volatile unsigned*)0xFE008028

static inline void DmaWaitNext(void) {
	while (1) {
		if ((*DMA0_DMAOR) & 4)//Address error has occurred stop looping
			break;
		if ((*DMA0_CHCR_0) & 2)//Transfer is done
			break;
	}

	SYNCO();
	*DMA0_CHCR_0 &= ~1;
	*DMA0_DMAOR = 0;
}

static inline void DmaDrawStrip(void* srcAddress, unsigned int size) {
	// disable dma so we can issue new command
	*DMA0_CHCR_0 &= ~1;
	*DMA0_DMAOR = 0;

	*((volatile unsigned*)MSTPCR0) &= ~(1 << 21);//Clear bit 21
	*DMA0_SAR_0 = ((unsigned int)srcAddress) & 0x1FFFFFFF;           //Source address is IL memory (avoids bus conflicts)
	*DMA0_DAR_0 = LCD_BASE & 0x1FFFFFFF;				//Destination is LCD
	*DMA0_TCR_0 = size / 32;							//Transfer count bytes/32
	*DMA0_CHCR_0 = 0x00101400;
	*DMA0_DMAOR |= 1;//Enable DMA on all channels
	*DMA0_DMAOR &= ~6;//Clear flags

	*DMA0_CHCR_0 |= 1;//Enable channel0 DMA
}
#endif

struct GraphX_Context {
	int Clip_MinX = 0;
	int Clip_MinY = 0;
	int Clip_MaxX = gfx_lcdWidth;
	int Clip_MaxY = gfx_lcdHeight;

	int TextX = 0;
	int TextY = 0;

	uint8_t TransparentIndex = 0;

	uint16_t Palette[256];

	uint16_t CurColor;
	uint8_t LastColorIndex;

	uint16_t CurTextColor;
	uint8_t LastTextColorIndex;

	uint16_t CurTextBGColor;
	uint8_t LastTextBGColorIndex;

	uint16_t CurTextClearColor;
	uint8_t LastTextClearColorIndex;

	void ClipRect(int& x, int& y, int& width, int& height) {
		int x2 = x + width;
		int y2 = y + height;
		x = max(x, Clip_MinX);
		y = max(y, Clip_MinY);
		x2 = min(x2, Clip_MaxX);
		y2 = min(y2, Clip_MaxY);
		width = x2 - x;
		height = y2 - y;
	}

	void ClipSprite(gfx_sprite_t* sprite, int x, int y, int& outX0, int& outY0, int& outW, int& outH) {
		int x1 = max(x, Clip_MinX);
		int y1 = max(y, Clip_MinY);
		int x2 = min(x + sprite->width, Clip_MaxX);
		int y2 = min(y + sprite->height, Clip_MaxY);

		outX0 = x1 - x;
		outY0 = y1 - y;
		outW = x2 - x;
		outH = y2 - y;
	}

	void ClipRLESprite(gfx_rletsprite_t* sprite, int x, int y, int& outX0, int& outY0, int& outW, int& outH) {
		int x1 = max(x, Clip_MinX);
		int y1 = max(y, Clip_MinY);
		int x2 = min(x + sprite->width, Clip_MaxX);
		int y2 = min(y + sprite->height, Clip_MaxY);

		outX0 = x1 - x;
		outY0 = y1 - y;
		outW = x2 - x;
		outH = y2 - y;
	}

	uint16_t ResolvePalette(uint8_t PaletteIndex) {
		return Palette[PaletteIndex];
	}
};

#if TARGET_WINSIM
struct ClipChecker {
	uint16_t hashTop;
	uint16_t hashBottom;

	uint16_t Hash(uint16_t* row) {
		uint16_t val = 0x1E37;
		for (int x = 0; x < gfx_lcdWidth; x++) {
			val = (val << 1) ^ (row[x]) ^ (val >> 15);
		}
		return val;
	}

	uint16_t HashTop() {
		return Hash(BackBuffer() - gfx_lcdWidth);
	}

	uint16_t HashBottom() {
		return Hash(BackBuffer() + gfx_lcdWidth * gfx_lcdHeight);
	}

	ClipChecker() {
		hashTop = HashTop();
		hashBottom = HashBottom();
	}

	~ClipChecker() {
		DebugAssert(hashTop == HashTop());
		DebugAssert(hashBottom == HashBottom());
	}
};

#define CheckClip() ClipChecker _checker;
#else
#define CheckClip()
#endif

static GraphX_Context GFX;

uint16_t* GetTargetAddr(int x, int y) {
	return BackBuffer() + gfx_lcdWidth * y + x;
}

template<bool bClip, bool bTransparent>
void RenderSprite(gfx_sprite_t *sprite, int x, int y) {
	CheckClip();

	if (!sprite)
		return;

	int x0, y0, w, h;
	if (bClip) {
		GFX.ClipSprite(sprite, x, y, x0, y0, w, h);
	} else {
		x0 = 0; y0 = 0; w = sprite->width; h = sprite->height;
	}

	uint8_t* spriteData = sprite->data;
	uint16_t* targetLine = GetTargetAddr(x, y);

	if (y0) {
		spriteData += y0 * sprite->width;
		targetLine += y0 * gfx_lcdWidth;
	}

	for (; y0 < h; y0++) {
		for (int xPix = x0; xPix < w; xPix++) {
			if (!bTransparent || spriteData[xPix] != GFX.TransparentIndex) {
				targetLine[xPix] = GFX.ResolvePalette(spriteData[xPix]);
			}
		}
		spriteData += sprite->width;
		targetLine += gfx_lcdWidth;
	}
}

template<bool bClip>
void RenderRLESprite(gfx_rletsprite_t *sprite, int x, int y) {
	CheckClip();

	if (!sprite)
		return;

	int x0, y0, w, h;
	if (bClip) {
		GFX.ClipRLESprite(sprite, x, y, x0, y0, w, h);
	} else {
		x0 = 0; y0 = 0; w = sprite->width; h = sprite->height;
	}

	uint8_t* spriteData = sprite->data;
	uint16_t* targetLine = GetTargetAddr(x, y+y0);

	if (y0) {
		for (int lines = 0; lines < y0; lines++) {
			int curWidth = sprite->width;
			bool bTransparent = true;
			while (curWidth > 0) {
				uint8 curRun = *(spriteData++);
				if (!bTransparent) {
					spriteData += curRun;
				}
				bTransparent = !bTransparent;
				curWidth -= curRun;
			}
		}
	}

	for (; y0 < h; y0++) {
		int curWidth = sprite->width;
		int xPix = 0;
		bool bTransparent = true;
		while (curWidth > 0) {
			uint8 curRun = *(spriteData++);
			if (!bTransparent) {
				for (uint8 pix = 0; pix < curRun; pix++, xPix++) {
					if (!bClip || (xPix >= x0 && xPix < w)) {
						targetLine[xPix] = GFX.ResolvePalette(*spriteData);
					}
					spriteData++;
				}
			} else {
				xPix += curRun;
			}
			bTransparent = !bTransparent;
			curWidth -= curRun;
		}
		targetLine += gfx_lcdWidth;
	}
}

void gfx_Sprite(gfx_sprite_t *sprite, int x, int y) {
	RenderSprite<true, false>(sprite, x, y);
}

void gfx_Sprite_NoClip(gfx_sprite_t *sprite, uint24_t x, uint8_t y) {
	RenderSprite<false, false>(sprite, x, y);
}

void gfx_TransparentSprite(gfx_sprite_t *sprite, int x, int y) {
	RenderSprite<true, true>(sprite, x, y);
}

void gfx_TransparentSprite_NoClip(gfx_sprite_t *sprite, uint24_t x, uint8_t y) {
	RenderSprite<false, true>(sprite, x, y);
}

void gfx_ScaledTransparentSprite_NoClip(gfx_sprite_t *sprite,
	uint24_t x,
	uint8_t y,
	uint8_t width_scale,
	uint8_t height_scale) {

	CheckClip();

	uint8_t* spriteData = sprite->data;
	uint16_t* targetLine = GetTargetAddr(x, y);
	const unsigned int pitch = gfx_lcdWidth * height_scale;

	for (int y0 = 0; y0 < sprite->height; y0++) {
		uint16_t* bufferTarget = targetLine;
		for (int x0 = 0; x0 < sprite->width; x0++, spriteData++) {
			if (*spriteData != GFX.TransparentIndex) {
				int yOffset = 0;
				for (int yScale = 0; yScale < height_scale; yScale++) {
					for (int xScale = 0; xScale < width_scale; xScale++) {
						bufferTarget[xScale + yOffset] = GFX.ResolvePalette(*(spriteData));
					}
					yOffset += gfx_lcdWidth;
				}
			}

			bufferTarget += width_scale;
		}
		targetLine += pitch;
	}
}

gfx_sprite_t *gfx_FlipSpriteY(gfx_sprite_t *sprite_in,
	gfx_sprite_t *sprite_out) {

	uint8_t* destLine = &sprite_out->data[0];
	uint8_t* srcLine = &sprite_in->data[0];
	for (int y = 0; y < sprite_in->height; y++) {
		uint8_t* dest = destLine + sprite_in->width - 1;
		for (int x = 0; x < sprite_in->width; x++) {
			*dest-- = srcLine[x];
		}
		destLine += sprite_in->width;
		srcLine += sprite_in->width;
	}
	sprite_out->width = sprite_in->width;
	sprite_out->height = sprite_in->height;

	return sprite_out;
}

void gfx_RLETSprite(gfx_rletsprite_t *sprite,
	int x,
	int y) {

	CheckClip();

	RenderRLESprite<true>(sprite, x, y);
}

void gfx_RLETSprite_NoClip(gfx_rletsprite_t *sprite,
	uint24_t x,
	uint8_t y) {

	CheckClip();

	RenderRLESprite<false>(sprite, x, y);
}

void gfx_SetPalette(void *palette,
	uint24_t size,
	uint24_t offset) {

	// convert 1555 to 565 w/ separate alpha
	uint16_t* srcColor = (uint16_t*) palette;
	uint16_t* dstColor = &GFX.Palette[offset / 2];
	size /= 2;
	for (uint32 i = 0; i < size; i++, dstColor++, srcColor++) {
		dstColor[0] =
			((srcColor[0] & 0b0111110000000000) << 1) |
			((srcColor[0] & 0b0000001111100000) << 1) |
		     (srcColor[0] & 0b0000000000011111);
	}
}

uint8_t gfx_SetTransparentColor(uint8_t index) {
	uint8_t PrevIndex = GFX.TransparentIndex;
	GFX.TransparentIndex = index;
	return PrevIndex;
}

uint16_t* GetGFXPalette() {
	return GFX.Palette;
}

void gfx_SetClipRegion(int xmin, int ymin, int xmax, int ymax) {
	GFX.Clip_MinX = xmin;
	GFX.Clip_MinY = ymin;
	GFX.Clip_MaxX = xmax;
	GFX.Clip_MaxY = ymax;
}

uint8_t *gfx_TilePtr(gfx_tilemap_t *tilemap, unsigned x_offset, unsigned y_offset) {
	return &tilemap->map[(x_offset / tilemap->tile_width) + ((y_offset / tilemap->tile_height)*tilemap->width)];
}

uint8_t gfx_SetColor(uint8_t index) {
	uint8_t ret = GFX.LastColorIndex;
	GFX.CurColor = GFX.ResolvePalette(index);
	GFX.LastColorIndex = index;
	return ret;
}

void gfx_Begin() {
	// prepare for full color mode
	Bdisp_EnableColor(1);
	EnableStatusArea(3);
	Bdisp_Fill_VRAM(0, 3);
	DrawFrame(0);
	Bdisp_PutDisp_DD();
}

void gfx_End() {
#if TARGET_PRIZM
	DmaWaitNext();
	*((volatile unsigned*)MSTPCR0) &= ~(1 << 21);//Clear bit 21
#endif
}

void gfx_FillScreen(uint8_t index) {
	uint16_t Color = GFX.ResolvePalette(index);
	uint16_t* DestColor = BackBuffer();
	for (int y = 0; y < gfx_lcdHeight; y++) {
		for (int x = 0; x < gfx_lcdWidth; x++) {
			*(DestColor++) = Color;
		}
	}
}

void gfx_Rectangle(int x,
	int y,
	int width,
	int height) {

	CheckClip();

	GFX.ClipRect(x, y, width, height);

	gfx_Rectangle_NoClip(x, y, width, height);
}


void gfx_Rectangle_NoClip(uint24_t x,
	uint8_t y,
	uint24_t width,
	uint8_t height) {

	CheckClip();

	uint16_t* targetLine = GetTargetAddr(x, y);

	uint16_t* bufferLine = targetLine;
	for (uint32 x0 = 0; x0 < width; x0++) {
		*(bufferLine++) = GFX.CurColor;
	}
	targetLine += gfx_lcdWidth;

	for (uint32 y0 = 1; y0 + 1 < height; y0++) {
		targetLine[0] = GFX.CurColor;
		targetLine[width - 1] = GFX.CurColor;
		targetLine += gfx_lcdWidth;
	}

	bufferLine = targetLine;
	for (uint32 x0 = 0; x0 < width; x0++) {
		*(bufferLine++) = GFX.CurColor;
	}
}

void gfx_FillRectangle(int x,
	int y,
	int width,
	int height) {

	GFX.ClipRect(x, y, width, height);

	if (width > 0 && height > 0) {
		gfx_FillRectangle_NoClip(x, y, width, height);
	}
}


void gfx_FillRectangle_NoClip(uint24_t x,
	uint8_t y,
	uint24_t width,
	uint8_t height) {

	CheckClip();

	uint16_t* targetLine = GetTargetAddr(x, y);
	for (uint32 y0 = 0; y0 < height; y0++) {
		for (uint32 x0 = 0; x0 < width; x0++) {
			targetLine[x0] = GFX.CurColor;
		}
		targetLine += gfx_lcdWidth;
	}
}

void gfx_SetPixel(uint24_t x, uint8_t y) {
	CheckClip();

	uint16_t* targetLine = GetTargetAddr(x, y);
	targetLine[0] = GFX.CurColor;
}

static void BlitScreenSection(int y1, int h) {
	uint16_t* SourceAddr = BackBuffer() + (gfx_lcdWidth * y1);

#if TARGET_PRIZM
	const int ScreenOffset = (396 - gfx_lcdWidth) / 2;
	DmaWaitNext();

	Bdisp_WriteDDRegister3_bit7(1);
	Bdisp_DefineDMARange(ScreenOffset, ScreenOffset + gfx_lcdWidth - 1, y1, y1+h);
	Bdisp_DDRegisterSelect(LCD_GRAM);

	DmaDrawStrip(SourceAddr, h * gfx_lcdWidth * 2);
	DmaWaitNext();
#endif

#if TARGET_WINSIM
	const int ScreenOffset = (LCD_WIDTH_PX - gfx_lcdWidth) / 2;
	h = min(LCD_HEIGHT_PX, y1 + h) - y1;
	if (h > 0) {
		uint16_t* TargetAddr = ((uint16_t*)GetVRAMAddress()) + ScreenOffset + LCD_WIDTH_PX * y1;
		for (int32 i = 0; i < h; i++, TargetAddr += LCD_WIDTH_PX, SourceAddr += gfx_lcdWidth) {
			memcpy(TargetAddr, SourceAddr, gfx_lcdWidth * 2);
		}
		Bdisp_PutDisp_DD();
	}
#endif
}

void gfx_Blit(gfx_location_t src) {
	// only one way supported
	DebugAssert(src == gfx_buffer);

	if (src == gfx_buffer) {
		BlitScreenSection(0, 224);
	}
}

void gfx_SetDraw(uint8_t location) {
	DebugAssert(location == gfx_buffer);
}

void gfx_BlitLines(gfx_location_t src,
	uint8_t y_loc,
	uint8_t num_lines) {

	// only one way supported
	DebugAssert(src == gfx_buffer);

	if (src == gfx_buffer) {
		BlitScreenSection(y_loc, num_lines);
	}
}

void gfx_FillCircle(int x,
	int y,
	uint24_t radius) {

	CheckClip();

	int width = radius * 2;
	int height = radius * 2;
	int x1 = x - radius;
	int y1 = y - radius;
	GFX.ClipRect(x1, y1, width, height);

	int x2 = x1 + width;
	int y2 = y1 + height;

	int rSq = radius * radius;

	uint16_t* targetLine = GetTargetAddr(0, y1);

	for (int curY = y1; curY < y2; curY++) {
		int distSqBase = (y - curY) * (y - curY);
		for (int curX = x1; curX < x2; curX++) {
			int distSQ = (x - curX) * (x - curX) + distSqBase;
			if (distSQ <= rSq) {
				targetLine[curX] = GFX.CurColor;
			}
		}
		targetLine += gfx_lcdWidth;
	}
}

uint8_t gfx_SetTextFGColor(uint8_t color) {
	uint8_t ret = GFX.LastTextColorIndex;
	GFX.CurTextColor = GFX.ResolvePalette(color);
	GFX.LastTextColorIndex = color;
	return ret;
}

uint8_t gfx_SetTextBGColor(uint8_t color) {
	uint8_t ret = GFX.LastTextBGColorIndex;
	GFX.CurTextBGColor = GFX.ResolvePalette(color);
	GFX.LastTextBGColorIndex = color;
	return ret;
}

uint8_t gfx_SetTextTransparentColor(uint8_t color) {
	uint8_t ret = GFX.LastTextClearColorIndex;
	GFX.CurTextClearColor = GFX.ResolvePalette(color);
	GFX.LastTextClearColorIndex = color;
	return ret;
}

void gfx_SetMonospaceFont(uint8_t spacing) {
	// todo
}

unsigned int gfx_GetStringWidth(const char *string) {
	return CalcType_Width(&arial_small, string);
}

void gfx_SetTextXY(int x, int y) {
	GFX.TextX = x;
	GFX.TextY = y;
}

void gfx_PrintStringXY(const char *string, int x, int y) {
	if (y + arial_small.height >= gfx_lcdHeight)
		return;

	// render background behind text if BG color is not clear color
	int32 width = CalcType_Width(&arial_small, string);

	{
		CheckClip();

		if (GFX.CurTextBGColor != GFX.CurTextClearColor) {
			uint16_t oldColor = GFX.CurColor;
			GFX.CurColor = GFX.CurTextBGColor;
			gfx_FillRectangle_NoClip(x, y, width, arial_small.height - 1);
			GFX.CurColor = oldColor;
		}

		CalcType_Draw(&arial_small, string, x, y, GFX.CurTextColor, (uint8*)BackBuffer(), 320);
	}

	GFX.TextX = x + width;
	GFX.TextY = y;
}

void gfx_PrintUInt(unsigned int n, uint8_t length) {
	char buffer[32];
	sprintf(buffer, "%d", n);
	int curLength = strlen(buffer);

	char paddedBuffer[32] = { 0 };
	int i = 0;
	while (curLength < length) {
		paddedBuffer[i++] = '0';
		curLength++;
	}
	strcpy(&paddedBuffer[i], buffer);
	gfx_PrintStringXY(paddedBuffer, GFX.TextX, GFX.TextY);
}

void gfx_ShiftDown(uint8_t pixels) {
	CheckClip();

	int32 shiftAmt = pixels * gfx_lcdWidth;
	uint16_t* VRAM = BackBuffer();
	memmove(VRAM + shiftAmt, VRAM, (gfx_lcdWidth * gfx_lcdHeight * 2) - shiftAmt * 2);
}

void gfx_Tilemap(gfx_tilemap_t *tilemap,
	uint24_t x_offset,
	uint24_t y_offset) {

	int baseX = tilemap->x_loc;
	int baseY = tilemap->y_loc;

	int tileX = x_offset / tilemap->tile_width;
	int tileY = y_offset / tilemap->tile_height;
	int tileXMod = x_offset % tilemap->tile_width;
	int tileYMod = y_offset % tilemap->tile_height;

	const int baseTileX = tileX;

	unsigned int numCols = tilemap->draw_width;
	unsigned int numRows = tilemap->draw_height;
	if (tileXMod) {
		baseX -= tileXMod;
		numCols++;
	}
	if (tileYMod) {
		baseY -= tileYMod;
		numRows++;
	}

	// if we are rendering past the data, then render black
	if (numRows + tileY > tilemap->height) {
		unsigned int newRows = tilemap->height - tileY;
		unsigned int removedRows = numRows - newRows;
		numRows = newRows;
		uint16_t SwapColor = GFX.CurColor;
		GFX.CurColor = 0;
		gfx_FillRectangle(baseX, baseY + tilemap->tile_height * numRows, numCols * tilemap->tile_width, tilemap->tile_height * removedRows);
		GFX.CurColor = SwapColor;
	}

	int curY = baseY;
	for (uint32 dY = 0; dY < numRows; dY++, curY += tilemap->tile_height, tileY++) {
		int curX = baseX;
		tileX = baseTileX;
		for (uint32 dX = 0; dX < numCols; dX++, curX += tilemap->tile_width, tileX++) {
			gfx_sprite_t* sprite = tilemap->tiles[tilemap->map[tileX + tileY * tilemap->width]];
			gfx_Sprite(sprite, curX, curY);
		}
	}
}

static void ResolveBufferToVRAM() {
	const int ScreenOffset = (LCD_WIDTH_PX - gfx_lcdWidth) / 2;

	// copy screen
	for (int y = 215; y >= 0; y--) {
		uint16_t* SrcColor = &BackBuffer()[gfx_lcdWidth * y];
		uint16_t* DestColor = ((uint16_t*)GetVRAMAddress()) + LCD_WIDTH_PX * y + ScreenOffset;
		memcpy(DestColor, SrcColor, gfx_lcdWidth * 2);
	}

	// black out sides
	for (int y = 215; y >= 0; y--) {
		uint16_t* DestColor = ((uint16_t*)GetVRAMAddress()) + LCD_WIDTH_PX * y;
		memset(DestColor, 0, ScreenOffset * 2);
		DestColor += ScreenOffset + gfx_lcdWidth;
		memset(DestColor, 0, ScreenOffset * 2);
	}
}

void kb_Scan_with_GetKey() {
#ifdef TARGET_PRIZM
	// before calling getkey we need to resolve the VRAM back into the system pitch:
	ResolveBufferToVRAM();
#endif

	int key = 0;
	GetKey(&key);
	kb_Scan();

	// clear VRAM and draw frame cause we mind have went to menu, onus is on app to redraw screen
	Bdisp_Fill_VRAM(0, 3);
	DrawFrame(0);
	Bdisp_PutDisp_DD();
}
