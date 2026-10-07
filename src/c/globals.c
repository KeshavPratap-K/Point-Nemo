#include "globals.h"

int SCREEN_W = DESIGN_W;
int SCREEN_H = DESIGN_H;

GameState s_state = GAME_STATE_MENU;
ControlMode s_control_mode = CONTROL_TILT;
int s_countdown_timer = 90;
uint32_t s_tick = 0;
int s_water_angle = 0, s_water_base_y = 0, s_wave_phase = 0, s_water_distance = 0;
int s_boat_x = 0, s_boat_y = 0;
int s_obstacle_x = 0, s_obstacle_gap_y = 0, s_obstacle_speed = 4;
bool s_powerup_active = false;
int s_powerup_x = -50, s_powerup_y = 0;
bool s_shield_active = false;
int s_shield_timer = 0, s_sun_cool_timer = 0;
int s_island_x = 0, s_lighthouse_island_x = 0, s_cloud1_x = 0, s_cloud2_x = 0;
int s_score = 0, s_high_score = 0;
bool s_up_pressed = false, s_down_pressed = false;
int s_button_angle = 0;
Particle s_particles[MAX_PARTICLES];
bool s_backlight_always_on = false;

int s_sink_timer = 0, s_sink_angle = 0, s_sink_y_offset = 0;
int s_shake_offset_x = 0, s_shake_offset_y = 0;
int s_death_text_idx = 0;
int s_last_death_score = -1;
int s_ghost_buoy_x = -100;
bool s_ghost_buoy_active = false;

GBitmap *s_bmp_boat, *s_bmp_anchor, *s_bmp_mine, *s_bmp_sun_normal, *s_bmp_sun_cool, *s_bmp_sun_surprised;
GBitmap *s_bmp_cloud_single, *s_bmp_cloud_double, *s_bmp_island_coconut, *s_bmp_island_lighthouse, *s_bmp_buoy;

AppTimer *s_timer;
Layer *s_canvas_layer;
