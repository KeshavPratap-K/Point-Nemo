#include <pebble.h>

#define SCREEN_W 200
#define SCREEN_H 228
#define FPS 30
#define NUM_WAVE_POINTS 11
#define MAX_PARTICLES 40
#define PERSIST_KEY_HIGH_SCORE 1001

typedef enum {
    GAME_STATE_COUNTDOWN,
    GAME_STATE_PLAYING,
    GAME_STATE_GAME_OVER
} GameState;

typedef enum {
    PARTICLE_SPLASH,
    PARTICLE_SMOKE
} ParticleType;

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

static GBitmap *s_boat_bitmap;
static GBitmap *s_anchor_bitmap;
static GBitmap *s_mine_bitmap;
static GBitmap *s_island_bitmap;

static Particle s_particles[MAX_PARTICLES];

static GameState s_state = GAME_STATE_COUNTDOWN;
static int s_countdown_timer = 90; 
static uint32_t s_tick = 0;        

#define PERSIST_KEY_BACKLIGHT 1002
static bool s_backlight_always_on = false;
static Window *s_settings_window;
static SimpleMenuLayer *s_simple_menu_layer;
static SimpleMenuSection s_menu_sections[1];
static SimpleMenuItem s_menu_items[1];

static int s_water_angle = 0; 
static int s_water_base_y = SCREEN_H / 2;
static int s_wave_phase = 0;

static int s_boat_x = SCREEN_W / 2;
static int s_boat_y = SCREEN_H / 2;

static int s_obstacle_x = SCREEN_W + 50;
static int s_obstacle_gap_y = SCREEN_H / 2;
static int s_obstacle_speed = 4;

static bool s_powerup_active = false;
static int s_powerup_x = -50;
static int s_powerup_y = 0;
static bool s_shield_active = false;
static int s_shield_timer = 0;

static int s_island_x = SCREEN_W + 40;
static int s_lighthouse_island_x = -120;
static int s_cloud1_x = 15;
static int s_cloud2_x = 110;

static int s_score = 0;
static int s_high_score = 0;

static void game_loop(void *data);

static void load_high_score(void) {
    if (persist_exists(PERSIST_KEY_HIGH_SCORE)) {
        s_high_score = persist_read_int(PERSIST_KEY_HIGH_SCORE);
    } else {
        s_high_score = 0;
    }
}

static void save_high_score(void) {
    if (s_score > s_high_score) {
        s_high_score = s_score;
        persist_write_int(PERSIST_KEY_HIGH_SCORE, s_high_score);
    }
}

static void reset_game(void) {
    s_state = GAME_STATE_COUNTDOWN;
    s_countdown_timer = 90;
    s_score = 0;
    s_obstacle_speed = 4;
    s_boat_x = SCREEN_W / 2;
    s_obstacle_x = SCREEN_W + 50;
    s_island_x = SCREEN_W + 40;
    s_lighthouse_island_x = -120;
    s_powerup_active = false;
    s_shield_active = false;
    s_shield_timer = 0;
    
    for (int i = 0; i < MAX_PARTICLES; i++) {
        s_particles[i].life = 0;
    }
}

static void update_backlight_subtitle(void) {
    s_menu_items[0].subtitle = s_backlight_always_on ? "Always On" : "Default";
    if (s_simple_menu_layer) {
        layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
    }
}

static void backlight_select_callback(int index, void *ctx) {
    s_backlight_always_on = !s_backlight_always_on;
    persist_write_bool(PERSIST_KEY_BACKLIGHT, s_backlight_always_on);
    light_enable(s_backlight_always_on);
    update_backlight_subtitle();
}

static void settings_window_load(Window *window) {
    s_menu_items[0] = (SimpleMenuItem) {
        .title = "Backlight",
        .callback = backlight_select_callback,
    };
    update_backlight_subtitle();

    s_menu_sections[0] = (SimpleMenuSection) {
        .title = "Settings",
        .num_items = 1,
        .items = s_menu_items,
    };

    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_frame(window_layer);

    s_simple_menu_layer = simple_menu_layer_create(bounds, window, s_menu_sections, 1, NULL);
    layer_add_child(window_layer, simple_menu_layer_get_layer(s_simple_menu_layer));
}

static void settings_window_unload(Window *window) {
    simple_menu_layer_destroy(s_simple_menu_layer);
    window_destroy(window);
    s_settings_window = NULL;
}

static void select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (!s_settings_window) {
        s_settings_window = window_create();
        window_set_window_handlers(s_settings_window, (WindowHandlers) {
            .load = settings_window_load,
            .unload = settings_window_unload,
        });
    }
    window_stack_push(s_settings_window, true);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_state == GAME_STATE_GAME_OVER) {
        reset_game();
        s_timer = app_timer_register(1000 / FPS, game_loop, NULL);
    }
}

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
    window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long_click_handler, NULL);
}

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
    s_tick++;

    AccelData accel;
    if (accel_service_peek(&accel) == 0) {
        int target_angle = accel.x / 15;
        s_water_angle = (s_water_angle * 3 + target_angle) / 4; 
    }

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

    s_water_base_y = (SCREEN_H / 2) + (s_water_angle * 13) / 10;
    
    int center_wave_angle = (s_wave_phase + (s_boat_x * 300)) % TRIG_MAX_ANGLE;
    int center_wave_height = (sin_lookup(center_wave_angle) * 4) / TRIG_MAX_RATIO;
    s_boat_y = s_water_base_y + center_wave_height;

    // Splash Particles
    if (s_tick % 3 == 0) {
        int spawn_x = s_boat_x - 5 + (rand() % 10 - 5);
        int spawn_y = s_boat_y + 4;
        int splash_vx = -3 - (rand() % 3); 
        int splash_vy = -1 - (rand() % 2);
        spawn_particle(PARTICLE_SPLASH, spawn_x, spawn_y, splash_vx, splash_vy, 6 + (rand() % 5));
    }

    // Smoke Particles
    if (s_tick % 6 == 0) {
        int smoke_x = s_boat_x + 5 + (rand() % 3 - 1);
        int smoke_y = s_boat_y - 20;
        int smoke_vx = -4 - (rand() % 2);
        int smoke_vy = -2 - (rand() % 2);
        spawn_particle(PARTICLE_SMOKE, smoke_x, smoke_y, smoke_vx, smoke_vy, 16 + (rand() % 8));
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (s_particles[i].life > 0) {
            s_particles[i].x += s_particles[i].vx;
            s_particles[i].y += s_particles[i].vy;
            s_particles[i].life--;
        }
    }

    s_wave_phase = (s_wave_phase + 1500) % TRIG_MAX_ANGLE;

    // Cloud Parallax
    if (s_tick % 5 == 0) s_cloud1_x -= 1;
    if (s_tick % 3 == 0) s_cloud2_x -= 1;
    if (s_cloud1_x < -40) s_cloud1_x = SCREEN_W + 10;
    if (s_cloud2_x < -40) s_cloud2_x = SCREEN_W + 10;

    // Parallax Islands Depth
    if (s_tick % 2 == 0) {
        s_island_x -= 1; 
        s_lighthouse_island_x -= 1;
    } 
    if (s_island_x < -80) {
        s_island_x = SCREEN_W + 150 + (rand() % 100);
    }
    if (s_lighthouse_island_x < -100) {
        s_lighthouse_island_x = SCREEN_W + 180 + (rand() % 100);
    }

    // Obstacle Logic
    s_obstacle_speed = 4 + (s_score / 4);
    if (s_obstacle_speed > 9) s_obstacle_speed = 9;

    s_obstacle_x -= s_obstacle_speed; 
    if (s_obstacle_x < -40) {
        s_obstacle_x = SCREEN_W + 20;
        s_obstacle_gap_y = (rand() % 80) + (SCREEN_H / 2 - 40);
        s_score++;
        vibes_short_pulse();
        save_high_score();

        if ((rand() % 100) < 35 && !s_shield_active) {
            s_powerup_active = true;
            s_powerup_x = s_obstacle_x + 10;
            s_powerup_y = s_obstacle_gap_y;
        }
    }

    // Power-up Logic
    if (s_powerup_active) {
        s_powerup_x -= s_obstacle_speed;
        int pdx = s_powerup_x - s_boat_x;
        int pdy = s_powerup_y - s_boat_y;
        if (pdx * pdx + pdy * pdy < 22 * 22) {
            s_powerup_active = false;
            s_shield_active = true;
            s_shield_timer = 240;
            vibes_short_pulse();
        }
    }

    if (s_shield_active) {
        s_shield_timer--;
        if (s_shield_timer <= 0) {
            s_shield_active = false;
        }
    }

    // Collision Check
    if (s_obstacle_x < s_boat_x + 15 && s_obstacle_x > s_boat_x - 15) {
        if (s_boat_y < s_obstacle_gap_y - 25 || s_boat_y > s_obstacle_gap_y + 25) {
            if (s_shield_active) {
                s_shield_active = false;
                s_obstacle_x = -50;
                vibes_short_pulse();
            } else {
                s_state = GAME_STATE_GAME_OVER;
                vibes_double_pulse();
                save_high_score();
            }
        }
    }

    layer_mark_dirty(s_canvas_layer);
    s_timer = app_timer_register(1000 / FPS, game_loop, NULL);
}

static void draw_sun_sprite(GContext *ctx, int x, int y) {
    graphics_context_set_stroke_width(ctx, 2);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_line(ctx, GPoint(x + 20, y + 2), GPoint(x + 20, y + 38));
    graphics_draw_line(ctx, GPoint(x + 2, y + 20), GPoint(x + 38, y + 20));
    graphics_draw_line(ctx, GPoint(x + 7, y + 7), GPoint(x + 33, y + 33));
    graphics_draw_line(ctx, GPoint(x + 7, y + 33), GPoint(x + 33, y + 7));
    
    // Use filled circles for a perfectly even border instead of relying on stroke width
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(x + 20, y + 20), 12);
    
    graphics_context_set_fill_color(ctx, GColorYellow);
    graphics_fill_circle(ctx, GPoint(x + 20, y + 20), 10);
}

static void draw_cloud_sprite(GContext *ctx, int x, int y) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);
    
    graphics_draw_circle(ctx, GPoint(x + 10, y + 20), 8);
    graphics_draw_circle(ctx, GPoint(x + 20, y + 13), 10);
    graphics_draw_circle(ctx, GPoint(x + 33, y + 17), 9);
    graphics_draw_circle(ctx, GPoint(x + 43, y + 23), 6);
    
    graphics_context_set_stroke_width(ctx, 1);
    graphics_fill_circle(ctx, GPoint(x + 10, y + 20), 7);
    graphics_fill_circle(ctx, GPoint(x + 20, y + 13), 9);
    graphics_fill_circle(ctx, GPoint(x + 33, y + 17), 8);
    graphics_fill_circle(ctx, GPoint(x + 43, y + 23), 5);
    
    graphics_fill_rect(ctx, GRect(x + 10, y + 17, 33, 11), 0, GCornerNone);
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorPictonBlue);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);

    // Living Sky Layer
    int sun_x = 145;
    int sun_y = 10;
    draw_sun_sprite(ctx, sun_x, sun_y);

    int dist_to_hazard = s_obstacle_x > s_boat_x ? s_obstacle_x - s_boat_x : s_boat_x - s_obstacle_x;
    bool is_panicking = (dist_to_hazard < 50 && s_state == GAME_STATE_PLAYING);
    bool is_blinking = (s_tick % 120 < 6); 

    int cx = sun_x + 20; 
    int cy = sun_y + 20;



    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);

    if (is_blinking && !is_panicking) {
        graphics_draw_line(ctx, GPoint(cx - 7, cy - 4), GPoint(cx - 3, cy - 4));
        graphics_draw_line(ctx, GPoint(cx + 3, cy - 4), GPoint(cx + 7, cy - 4));
    } else {
        int eye_y = is_panicking ? cy - 6 : cy - 4; 
        int eye_size = is_panicking ? 3 : 2; 
        graphics_fill_circle(ctx, GPoint(cx - 5, eye_y), eye_size);
        graphics_fill_circle(ctx, GPoint(cx + 5, eye_y), eye_size);
    }

    if (is_panicking) {
        graphics_fill_circle(ctx, GPoint(cx, cy + 6), 3);
    } else {
        graphics_draw_line(ctx, GPoint(cx - 6, cy + 3), GPoint(cx, cy + 6));
        graphics_draw_line(ctx, GPoint(cx, cy + 6), GPoint(cx + 6, cy + 3));
    }

    // Clouds Parallax
    int c1_pop = abs((sin_lookup((s_tick * 250) % TRIG_MAX_ANGLE) * 5) / TRIG_MAX_RATIO);
    draw_cloud_sprite(ctx, s_cloud1_x, 25 - c1_pop);
    
    int c2_pop = abs((sin_lookup(((s_tick + 60) * 300) % TRIG_MAX_ANGLE) * 4) / TRIG_MAX_RATIO);
    draw_cloud_sprite(ctx, s_cloud2_x, 45 - c2_pop);

    // --- Lighthouse Island Layer ---
    int bg_angle = (s_water_angle * 7) / 10;
    int lh_horizon_y = (s_water_base_y - 12) + ((s_lighthouse_island_x + 15 - (SCREEN_W / 2)) * bg_angle) / 100;
    
    // Draw Natural Island Hill
    graphics_context_set_fill_color(ctx, GColorDarkGreen);
    graphics_fill_circle(ctx, GPoint(s_lighthouse_island_x + 15, lh_horizon_y + 2), 22);
    
    // Draw Lighthouse Structure
    int lh_base_x = s_lighthouse_island_x + 15;
    int lh_base_y = lh_horizon_y - 4;
    
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(lh_base_x - 4, lh_base_y - 32, 8, 32), 0, GCornerNone);
    
    // Red stripes on lighthouse
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_rect(ctx, GRect(lh_base_x - 4, lh_base_y - 24, 8, 6), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(lh_base_x - 4, lh_base_y - 10, 8, 6), 0, GCornerNone);

    // Lighthouse Top Lamp House
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(lh_base_x - 5, lh_base_y - 38, 10, 6), 1, GCornersAll);

    // --- Volumetric Dual-Beam Sweeping Light Cones (Uniform Proportional 3D Perspective) ---
    int lamp_x = lh_base_x;
    int lamp_y = lh_base_y - 35;

    int angle1 = (s_tick * 80) % TRIG_MAX_ANGLE;
    int angle2 = (angle1 + (TRIG_MAX_ANGLE / 2)) % TRIG_MAX_ANGLE;

    for (int i = 0; i < 2; i++) {
        int cur_angle = (i == 0) ? angle1 : angle2;
        int sin_v = sin_lookup(cur_angle);
        int cos_v = cos_lookup(cur_angle); // Depth factor (-TRIG_MAX_RATIO to TRIG_MAX_RATIO)

        // Unified length scaling based on depth (cos_v)
        // Foreground: ~65 length, Backside: ~35 length
        int length = 50 + (cos_v * 18) / TRIG_MAX_RATIO;
        int tip_x = lamp_x + (sin_v * length) / TRIG_MAX_RATIO;
        
        // Proportional vertical drop to keep the exact same cone angle on both sides
        int tip_y = lamp_y + (length * 26) / 50; 
        int half_w = (length * 16) / 50;
        int core_w = (length * 5) / 50;

        bool is_backside = (cos_v < 0);
        GColor beam_color = is_backside ? GColorPastelYellow : GColorYellow;

        // 1. Scattered Dim Outer Rays
        graphics_context_set_stroke_color(ctx, is_backside ? GColorLightGray : GColorPastelYellow);
        graphics_context_set_stroke_width(ctx, 1);
        for (int r = -half_w - 5; r <= half_w + 5; r += 5) {
            if (abs(r) < core_w) continue;
            graphics_draw_line(ctx, GPoint(lamp_x, lamp_y), GPoint(tip_x + r, tip_y + (abs(r) / 4)));
        }

        // 2. Mid Beam Wedge
        GPoint beam_points[] = {
            GPoint(lamp_x, lamp_y),
            GPoint(tip_x - half_w, tip_y),
            GPoint(tip_x + half_w, tip_y)
        };
        GPathInfo beam_info = { .num_points = 3, .points = beam_points };
        GPath *beam_path = gpath_create(&beam_info);
        graphics_context_set_fill_color(ctx, beam_color);
        gpath_draw_filled(ctx, beam_path);
        gpath_destroy(beam_path);

        // 3. Inner Core Beam
        GPoint core_points[] = {
            GPoint(lamp_x, lamp_y),
            GPoint(tip_x - core_w, tip_y - 2),
            GPoint(tip_x + core_w, tip_y - 2)
        };
        GPathInfo core_info = { .num_points = 3, .points = core_points };
        GPath *core_path = gpath_create(&core_info);
        graphics_context_set_fill_color(ctx, is_backside ? GColorYellow : GColorWhite);
        gpath_draw_filled(ctx, core_path);
        gpath_destroy(core_path);
    }

    // Ultra-bright light source bulb center
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(lamp_x, lamp_y), 2);

    // --- Standard Island Layer ---
    GRect island_b = gbitmap_get_bounds(s_island_bitmap);
    int island_horizon_y = (s_water_base_y - 12) + ((s_island_x - (SCREEN_W / 2)) * bg_angle) / 100;
    graphics_draw_bitmap_in_rect(ctx, s_island_bitmap, GRect(s_island_x, island_horizon_y - island_b.size.h + 12, island_b.size.w, island_b.size.h));

    // --- Background Water Layer (3D Depth) ---
    GPoint bg_wave_points[NUM_WAVE_POINTS + 3];
    int step_x = SCREEN_W / (NUM_WAVE_POINTS - 1);
    int bg_base_y = s_water_base_y - 12;

    for (int i = 0; i < NUM_WAVE_POINTS; i++) {
        int px = i * step_x;
        int tilt_offset = ((px - (SCREEN_W / 2)) * bg_angle) / 100;
        int wave_angle = (s_wave_phase + 8000 + (px * 400)) % TRIG_MAX_ANGLE; 
        int wave_height = (sin_lookup(wave_angle) * 3) / TRIG_MAX_RATIO; 
        bg_wave_points[i] = GPoint(px, bg_base_y + tilt_offset + wave_height);
    }
    bg_wave_points[NUM_WAVE_POINTS]     = GPoint(SCREEN_W, SCREEN_H);
    bg_wave_points[NUM_WAVE_POINTS + 1] = GPoint(0, SCREEN_H);

    GPathInfo bg_path_info = { .num_points = NUM_WAVE_POINTS + 2, .points = bg_wave_points };
    GPath *bg_water_path = gpath_create(&bg_path_info);
    
    graphics_context_set_fill_color(ctx, GColorDukeBlue); 
    gpath_draw_filled(ctx, bg_water_path);
    gpath_destroy(bg_water_path);

    // --- Foreground Water Layer ---
    GPoint wave_points[NUM_WAVE_POINTS + 3];

    for (int i = 0; i < NUM_WAVE_POINTS; i++) {
        int px = i * step_x;
        int tilt_offset = ((px - (SCREEN_W / 2)) * s_water_angle) / 100;
        int wave_angle = (s_wave_phase + (px * 300)) % TRIG_MAX_ANGLE;
        int wave_height = (sin_lookup(wave_angle) * 4) / TRIG_MAX_RATIO; 
        wave_points[i] = GPoint(px, s_water_base_y + tilt_offset + wave_height);
    }
    wave_points[NUM_WAVE_POINTS]     = GPoint(SCREEN_W, SCREEN_H);
    wave_points[NUM_WAVE_POINTS + 1] = GPoint(0, SCREEN_H);

    GPathInfo path_info = { .num_points = NUM_WAVE_POINTS + 2, .points = wave_points };
    GPath *water_path = gpath_create(&path_info);

    graphics_context_set_fill_color(ctx, GColorCobaltBlue);
    gpath_draw_filled(ctx, water_path);

    // Water Textures
    graphics_context_set_stroke_color(ctx, GColorCeleste); 
    graphics_context_set_stroke_width(ctx, 1);
    for (int row = 0; row < 5; row++) {
        int y_base = s_water_base_y + 25 + (row * 25);
        int offset_x = (s_wave_phase / 300 + row * 15) % 40; 
        
        for (int x = -40; x < SCREEN_W; x += 40) {
            int px = x + offset_x;
            int py = y_base + ((px - (SCREEN_W / 2)) * s_water_angle) / 100; 
            if (px > 0 && px < SCREEN_W && py < SCREEN_H) {
                graphics_draw_line(ctx, GPoint(px, py), GPoint(px + 12, py));
            }
        }
    }

    // Surface Line
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 3);
    for (int i = 0; i < NUM_WAVE_POINTS - 1; i++) {
        graphics_draw_line(ctx, wave_points[i], wave_points[i + 1]);
    }
    gpath_destroy(water_path);

    // Obstacles
    GRect anchor_b = gbitmap_get_bounds(s_anchor_bitmap);
    GRect mine_b = gbitmap_get_bounds(s_mine_bitmap);
    graphics_context_set_stroke_width(ctx, 2);
    
    for (int y = 0; y < s_obstacle_gap_y - 30; y += 12) {
        graphics_draw_line(ctx, GPoint(s_obstacle_x + 10, y), GPoint(s_obstacle_x + 10, y + 6));
    }
    graphics_draw_bitmap_in_rect(ctx, s_anchor_bitmap, GRect(s_obstacle_x + 10 - anchor_b.size.w/2, s_obstacle_gap_y - 30 - anchor_b.size.h, anchor_b.size.w, anchor_b.size.h));

    for (int y = s_obstacle_gap_y + 30; y < SCREEN_H; y += 12) {
        graphics_draw_line(ctx, GPoint(s_obstacle_x + 10, y), GPoint(s_obstacle_x + 10, y + 6));
    }
    graphics_draw_bitmap_in_rect(ctx, s_mine_bitmap, GRect(s_obstacle_x + 10 - mine_b.size.w/2, s_obstacle_gap_y + 30, mine_b.size.w, mine_b.size.h));

    // Shield Power-Up
    if (s_powerup_active) {
        int bob_y = s_powerup_y + (sin_lookup((s_tick * 800) % TRIG_MAX_ANGLE) * 4) / TRIG_MAX_RATIO;
        graphics_context_set_fill_color(ctx, GColorCyan);
        graphics_fill_circle(ctx, GPoint(s_powerup_x, bob_y), 7);
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_circle(ctx, GPoint(s_powerup_x, bob_y), 7);
    }

    // Particles (Behind Boat)
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (s_particles[i].life > 0) {
            if (s_particles[i].type == PARTICLE_SPLASH) {
                graphics_context_set_fill_color(ctx, GColorWhite);
                graphics_fill_circle(ctx, GPoint(s_particles[i].x, s_particles[i].y), 2);
            } else if (s_particles[i].type == PARTICLE_SMOKE) {
                graphics_context_set_fill_color(ctx, GColorDarkGray);
                int radius = 1 + ((s_particles[i].max_life - s_particles[i].life) / 4);
                graphics_fill_circle(ctx, GPoint(s_particles[i].x, s_particles[i].y), radius);
            }
        }
    }

    // Shield Aura
    if (s_shield_active) {
        GColor shield_color = (s_shield_timer < 60 && (s_tick % 6 < 3)) ? GColorBrightGreen : GColorElectricBlue;
        graphics_context_set_stroke_color(ctx, shield_color);
        graphics_context_set_stroke_width(ctx, 2);
        int shield_r = 18 + (sin_lookup((s_tick * 600) % TRIG_MAX_ANGLE) * 2) / TRIG_MAX_RATIO;
        graphics_draw_circle(ctx, GPoint(s_boat_x, s_boat_y - 6), shield_r);
    }

    // Boat Sprite
    GRect boat_b = gbitmap_get_bounds(s_boat_bitmap);
    graphics_draw_bitmap_in_rect(ctx, s_boat_bitmap, GRect(s_boat_x - boat_b.size.w / 2, s_boat_y - boat_b.size.h + 5, boat_b.size.w, boat_b.size.h));

    // Trajectory Dots (In front of boat)
    if (s_state == GAME_STATE_PLAYING) {
        graphics_context_set_fill_color(ctx, GColorYellow);
        for (int i = 1; i <= 6; i++) {
            int dot_x = s_boat_x + (i * 12);
            if (dot_x < SCREEN_W) {
                int tilt_offset = ((dot_x - (SCREEN_W / 2)) * s_water_angle) / 100;
                int wave_angle = (s_wave_phase + (dot_x * 300)) % TRIG_MAX_ANGLE;
                int wave_height = (sin_lookup(wave_angle) * 4) / TRIG_MAX_RATIO;
                int dot_y = s_water_base_y + tilt_offset + wave_height - 3;
                
                graphics_fill_circle(ctx, GPoint(dot_x, dot_y), 2);
            }
        }
    }

    // Score Display
    char score_buffer[16];
    snprintf(score_buffer, sizeof(score_buffer), "%d", s_score);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, score_buffer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), 
                       GRect(0, 10, SCREEN_W, 50), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // Countdown Overlay
    if (s_state == GAME_STATE_COUNTDOWN) {
        int count = (s_countdown_timer / 30) + 1;
        char count_buf[16];
        snprintf(count_buf, sizeof(count_buf), "%d", count);
        
        graphics_context_set_text_color(ctx, GColorYellow);
        graphics_draw_text(ctx, count_buf, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), 
                           GRect(0, SCREEN_H / 2 - 40, SCREEN_W, 50), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // Game Over Overlay
    if (s_state == GAME_STATE_GAME_OVER) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_rect(ctx, GRect(15, SCREEN_H / 2 - 45, SCREEN_W - 30, 90), 6, GCornersAll);
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_rect(ctx, GRect(15, SCREEN_H / 2 - 45, SCREEN_W - 30, 90));

        char over_buf[64];
        snprintf(over_buf, sizeof(over_buf), "SUNK!\nScore: %d | Best: %d\nPress SELECT to retry", s_score, s_high_score);
        
        graphics_context_set_text_color(ctx, GColorBlack);
        graphics_draw_text(ctx, over_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 
                           GRect(15, SCREEN_H / 2 - 38, SCREEN_W - 30, 80), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
}

static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    load_high_score();

    s_boat_bitmap   = gbitmap_create_with_resource(RESOURCE_ID_BOAT);
    s_anchor_bitmap = gbitmap_create_with_resource(RESOURCE_ID_ANCHOR);
    s_mine_bitmap   = gbitmap_create_with_resource(RESOURCE_ID_MINE);
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
    gbitmap_destroy(s_island_bitmap);
    layer_destroy(s_canvas_layer);
}

static void init(void) {
    if (persist_exists(PERSIST_KEY_BACKLIGHT)) {
        s_backlight_always_on = persist_read_bool(PERSIST_KEY_BACKLIGHT);
        light_enable(s_backlight_always_on);
    }

    s_main_window = window_create();
    window_set_click_config_provider(s_main_window, click_config_provider);
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });
    window_stack_push(s_main_window, true);

    accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);
}

static void deinit(void) {
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}