#include <pebble.h>

#define SCREEN_W 200
#define SCREEN_H 228
#define FPS 30
#define GRAVITY_MODIFIER 15
#define FRICTION 9
#define NUM_WAVE_POINTS 11
#define MAX_PARTICLES 40

typedef enum {
    GAME_STATE_COUNTDOWN,
    GAME_STATE_PLAYING,
    GAME_STATE_GAME_OVER
} GameState;

typedef enum {
    PARTICLE_SPLASH,
    PARTICLE_SMOKE
} ParticleType;

// Dynamic Particle Structure
typedef struct {
    ParticleType type;
    int x, y;
    int vx, vy;
    int life;
    int max_life;
} Particle;

static Window *s_main_window;
static Layer *s_canvas_layer;
static AppTimer *s_timer;

// Bitmap Sprites
static GBitmap *s_boat_bitmap;
static GBitmap *s_anchor_bitmap;
static GBitmap *s_mine_bitmap;
static GBitmap *s_sun_bitmap;
static GBitmap *s_cloud_bitmap;
static GBitmap *s_island_bitmap;

static Particle s_particles[MAX_PARTICLES];

// Game Logic & State
static GameState s_state = GAME_STATE_COUNTDOWN;
static int s_countdown_timer = 90; // 90 frames = 3 seconds at 30 FPS
static uint32_t s_tick = 0;        // Global animation tick

static int s_water_angle = 0; 
static int s_wave_phase = 0;
static int s_boat_x = SCREEN_W / 2;
static int s_boat_y = SCREEN_H / 2;
static int s_boat_velocity = 0;

static int s_obstacle_x = SCREEN_W + 50;
static int s_obstacle_gap_y = SCREEN_H / 2;

static int s_island_x = SCREEN_W + 40;
static int s_score = 0;

static void game_loop(void *data);

static void reset_game(void) {
    s_state = GAME_STATE_COUNTDOWN;
    s_countdown_timer = 90;
    s_score = 0;
    s_boat_x = SCREEN_W / 2;
    s_boat_velocity = 0;
    s_obstacle_x = SCREEN_W + 50;
    s_island_x = SCREEN_W + 40;
    
    for (int i = 0; i < MAX_PARTICLES; i++) {
        s_particles[i].life = 0;
    }
}

static void accel_data_handler(AccelData *data, uint32_t num_samples) {
    s_water_angle = data[0].x / 15; 
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
    if (s_state == GAME_STATE_GAME_OVER) {
        reset_game();
        s_timer = app_timer_register(1000 / FPS, game_loop, NULL);
    }
}

// Universal Particle Spawner
static void spawn_particle(ParticleType type, int x, int y, int vx, int vy, int life) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (s_particles[i].life <= 0) {
            s_particles[i].type = type;
            s_particles[i].x = x;
            s_particles[i].y = y;
            s_particles[i].vx = vx;
            s_particles[i].vy = vy;
            s_particles[i].life = life;
            s_particles[i].max_life = life;
            break;
        }
    }
}

static void game_loop(void *data) {
    s_tick++; // Advance global animation tick

    if (s_state == GAME_STATE_COUNTDOWN) {
        s_countdown_timer--;
        s_wave_phase = (s_wave_phase + 1000) % TRIG_MAX_ANGLE;
        
        if (s_countdown_timer <= 0) {
            s_state = GAME_STATE_PLAYING;
        }
        
        layer_mark_dirty(s_canvas_layer);
        s_timer = app_timer_register(1000 / FPS, game_loop, NULL);
        return;
    }

    if (s_state == GAME_STATE_GAME_OVER) return;

    // Boat Physics
    s_boat_velocity -= s_water_angle / GRAVITY_MODIFIER; 
    s_boat_velocity = (s_boat_velocity * FRICTION) / 10;
    s_boat_x += s_boat_velocity;

    if (s_boat_x < 20) { s_boat_x = 20; s_boat_velocity = 0; }
    if (s_boat_x > SCREEN_W - 20) { s_boat_x = SCREEN_W - 20; s_boat_velocity = 0; }

    int dx = s_boat_x - (SCREEN_W / 2);
    s_boat_y = (SCREEN_H / 2) + (dx * s_water_angle) / 100;

    // --- Dynamic Motion & Tilt Physics for Particles ---
    
    // Dynamic Splash: Trails opposite movement & scales angle directly with watch tilt angle
    if (s_boat_velocity > 1 || s_boat_velocity < -1) {
        int dir = (s_boat_velocity > 0) ? 1 : -1;
        int spawn_x = s_boat_x - (dir * 10) + (rand() % 6 - 3);
        int spawn_y = s_boat_y + 4;

        // X velocity combines opposing boat momentum and tilt vector forces
        int splash_vx = (-s_boat_velocity / 2) - (s_water_angle / 6) + (rand() % 3 - 1);
        int splash_vy = -1 - (abs(s_boat_velocity) / 3) - (rand() % 2);
        
        spawn_particle(PARTICLE_SPLASH, spawn_x, spawn_y, splash_vx, splash_vy, 6 + (rand() % 5));
    }

    // Chimney Smoke: Drifts naturally away from boat movement & bends with tilt angle
    if (s_tick % 6 == 0) {
        int smoke_x = s_boat_x + 5 + (rand() % 3 - 1);
        int smoke_y = s_boat_y - 20;

        // Smoke velocity tilts naturally with device orientation and boat motion drag
        int smoke_vx = (-s_boat_velocity / 3) + (s_water_angle / 8) + (rand() % 3 - 1);
        int smoke_vy = -2 - (rand() % 2);

        spawn_particle(PARTICLE_SMOKE, smoke_x, smoke_y, smoke_vx, smoke_vy, 16 + (rand() % 8));
    }

    // Update Particles
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (s_particles[i].life > 0) {
            s_particles[i].x += s_particles[i].vx;
            s_particles[i].y += s_particles[i].vy;
            s_particles[i].life--;
        }
    }

    s_wave_phase = (s_wave_phase + 1500) % TRIG_MAX_ANGLE;

    // Parallax Island Depth
    if (s_tick % 3 == 0) {
        s_island_x -= 1; 
    }
    if (s_island_x < -80) {
        s_island_x = SCREEN_W + 150 + (rand() % 100);
    }

    // Obstacle Logic
    s_obstacle_x -= 4; 
    if (s_obstacle_x < -40) {
        s_obstacle_x = SCREEN_W + 20;
        s_obstacle_gap_y = (rand() % 80) + (SCREEN_H / 2 - 40);
        s_score++;
    }

    // Collision Check
    if (s_obstacle_x < s_boat_x + 15 && s_obstacle_x > s_boat_x - 15) {
        if (s_boat_y < s_obstacle_gap_y - 25 || s_boat_y > s_obstacle_gap_y + 25) {
            s_state = GAME_STATE_GAME_OVER;
        }
    }

    layer_mark_dirty(s_canvas_layer);
    s_timer = app_timer_register(1000 / FPS, game_loop, NULL);
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    // 1. Sky Background
    graphics_context_set_fill_color(ctx, GColorPictonBlue);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);

    // --- Living Sky Layer ---
    GRect sun_b = gbitmap_get_bounds(s_sun_bitmap);
    int sun_x = 145;
    int sun_y = 10;
    graphics_draw_bitmap_in_rect(ctx, s_sun_bitmap, GRect(sun_x, sun_y, sun_b.size.w, sun_b.size.h));

    // Dynamic Sun Face Logic
    int dist_to_hazard = s_obstacle_x > s_boat_x ? s_obstacle_x - s_boat_x : s_boat_x - s_obstacle_x;
    bool is_panicking = (dist_to_hazard < 50 && s_state == GAME_STATE_PLAYING);
    bool is_blinking = (s_tick % 120 < 6); 

    int cx = sun_x + 20; 
    int cy = sun_y + 20;

    // Blank out underlying static face
    graphics_context_set_fill_color(ctx, GColorYellow);
    graphics_fill_circle(ctx, GPoint(cx, cy + 2), 10);

    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);

    // Draw Eyes
    if (is_blinking && !is_panicking) {
        graphics_draw_line(ctx, GPoint(cx - 7, cy - 4), GPoint(cx - 3, cy - 4));
        graphics_draw_line(ctx, GPoint(cx + 3, cy - 4), GPoint(cx + 7, cy - 4));
    } else {
        int eye_y = is_panicking ? cy - 6 : cy - 4; 
        int eye_size = is_panicking ? 3 : 2; 
        graphics_fill_circle(ctx, GPoint(cx - 5, eye_y), eye_size);
        graphics_fill_circle(ctx, GPoint(cx + 5, eye_y), eye_size);
    }

    // Draw Mouth
    if (is_panicking) {
        graphics_fill_circle(ctx, GPoint(cx, cy + 6), 3);
    } else {
        graphics_draw_line(ctx, GPoint(cx - 6, cy + 3), GPoint(cx, cy + 6));
        graphics_draw_line(ctx, GPoint(cx, cy + 6), GPoint(cx + 6, cy + 3));
    }

    // Bouncing Clouds
    GRect cloud_b = gbitmap_get_bounds(s_cloud_bitmap);
    
    int c1_pop = abs((sin_lookup((s_tick * 250) % TRIG_MAX_ANGLE) * 5) / TRIG_MAX_RATIO);
    graphics_draw_bitmap_in_rect(ctx, s_cloud_bitmap, GRect(15, 25 - c1_pop, cloud_b.size.w, cloud_b.size.h));
    
    int c2_pop = abs((sin_lookup(((s_tick + 60) * 300) % TRIG_MAX_ANGLE) * 4) / TRIG_MAX_RATIO);
    graphics_draw_bitmap_in_rect(ctx, s_cloud_bitmap, GRect(110, 45 - c2_pop, cloud_b.size.w, cloud_b.size.h));

    // --- Environment Depth & Island Layer ---
    GRect island_b = gbitmap_get_bounds(s_island_bitmap);
    int island_horizon_y = (SCREEN_H / 2) + ((s_island_x - (SCREEN_W / 2)) * s_water_angle) / 100;
    graphics_draw_bitmap_in_rect(ctx, s_island_bitmap, GRect(s_island_x, island_horizon_y - island_b.size.h + 12, island_b.size.w, island_b.size.h));

    // --- Water Surface Path ---
    GPoint wave_points[NUM_WAVE_POINTS + 3];
    int step_x = SCREEN_W / (NUM_WAVE_POINTS - 1);

    for (int i = 0; i < NUM_WAVE_POINTS; i++) {
        int px = i * step_x;
        int tilt_offset = ((px - (SCREEN_W / 2)) * s_water_angle) / 100;
        int wave_angle = (s_wave_phase + (px * 300)) % TRIG_MAX_ANGLE;
        int wave_height = (sin_lookup(wave_angle) * 4) / TRIG_MAX_RATIO; 

        wave_points[i] = GPoint(px, (SCREEN_H / 2) + tilt_offset + wave_height);
    }

    wave_points[NUM_WAVE_POINTS]     = GPoint(SCREEN_W, SCREEN_H);
    wave_points[NUM_WAVE_POINTS + 1] = GPoint(0, SCREEN_H);

    GPathInfo path_info = { .num_points = NUM_WAVE_POINTS + 2, .points = wave_points };
    GPath *water_path = gpath_create(&path_info);

    graphics_context_set_fill_color(ctx, GColorCobaltBlue);
    gpath_draw_filled(ctx, water_path);

    // Dynamic Water Textures
    graphics_context_set_stroke_color(ctx, GColorCeleste); 
    graphics_context_set_stroke_width(ctx, 1);
    for (int row = 0; row < 5; row++) {
        int y_base = (SCREEN_H / 2) + 25 + (row * 25);
        int offset_x = (s_wave_phase / 300 + row * 15) % 40; 
        
        for (int x = -40; x < SCREEN_W; x += 40) {
            int px = x + offset_x;
            int py = y_base + ((px - (SCREEN_W / 2)) * s_water_angle) / 100; 
            if (px > 0 && px < SCREEN_W && py < SCREEN_H) {
                graphics_draw_line(ctx, GPoint(px, py), GPoint(px + 12, py));
            }
        }
    }

    // Surface Stroke
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 3);
    for (int i = 0; i < NUM_WAVE_POINTS - 1; i++) {
        graphics_draw_line(ctx, wave_points[i], wave_points[i + 1]);
    }
    gpath_destroy(water_path);

    // --- Obstacles ---
    GRect anchor_b = gbitmap_get_bounds(s_anchor_bitmap);
    GRect mine_b = gbitmap_get_bounds(s_mine_bitmap);

    graphics_context_set_stroke_width(ctx, 2);
    
    // Anchor Dashed Tether
    for (int y = 0; y < s_obstacle_gap_y - 30; y += 12) {
        graphics_draw_line(ctx, GPoint(s_obstacle_x + 10, y), GPoint(s_obstacle_x + 10, y + 6));
    }
    graphics_draw_bitmap_in_rect(ctx, s_anchor_bitmap, GRect(s_obstacle_x + 10 - anchor_b.size.w/2, s_obstacle_gap_y - 30 - anchor_b.size.h, anchor_b.size.w, anchor_b.size.h));

    // Mine Dashed Tether
    for (int y = s_obstacle_gap_y + 30; y < SCREEN_H; y += 12) {
        graphics_draw_line(ctx, GPoint(s_obstacle_x + 10, y), GPoint(s_obstacle_x + 10, y + 6));
    }
    graphics_draw_bitmap_in_rect(ctx, s_mine_bitmap, GRect(s_obstacle_x + 10 - mine_b.size.w/2, s_obstacle_gap_y + 30, mine_b.size.w, mine_b.size.h));

    // --- Trajectory Line ---
    if (s_state == GAME_STATE_PLAYING) {
        int dir = 0;
        if (s_boat_velocity != 0) {
            dir = (s_boat_velocity > 0) ? 1 : -1;
        } else if (s_water_angle != 0) {
            dir = (s_water_angle < 0) ? 1 : -1; 
        }

        if (dir != 0) {
            graphics_context_set_fill_color(ctx, GColorYellow);
            for (int i = 1; i <= 5; i++) {
                int dot_x = s_boat_x + (dir * i * 10);
                if (dot_x >= 5 && dot_x <= SCREEN_W - 5) {
                    int tilt_offset = ((dot_x - (SCREEN_W / 2)) * s_water_angle) / 100;
                    int wave_angle = (s_wave_phase + (dot_x * 300)) % TRIG_MAX_ANGLE;
                    int wave_height = (sin_lookup(wave_angle) * 4) / TRIG_MAX_RATIO;
                    int dot_y = (SCREEN_H / 2) + tilt_offset + wave_height - 3;
                    
                    graphics_fill_circle(ctx, GPoint(dot_x, dot_y), 2);
                }
            }
        }
    }

    // --- Dynamic Particle Rendering ---
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (s_particles[i].life > 0) {
            if (s_particles[i].type == PARTICLE_SPLASH) {
                // Water splash droplets
                graphics_context_set_fill_color(ctx, GColorWhite);
                graphics_fill_circle(ctx, GPoint(s_particles[i].x, s_particles[i].y), 2);
            } else if (s_particles[i].type == PARTICLE_SMOKE) {
                // Expanding smoke particles
                graphics_context_set_fill_color(ctx, GColorDarkGray);
                int radius = 1 + ((s_particles[i].max_life - s_particles[i].life) / 4);
                graphics_fill_circle(ctx, GPoint(s_particles[i].x, s_particles[i].y), radius);
            }
        }
    }

    // Boat Sprite
    GRect boat_b = gbitmap_get_bounds(s_boat_bitmap);
    graphics_draw_bitmap_in_rect(ctx, s_boat_bitmap, GRect(s_boat_x - boat_b.size.w / 2, s_boat_y - boat_b.size.h + 5, boat_b.size.w, boat_b.size.h));

    // Score Display
    char score_buffer[16];
    snprintf(score_buffer, sizeof(score_buffer), "%d", s_score);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, score_buffer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), 
                       GRect(0, 10, SCREEN_W, 50), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // Countdown UI Overlay
    if (s_state == GAME_STATE_COUNTDOWN) {
        int count = (s_countdown_timer / 30) + 1;
        char count_buf[16];
        snprintf(count_buf, sizeof(count_buf), "%d", count);
        
        graphics_context_set_text_color(ctx, GColorYellow);
        graphics_draw_text(ctx, count_buf, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), 
                           GRect(0, SCREEN_H / 2 - 40, SCREEN_W, 50), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // Game Over Screen
    if (s_state == GAME_STATE_GAME_OVER) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_rect(ctx, GRect(20, SCREEN_H / 2 - 30, SCREEN_W - 40, 60), 5, GCornersAll);
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_rect(ctx, GRect(20, SCREEN_H / 2 - 30, SCREEN_W - 40, 60));
        graphics_context_set_text_color(ctx, GColorBlack);
        graphics_draw_text(ctx, "SUNK!\nShake to retry", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                           GRect(20, SCREEN_H / 2 - 25, SCREEN_W - 40, 60), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
}

static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    s_boat_bitmap   = gbitmap_create_with_resource(RESOURCE_ID_BOAT);
    s_anchor_bitmap = gbitmap_create_with_resource(RESOURCE_ID_ANCHOR);
    s_mine_bitmap   = gbitmap_create_with_resource(RESOURCE_ID_MINE);
    s_sun_bitmap    = gbitmap_create_with_resource(RESOURCE_ID_SUN);
    s_cloud_bitmap  = gbitmap_create_with_resource(RESOURCE_ID_CLOUD);
    s_island_bitmap = gbitmap_create_with_resource(RESOURCE_ID_ISLAND);

    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    srand(time(NULL));
    s_timer = app_timer_register(1000 / FPS, game_loop, NULL);
}

static void main_window_unload(Window *window) {
    gbitmap_destroy(s_boat_bitmap);
    gbitmap_destroy(s_anchor_bitmap);
    gbitmap_destroy(s_mine_bitmap);
    gbitmap_destroy(s_sun_bitmap);
    gbitmap_destroy(s_cloud_bitmap);
    gbitmap_destroy(s_island_bitmap);
    layer_destroy(s_canvas_layer);
}

static void init(void) {
    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });
    window_stack_push(s_main_window, true);
    accel_data_service_subscribe(10, accel_data_handler);
    accel_tap_service_subscribe(tap_handler);
}

static void deinit(void) {
    accel_data_service_unsubscribe();
    accel_tap_service_unsubscribe();
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}