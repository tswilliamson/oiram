#include "platform.h"
#include "debug.h"

#include <stdbool.h>
#include <stdint.h>
#include <tice.h>

#include <graphx.h>
#include <keypadc.h>
#include <fileioc.h>

#include "tile_handlers.h"
#include "powerups.h"
#include "defines.h"
#include "oiram.h"
#include "images.h"
#include "simple_mover.h"

void add_mushroom_1up(uint8_t *spawing_tile) {
    add_mushroom(spawing_tile);
    simple_mover[num_simple_movers-1]->type = MUSHROOM_1UP_TYPE;
}

void add_mushroom(uint8_t *spawing_tile) {
    simple_move_t *shroom = add_simple_mover(spawing_tile);

    shroom->hitbox.width = 15;
    shroom->hitbox.height = 15;
    shroom->y -= TILE_HEIGHT;
    if (oiram.x < shroom->x) {
        shroom->vx = 2;
    } else {
        shroom->vx = -2;
    }
    shroom->type = MUSHROOM_TYPE;
}

void add_star(uint8_t *spawing_tile) {
    simple_move_t *star = add_simple_mover(spawing_tile);

    star->hitbox.width = 15;
    star->hitbox.height = 15;
    star->y -= TILE_HEIGHT;
    if (oiram.x < star->x) {
        star->vx = 2;
    } else {
        star->vx = -2;
    }
    star->type = STAR_TYPE;
    star->is_bouncer = true;
}

void add_fire_flower(uint8_t *spawing_tile) {
    simple_move_t *flower = add_simple_mover(spawing_tile);

    flower->hitbox.width = 15;
    flower->hitbox.height = 15;
    flower->y -= TILE_HEIGHT;
    flower->type = FIRE_FLOWER_TYPE;
}

void set_left_oiram_sprites(void) {
    gfx_FlipSpriteY(oiram_0_buffer_right, oiram_0_buffer_left);
    gfx_FlipSpriteY(oiram_1_buffer_right, oiram_1_buffer_left);
}

void eat_leaf(void) {
    if (!(oiram.flags & FLAG_OIRAM_RACOON)) {
        if (!oiram.crouched) {
            memcpy(oiram_0_buffer_right, oiram_0_racoon, OIRAM_BIG_SPRITE_SIZE);
            memcpy(oiram_1_buffer_right, oiram_1_racoon, OIRAM_BIG_SPRITE_SIZE);
        } else {
            memcpy(oiram_0_buffer_right, oiram_crouch_racoon, OIRAM_BIG_SPRITE_SIZE);
            memcpy(oiram_1_buffer_right, oiram_crouch_racoon, OIRAM_BIG_SPRITE_SIZE);
        }
        set_left_oiram_sprites();
        if (!(oiram.flags & FLAG_OIRAM_BIG) && !oiram.crouched) {
            oiram.y -= 12;
        }
        oiram.hitbox.height = 26;
        oiram.hitbox_height_half = 13;
        oiram.flags |= FLAG_OIRAM_BIG | FLAG_OIRAM_RACOON;
        oiram.flags &= ~FLAG_OIRAM_FIRE;
    }
}

void eat_mushroom(void) {
    if (!(oiram.flags & FLAG_OIRAM_BIG)) {
        oiram.y -= 12;
        oiram.flags |= FLAG_OIRAM_BIG;
        set_normal_oiram_sprites();
    }
}

void eat_fire_flower(void) {
    if (!(oiram.flags & FLAG_OIRAM_FIRE)) {
        if (!(oiram.flags & FLAG_OIRAM_BIG)) {
            oiram.y -= 12;
        }
        oiram.flags |= FLAG_OIRAM_BIG | FLAG_OIRAM_FIRE;
        oiram.flags &= ~FLAG_OIRAM_RACOON;
        set_normal_oiram_sprites();
    }
}

void eat_star(void) {
    oiram.invincible = 250;
    oiram.flags |= FLAG_OIRAM_INVINCIBLE;
}

bool shrink_oiram(void) {
    if (oiram.flags & (FLAG_OIRAM_INVINCIBLE | FLAG_OIRAM_SLIDE)) {
        return false;
    }

    if (!warp.style) {
        if (!oiram.shrink_count) {
            oiram.shrink_count = 40;
            if (oiram.has_shell) { oiram.has_shell = false; }
            if (oiram.flags & FLAG_OIRAM_RACOON) {
                oiram.flags &= ~FLAG_OIRAM_RACOON;
                goto shrink_to_big;
            } else if (oiram.flags & FLAG_OIRAM_FIRE) {
                oiram.flags &= ~FLAG_OIRAM_FIRE;
    shrink_to_big:
                if (oiram.crouched) {
                    memcpy(oiram_0_buffer_right, oiram_crouch_big, OIRAM_BIG_SPRITE_SIZE);
                    memcpy(oiram_1_buffer_right, oiram_crouch_big, OIRAM_BIG_SPRITE_SIZE);
                } else {
                    memcpy(oiram_0_buffer_right, oiram_0_big, OIRAM_BIG_SPRITE_SIZE);
                    memcpy(oiram_1_buffer_right, oiram_1_big, OIRAM_BIG_SPRITE_SIZE);
                }
                set_left_oiram_sprites();
            } else if (oiram.flags & FLAG_OIRAM_BIG) {
                memcpy(oiram_0_buffer_right, oiram_0_small, OIRAM_BIG_SPRITE_SIZE);
                memcpy(oiram_1_buffer_right, oiram_1_small, OIRAM_BIG_SPRITE_SIZE);
                set_left_oiram_sprites();

                if (!oiram.crouched) { oiram.y += 11; }

                oiram.hitbox.height = 15;
                oiram.flags &= ~FLAG_OIRAM_BIG;
            } else {
                oiram.failed = true;
            }
        }
        oiram.hitbox_height_half = oiram.hitbox.height/2;
    }

    return true;
}

void show_blue_items(bool state) {
    unsigned int j, loop = tilemap.width * tilemap.height;

    for(j = 0; j < loop; j++) {
        uint8_t *this = tilemap.map + j;
        uint8_t tile = *this;
        if (state) {
            if (tile == TILE_BLUE_BRICK_X) {
                *this = TILE_BLUE_BRICK;
            } else
            if (tile == TILE_BLUE_COIN_X) {
                *this = TILE_BLUE_COIN;
            }
        } else {
            if (tile == TILE_BLUE_BRICK) {
                *this = TILE_BLUE_BRICK_X;
            } else
            if (tile == TILE_BLUE_COIN) {
                *this = TILE_BLUE_COIN_X;
            }
        }
    }
}
