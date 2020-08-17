#include "platform.h"
#include "debug.h"

#include "loadscreen.h"
#include "tile_handlers.h"
#include "defines.h"
#include "powerups.h"
#include "enemies.h"
#include "events.h"
#include "images.h"
#include "oiram.h"
#include "lower.h"
#include "simple_mover.h"

#include <string.h>
#include <stdbool.h>

#if !TARGET_PRIZM
#include <stdint.h>
#endif

#include "tice.h"
#include "graphx.h"
#include "keypadc.h"
#include "fileioc.h"

uint8_t *warp_info;
unsigned int warp_num;

pack_info_t pack_info[256];

static char *var_name;
static const char search_string0[] = { 0xAB, 0xCD, 0x00 };
static const char search_string1[] = { 0xAB, 0xCE, 0x00 };
static const char *save_name = "OiramSV";
uint8_t num_packs = 0;
char *pack_author;

uint8_t *get_pack_pointer(ti_var_t slot) {
    uint8_t *ptr = (uint8_t*)ti_GetDataPtr(slot) + 1;

    if (*ptr == 0xCE) {
        ptr += 5;
    } else {
        ptr += 1;
    }

    return ptr;
}

// this routine should only be used right before an exit
void save_progress(void) {

    ti_var_t variable;
    ti_CloseAll();
	int size = 8 + sizeof(pack_info_t) * num_packs;
    if ((variable = ti_Open(save_name, "w", size))) {
        ti_PutC((char)num_packs, variable);
        ti_Write(&pack_info, sizeof(pack_info_t), num_packs, variable);
		ti_PutC((char)game.jumpKey, variable);
		ti_PutC((char)game.runKey, variable);
		ti_PutC((char)game.attackKey, variable);
		ti_PutC((char)game.duckKey, variable);
		ti_PutC((char)game.leftKey, variable);
		ti_PutC((char)game.rightKey, variable);
		ti_PutC((char)game.pauseKey, variable);
        ti_SetArchiveStatus(true, variable);
    }
    ti_CloseAll();

    gfx_End();
}

void load_progress(void) {
    ti_var_t variable;
    pack_info_t **pack_info_in_var = NULL;
    uint8_t num_packs_in_var = 0;
    unsigned int j;
    void *search_pos;
    const char *search_str;
    uint8_t search_current = 1;

    ti_CloseAll();
    if ((variable = ti_Open(save_name, "r", -1))) {
        num_packs_in_var = (uint8_t)ti_GetC(variable);
        pack_info_in_var = ti_GetDataPtr(variable);
        ti_Seek(-7, SEEK_END, variable);
		game.jumpKey = ti_GetC(variable);
		game.runKey = ti_GetC(variable);
		game.attackKey = ti_GetC(variable);
		game.duckKey = ti_GetC(variable);
		game.leftKey = ti_GetC(variable);
		game.rightKey = ti_GetC(variable);
		game.pauseKey = ti_GetC(variable);
        ti_Rewind(variable);
    } else {
		// defaults: Jump = Shift, Run = Optn, Attack = Optn, Duck = Down
		game.jumpKey = 78;
		game.runKey = 68;
		game.attackKey = 68;
		game.duckKey = 37;
		game.leftKey = 38;
		game.rightKey = 27;
		game.pauseKey = 39;
    }

    memset(pack_info, 0, sizeof pack_info);
    for(j=0; j<256; j++) {
        pack_info[j].lives = 15;
    }

    num_packs = 0;
    search_str = search_string0;
    do {
        search_pos = NULL;
        while ((var_name = ti_Detect(&search_pos, search_str))) {
            pack_info_t *pack = &pack_info[num_packs];
            for (j = 0; j < num_packs_in_var; j++) {
                pack_info_t *pack_in_var = (pack_info_t*)((uint8_t*)((uint8_t*)(pack_info_in_var) + (j*sizeof(pack_info_t))));
                if (!strcmp(pack_in_var->name, var_name)) {
                    memcpy(pack, pack_in_var, sizeof(pack_info_t));
                    break;
                }
            }

            // if we didn't break from the loop
            if (j == num_packs_in_var) {
                memcpy(pack->name, var_name, 8);
            }
            num_packs++;
        }
        search_str = search_string1;
    } while (search_current--);

    ti_CloseAll();
}

static void init_level(uint8_t width, uint8_t height, uint8_t scroll) {
    level_map.scroll = scroll;
    level_map.max_y = height * TILE_HEIGHT;
    level_map.max_x = width * TILE_WIDTH;
    level_map.max_x_scroll = level_map.max_x - ((TILEMAP_DRAW_WIDTH-1) * TILE_WIDTH);
    level_map.max_y_scroll = level_map.max_y - ((TILEMAP_DRAW_HEIGHT-1) * TILE_HEIGHT);
    tilemap.tiles = tileset_tiles;
    tilemap.type_width = gfx_tile_16_pixel;
    tilemap.type_height = gfx_tile_16_pixel;
    tilemap.tile_height = TILE_HEIGHT;
    tilemap.tile_width = TILE_WIDTH;
    tilemap.draw_height = TILEMAP_DRAW_HEIGHT;
    tilemap.draw_width = TILEMAP_DRAW_WIDTH;
    tilemap.width = width;
    tilemap.height = height;
    tilemap.y_loc = 0;
    tilemap.x_loc = 0;
}

static void decode(uint8_t *in, uint8_t *out) {
    for(;;) {
        uint8_t c, i, cnt;
        c = *in;
        in++;
        if (c > 128) {
            if (c == 255) {
                break;
            }
            cnt = c - 128;
            for (i = 0; i < cnt; i++) {
                *out = *in;
                out++; in++;
            }
        } else {
            cnt = c;
            c = *in;
            in++;
            for (i = 0; i < cnt; i++) {
                *out = c;
                out++;
            }
        }
    }
}

void set_level(char *name, uint8_t level) {
    uint8_t level_width = 0;
    uint8_t level_height = 0;
    uint16_t color = 0;
    uint8_t scroll = SCROLL_NONE;
    ti_var_t slot;

    game.num_levels = 0;
    ti_CloseAll();

    if ((slot = ti_Open(name, "r", -1))) {
        uint8_t *pack_data;
        uint8_t num_pipes;

        // get actual pack pointer
        pack_data = get_pack_pointer(slot);

        // find start of actual data
        pack_data += strlen((char*)pack_data) + 1;

        pack_author = (char*)pack_data;
        pack_data += strlen(pack_author) + 1;

        // get the number of levels in the pack
        game.num_levels = *pack_data;
        pack_data++;
        if (level) {
			uint8_t* level_data = pack_data + (level - 1) * 2;
            pack_data += (level_data[0] | (level_data[1] << 8));
        }
        pack_data += (game.num_levels-1)*2;

        // extract color channel
		color = pack_data[0] | (pack_data[1] << 8);
        pack_data += 2;

        // extract scroll behavior if available
        if (*pack_data == 255) {
            pack_data += 1;
            scroll = *pack_data;
            pack_data += 1;
        }

        // get the number of pipes
        num_pipes = *pack_data;
        pack_data++;
        warp_info = pack_data;
        warp_num = num_pipes * 2;

        pack_data += num_pipes * 6;

        // get level width and height
        level_width = *pack_data++;
        level_height = *pack_data++;

        // allocate and decompress the level
        tilemap.map = malloc(level_width * level_height);
        decode(pack_data, tilemap.map);
    }

    if (!level_width || !level_height) {
        save_progress();
        exit(0);
    }

    // init the tilemap structure
    init_level(level_width, level_height, scroll);
    gfx_palette[BACKGROUND_COLOR_INDEX] = color;
}

bool GetMapKey(uint8_t* keyCode, const char* button) {
	gfx_SetColor(0);
	gfx_Rectangle(80, 80, 160, 60);
	gfx_SetColor(8);
	gfx_FillRectangle(81, 81, 158, 58);

	gfx_SetTextFGColor(0);
	const char* str1 = "PRESS KEY FOR";
	const char* str2 = "[MENU] TO CANCEL";
	int width = gfx_GetStringWidth(str1);
	gfx_PrintStringXY(str1, 160 - width / 2, 90);
	width = gfx_GetStringWidth(button);
	gfx_PrintStringXY(button, 160 - width / 2, 105);
	width = gfx_GetStringWidth(str2);
	gfx_PrintStringXY(str2, 160 - width / 2, 120);

	gfx_BlitBuffer();

	uint8_t newKey;
	do {
		newKey = os_GetCSC();
	} while (newKey == 0);
	
	while (os_GetCSC()) {}

	if (newKey == 48)
		return false;

	*keyCode = newKey;
	return true;
}

#define MAX_SHOW 7

void set_load_screen(void) {
	// on prizm, save every time we enter this screen (except the first time)
	static bool saveNow = false;
	if (saveNow) {
		save_progress();
	}
	saveNow = true;

    ti_var_t slot;
    int y;

    uint8_t search_current;
    void *search_pos;
    const char *search_str;

    gfx_sprite_t *tile_208 = tileset_tiles[208];
    gfx_sprite_t *tile_145 = tileset_tiles[145];
    gfx_sprite_t *tile_194 = tileset_tiles[194];
    gfx_sprite_t *tile_195 = tileset_tiles[195];

    uint8_t *pack_data;
    static uint8_t selected_pack = 0;
    uint8_t selected_level = 0;
    static uint8_t scroll_amt = 0;
    uint8_t num_levels;
    uint8_t max_select_level[MAX_SHOW];
    uint8_t tmp;

    gfx_SetClipRegion(0, 0, LCD_WIDTH, LCD_HEIGHT);

    redraw_screen:

    gfx_SetTextBGColor(DARK_BLUE_INDEX);
    gfx_SetTextTransparentColor(DARK_BLUE_INDEX);

    y = 103;
    slot = 0;

    gfx_palette[BACKGROUND_COLOR_INDEX] = 0xD77E;
    gfx_FillScreen(BLACK_INDEX);
    gfx_ScaledTransparentSprite_NoClip(oiram_logo, 150, 32, 2, 2);

    gfx_TransparentSprite(tile_194, 24, 52);
    gfx_TransparentSprite(tile_195, 40, 52);

    gfx_TransparentSprite(tile_145, 40, 68);

    gfx_TransparentSprite(tile_194, 56, 20);
    gfx_TransparentSprite(tile_195, 72, 20);
    gfx_TransparentSprite(tile_145, 72, 36);
    gfx_TransparentSprite(tile_145, 72, 52);
    gfx_TransparentSprite(tile_145, 72, 68);
    gfx_TransparentSprite(tile_145, 72, 84);
    gfx_TransparentSprite(tile_208, 56, 36);
    gfx_TransparentSprite(tile_208, 56, 52);
    gfx_TransparentSprite(tile_208, 56, 68);

    gfx_TransparentSprite(tile_208, 24, 68);
    gfx_TransparentSprite(tile_208, 24, 84);

    gfx_TransparentSprite(tileset_tiles[222], 56, 84);
    gfx_TransparentSprite(tileset_tiles[223], 40, 84);

	gfx_SetTextFGColor(12);
	gfx_PrintStringXY("Prizm port by TSW", 150, 70);

    gfx_SetTextFGColor(WHITE_INDEX);
    gfx_PrintStringXY("By Mateo", 150, 58);
    gfx_SetColor(DARK_BLUE_INDEX);
    gfx_FillRectangle(2, 100, 316, 80);
    gfx_SetColor(WHITE_INDEX);
    gfx_Rectangle(2, 100, 316, ((MAX_SHOW+1) * 10) + 1);

    gfx_TransparentSprite(mushroom, 5, selected_pack*10 + 103);
	
    gfx_PrintStringXY("[EXE] Start Game", 9, 184);
    gfx_PrintStringXY("[OPTN] Change Keys", 9, 196);
    gfx_PrintStringXY("[MENU] Quit", 9, 208);

    gfx_PrintStringXY("Press <> to select level", 150, 184);

    tmp = 0;
    num_packs = 0;
    search_str = search_string0;
    search_current = 1;
    do {
        ti_CloseAll();
        search_pos = NULL;
        while((var_name = ti_Detect(&search_pos, search_str))) {
            if (scroll_amt <= num_packs && y < (103 + 10*MAX_SHOW)) {
                uint8_t max_select;
                uint8_t progress;

                slot = ti_Open((char*)var_name, "r", 128);
                pack_data = get_pack_pointer(slot);

                gfx_PrintStringXY((char*)pack_data, 23, y + 4);

                pack_data += strlen((char*)pack_data) + 1;
                pack_data += strlen((char*)pack_data) + 1;
                num_levels = *pack_data;
                max_select = progress = pack_info[num_packs].progress;

                if (progress != num_levels) {
                    max_select++;
                }

                gfx_SetTextXY(320 - 8*3 - 3, y + 4);
                gfx_SetMonospaceFont(8);
                gfx_PrintUInt(max_select, 3);
                gfx_SetMonospaceFont(0);

                max_select_level[num_packs - scroll_amt] = max_select - 1;

                if (selected_pack == tmp) {
                    selected_level = max_select - 1;
                }
                y += 10;
                tmp++;

                ti_CloseAll();
            }

            num_packs++;
        }
        search_str = search_string1;
    } while (search_current--);

    if (selected_pack > num_packs) {
        selected_pack = 0;
        scroll_amt = 0;
    }

    gfx_BlitBuffer();

    // debounce
	while (os_GetCSC()) {}

    for (;;) {
        kb_key_t grp7;
        kb_key_t _grp1;
        kb_key_t grp6;

        // scan the keypad
		kb_Scan();

		grp7 = kb_Data[7];
		grp6 = kb_Data[6];
		_grp1 = kb_Data[1];

		// don't debounce exit
        if (kb_Data[EXIT_KEY_GROUP] == EXIT_KEY) {
#if TARGET_WINSIM
			exit(0);
#endif
			kb_Scan_with_GetKey();
			goto redraw_screen;
        }

        // debounce
		if (os_GetCSC() && (_grp1 || grp6 || grp7)) {
			while (os_GetCSC()) {}
		}

        if (grp6 == kb_Enter || _grp1 == kb_2nd) {
            break;
        }
        if (_grp1 == kb_Mode) {
			uint8_t newRun, newJump, newAttack, newDuck, newLeft, newRight, newPause;
			if (!GetMapKey(&newJump, "JUMP")) goto redraw_screen;
			if (!GetMapKey(&newRun, "RUN")) goto redraw_screen;
			if (!GetMapKey(&newAttack, "ATTACK")) goto redraw_screen;
			if (!GetMapKey(&newDuck, "DUCK/PIPE")) goto redraw_screen;
			if (!GetMapKey(&newLeft, "LEFT")) goto redraw_screen;
			if (!GetMapKey(&newRight, "RIGHT")) goto redraw_screen;
			if (!GetMapKey(&newPause, "PAUSE")) goto redraw_screen;
			game.runKey = newRun;
			game.jumpKey = newJump;
			game.attackKey = newAttack;
			game.duckKey = newDuck;
			game.leftKey = newLeft;
			game.rightKey = newRight;
			game.pauseKey = newPause;
            goto redraw_screen;
        }
        if (grp7 == kb_Down || grp7 == kb_Up) {
            if (grp7 == kb_Down && ((selected_pack + scroll_amt + 1) < num_packs)) {
                if (selected_pack == MAX_SHOW-1) {
                    scroll_amt++;
                } else {
                    selected_pack++;
                }
            }
            if (grp7 == kb_Up && ((selected_pack + scroll_amt) != 0)) {
                if (selected_pack == 0) {
                    scroll_amt--;
                } else {
                    selected_pack--;
                }
            }
            goto redraw_screen;
        }
        if (grp7 == kb_Left || grp7 == kb_Right) {
            if (grp7 == kb_Left) {
                if (selected_level) {
                    selected_level--;
                }
            } else {
                if (selected_level != max_select_level[selected_pack]) {
                    selected_level++;
                }
            }
            gfx_SetTextBGColor(DARK_BLUE_INDEX);
            gfx_SetTextTransparentColor(BLACK_INDEX);
            gfx_SetTextXY(320 - 8*3 - 3, selected_pack*10 + 103 + 4);
            gfx_SetMonospaceFont(8);
            gfx_PrintUInt(selected_level + 1, 3);
            gfx_SetMonospaceFont(0);
            gfx_BlitBuffer();
        }
    }

    game.pack = selected_pack + scroll_amt;
    game.level = selected_level;

    gfx_SetClipRegion(0, 0, X_PXL_MAX, Y_PXL_MAX);
}
