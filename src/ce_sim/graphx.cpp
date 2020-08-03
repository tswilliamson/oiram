
#include "platform.h"
#include "debug.h"
#include "ce_sim.h"
#include "graphx.h"

#include "calctype/fonts/arial_small/arial_small.h"

#include "fxcg/display.h"

struct GraphX_Context {
	int Clip_MinX = 0;
	int Clip_MinY = 0;
	int Clip_MaxX = LCD_WIDTH_PX;
	int Clip_MaxY = LCD_HEIGHT_PX;

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

	uint16_t ResolvePalette(uint8_t PaletteIndex) {
		return Palette[PaletteIndex];
	}
};

struct ClipChecker {
	uint16_t hashTop;
	uint16_t hashBottom;

	uint16_t Hash(uint16_t* row) {
		uint16_t val = 0x1E37;
		for (int x = 0; x < LCD_WIDTH_PX; x++) {
			val = (val << 1) ^ (row[x]) ^ (val >> 15);
		}
		return val;
	}

	uint16_t HashTop() {
		return Hash(((uint16_t*)GetVRAMAddress()) - LCD_WIDTH_PX);
	}

	uint16_t HashBottom() {
		return Hash(((uint16_t*)GetVRAMAddress()) + LCD_WIDTH_PX * LCD_HEIGHT_PX);
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

static GraphX_Context GFX;
const int ScreenOffset = (LCD_WIDTH_PX - gfx_lcdWidth) / 2;

uint16_t* GetTargetAddr(int x, int y) {
	return (uint16_t*)GetVRAMAddress() + LCD_WIDTH_PX * y + x + ScreenOffset;
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
		targetLine += y0 * LCD_WIDTH_PX;
	}

	for (; y0 < h; y0++) {
		for (int xPix = x0; xPix < w; xPix++) {
			if (!bTransparent || spriteData[xPix] != GFX.TransparentIndex) {
				targetLine[xPix] = GFX.ResolvePalette(spriteData[xPix]);
			}
		}
		spriteData += sprite->width;
		targetLine += LCD_WIDTH_PX;
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
	const unsigned int pitch = LCD_WIDTH_PX * height_scale;

	for (int y0 = 0; y0 < sprite->height; y0++) {
		uint16_t* bufferTarget = targetLine;
		for (int x0 = 0; x0 < sprite->width; x0++, spriteData++) {
			if (*spriteData != GFX.TransparentIndex) {
				int yOffset = 0;
				for (int yScale = 0; yScale < height_scale; yScale++) {
					for (int xScale = 0; xScale < width_scale; xScale++) {
						bufferTarget[xScale + yOffset] = GFX.ResolvePalette(*(spriteData));
					}
					yOffset += LCD_WIDTH_PX;
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

	// todo
}

void gfx_SetPalette(void *palette,
	uint24_t size,
	uint8_t offset) {

	// convert 1555 to 565 w/ separate alpha
	uint16_t* srcColor = (uint16_t*) palette;
	uint16_t* dstColor = &GFX.Palette[offset / 2];
	size /= 2;
	for (int32 i = 0; i < size; i++, dstColor++, srcColor++) {
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
}

void gfx_End() {
}

void gfx_FillScreen(uint8_t index) {
	uint16_t Color = GFX.ResolvePalette(index);
	Bdisp_Fill_VRAM(Color, 3);
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
	for (int x0 = 0; x0 < width; x0++) {
		*(bufferLine++) = GFX.CurColor;
	}
	targetLine += LCD_WIDTH_PX;

	for (int y0 = 1; y0 + 1 < height; y0++) {
		targetLine[0] = GFX.CurColor;
		targetLine[width - 1] = GFX.CurColor;
		targetLine += LCD_WIDTH_PX;
	}

	bufferLine = targetLine;
	for (int x0 = 0; x0 < width; x0++) {
		*(bufferLine++) = GFX.CurColor;
	}
}

void gfx_FillRectangle(int x,
	int y,
	int width,
	int height) {

	GFX.ClipRect(x, y, width, height);

	gfx_FillRectangle_NoClip(x, y, width, height);
}


void gfx_FillRectangle_NoClip(uint24_t x,
	uint8_t y,
	uint24_t width,
	uint8_t height) {

	CheckClip();

	uint16_t* targetLine = GetTargetAddr(x, y);
	for (int y0 = 0; y0 < height; y0++) {
		for (int x0 = 0; x0 < width; x0++) {
			targetLine[x0] = GFX.CurColor;
		}
		targetLine += LCD_WIDTH_PX;
	}
}

void gfx_SetPixel(uint24_t x, uint8_t y) {
	CheckClip();

	uint16_t* targetLine = GetTargetAddr(x, y);
	targetLine[0] = GFX.CurColor;
}

void gfx_Blit(gfx_location_t src) {
	// only one way supported
	DebugAssert(src == gfx_buffer);

	if (src == gfx_buffer) {
		Bdisp_PutDisp_DD();
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
		Bdisp_PutDisp_DD_stripe(y_loc, num_lines);
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
		targetLine += LCD_WIDTH_PX;
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
	CheckClip();

	if (y + arial_small.height >= LCD_HEIGHT_PX)
		return;

	CalcType_Draw(&arial_small, string, x + ScreenOffset, y, GFX.CurTextColor, 0, 0);
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
	int32 shiftAmt = pixels * LCD_WIDTH_PX;
	uint16_t* VRAM = (uint16_t*)GetVRAMAddress();
	memmove(VRAM + shiftAmt, VRAM, (LCD_WIDTH_PX * LCD_HEIGHT_PX * 2) - shiftAmt * 2);
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

	int curY = baseY;
	for (int dY = 0; dY < numRows; dY++, curY += tilemap->tile_height, tileY++) {
		int curX = baseX;
		tileX = baseTileX;
		for (int dX = 0; dX < numCols; dX++, curX += tilemap->tile_width, tileX++) {
			gfx_sprite_t* sprite = tilemap->tiles[tilemap->map[tileX + tileY * tilemap->width]];
			gfx_Sprite(sprite, curX, curY);
		}
	}
}