#pragma once
#include <pebble.h>
void reset_game(void);
void game_loop(void *data);
void click_config_provider(void *context);
void canvas_update_proc(Layer *layer, GContext *ctx);
void load_high_score(void);
