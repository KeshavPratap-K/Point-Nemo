#pragma once
#include <pebble.h>

#define DESIGN_W 200
#define DESIGN_H 228
extern int SCREEN_W;
extern int SCREEN_H;
#define SX(v) ((v) * SCREEN_W / DESIGN_W)
#define SY(v) ((v) * SCREEN_H / DESIGN_H)

#define FPS 30
#define NUM_WAVE_POINTS 11
#define MAX_PARTICLES 40
#define PERSIST_KEY_HIGH_SCORE 1001
#define PERSIST_KEY_BACKLIGHT 1002
#define PERSIST_KEY_LAST_SCORE 1003

typedef enum { GAME_STATE_MENU, GAME_STATE_MODE_SELECT, GAME_STATE_COUNTDOWN, GAME_STATE_PLAYING, GAME_STATE_SINKING, GAME_STATE_GAME_OVER } GameState;
typedef enum { CONTROL_TILT, CONTROL_BUTTONS } ControlMode;
typedef enum { PARTICLE_SPLASH, PARTICLE_SMOKE } ParticleType;

typedef struct {
    ParticleType type;
    int x, y, vx, vy, life, max_life;
} Particle;

extern GameState s_state;
extern ControlMode s_control_mode;
extern int s_countdown_timer;
extern uint32_t s_tick;
extern int s_water_angle, s_water_base_y, s_wave_phase, s_water_distance;
extern int s_boat_x, s_boat_y;
extern int s_obstacle_x, s_obstacle_gap_y, s_obstacle_speed;
extern bool s_powerup_active;
extern int s_powerup_x, s_powerup_y;
extern bool s_shield_active;
extern int s_shield_timer, s_sun_cool_timer;
extern int s_island_x, s_lighthouse_island_x, s_cloud1_x, s_cloud2_x;
extern int s_score, s_high_score;
extern bool s_up_pressed, s_down_pressed;
extern int s_button_angle;
extern Particle s_particles[MAX_PARTICLES];
extern bool s_backlight_always_on;

extern int s_sink_timer, s_sink_angle, s_sink_y_offset;
extern int s_shake_offset_x, s_shake_offset_y;
extern int s_death_text_idx;
extern int s_last_death_score;
extern int s_ghost_buoy_x;
extern bool s_ghost_buoy_active;

extern GBitmap *s_bmp_boat, *s_bmp_anchor, *s_bmp_mine, *s_bmp_sun_normal, *s_bmp_sun_cool, *s_bmp_sun_surprised;
extern GBitmap *s_bmp_cloud_single, *s_bmp_cloud_double, *s_bmp_island_coconut, *s_bmp_island_lighthouse, *s_bmp_buoy;

extern AppTimer *s_timer;
extern Layer *s_canvas_layer;
