#include <pebble.h>

// Design-space constants (the coordinate system the code was authored in)
#define DESIGN_W 200
#define DESIGN_H 228

// Runtime screen dimensions – set once in main_window_load()
static int SCREEN_W = DESIGN_W;
static int SCREEN_H = DESIGN_H;

// Scale a design-space X or Y value to the actual screen
#define SX(v) ((v) * SCREEN_W / DESIGN_W)
#define SY(v) ((v) * SCREEN_H / DESIGN_H)

#define FPS 30
#define NUM_WAVE_POINTS 11
#define MAX_PARTICLES 40
#define PERSIST_KEY_HIGH_SCORE 1001

typedef enum {
    GAME_STATE_MENU,
    GAME_STATE_COUNTDOWN,
    GAME_STATE_PLAYING,
    GAME_STATE_GAME_OVER
} GameState;

typedef enum {
    CONTROL_TILT,
    CONTROL_BUTTONS
} ControlMode;

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

static GBitmap *s_bmp_boat;
static GBitmap *s_bmp_anchor;
static GBitmap *s_bmp_mine;
static GBitmap *s_bmp_sun_normal;
static GBitmap *s_bmp_sun_cool;
static GBitmap *s_bmp_sun_surprised;
static GBitmap *s_bmp_cloud_single;
static GBitmap *s_bmp_cloud_double;
static GBitmap *s_bmp_island_coconut;
static GBitmap *s_bmp_island_lighthouse;

static Particle s_particles[MAX_PARTICLES];

static GameState s_state = GAME_STATE_MENU;
static ControlMode s_control_mode = CONTROL_TILT;
static int s_countdown_timer = 90; 
static uint32_t s_tick = 0;        

#define PERSIST_KEY_BACKLIGHT 1002
static bool s_backlight_always_on = false;
static Window *s_settings_window;
static SimpleMenuLayer *s_simple_menu_layer;
static SimpleMenuSection s_menu_sections[1];
static SimpleMenuItem s_menu_items[1];

static int s_water_angle = 0; 
static int s_water_base_y;
static int s_wave_phase = 0;
static int s_water_distance = 0;

static int s_boat_x;
static int s_boat_y;

static int s_obstacle_x;
static int s_obstacle_gap_y;
static int s_obstacle_speed = 4;

static bool s_powerup_active = false;
static int s_powerup_x = -50;
static int s_powerup_y = 0;
static bool s_shield_active = false;
static int s_shield_timer = 0;
static int s_sun_cool_timer = 0;

static int s_island_x;
static int s_lighthouse_island_x;
static int s_cloud1_x;
static int s_cloud2_x;

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
    s_countdown_timer = 90;
    s_score = 0;
    s_obstacle_speed = 4;
    s_water_base_y = SCREEN_H / 2;
    s_water_distance = 0;
    s_boat_x = SCREEN_W / 2;
    s_boat_y = SCREEN_H / 2;
    s_obstacle_x = SCREEN_W + SX(50);
    s_obstacle_gap_y = SCREEN_H / 2;
    s_island_x = SCREEN_W + SX(40);
    s_lighthouse_island_x = SX(-120);
    s_cloud1_x = SX(15);
    s_cloud2_x = SX(110);
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

static bool s_up_pressed = false;
static bool s_down_pressed = false;
static int s_button_angle = 0;

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

static void up_down_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_state == GAME_STATE_MENU) {
        s_control_mode = CONTROL_TILT;
        reset_game();
        s_state = GAME_STATE_COUNTDOWN;
    } else if (s_state == GAME_STATE_PLAYING && s_control_mode == CONTROL_BUTTONS) {
        s_up_pressed = true;
    }
}

static void up_up_handler(ClickRecognizerRef recognizer, void *context) {
    s_up_pressed = false;
}

static void down_down_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_state == GAME_STATE_MENU) {
        s_control_mode = CONTROL_BUTTONS;
        s_button_angle = 0;
        reset_game();
        s_state = GAME_STATE_COUNTDOWN;
    } else if (s_state == GAME_STATE_PLAYING && s_control_mode == CONTROL_BUTTONS) {
        s_down_pressed = true;
    }
}

static void down_up_handler(ClickRecognizerRef recognizer, void *context) {
    s_down_pressed = false;
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_state == GAME_STATE_GAME_OVER) {
        s_state = GAME_STATE_MENU;
    }
}

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
    window_raw_click_subscribe(BUTTON_ID_UP, up_down_handler, up_up_handler, NULL);
    window_raw_click_subscribe(BUTTON_ID_DOWN, down_down_handler, down_up_handler, NULL);
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

    int target_angle = 0;
    if (s_control_mode == CONTROL_TILT) {
        AccelData accel;
        if (accel_service_peek(&accel) == 0) {
            target_angle = accel.x / 15;
        }
    } else {
        if (s_up_pressed) {
            s_button_angle -= 4;
            if (s_button_angle < -60) s_button_angle = -60;
        } else if (s_down_pressed) {
            s_button_angle += 4;
            if (s_button_angle > 60) s_button_angle = 60;
        } else {
            if (s_button_angle > 0) s_button_angle -= 4;
            if (s_button_angle < 0) s_button_angle += 4;
            if (abs(s_button_angle) < 4) s_button_angle = 0;
        }
        target_angle = s_button_angle;
    }
    
    // Smooth angle interpolation
    s_water_angle = (s_water_angle * 3 + target_angle) / 4; 

    if (s_state == GAME_STATE_MENU) {
        s_water_angle = (s_water_angle * 3) / 4; // level out over time
        s_water_base_y = (SCREEN_H / 2) + (s_water_angle * SY(13)) / 10;
        s_wave_phase = (s_wave_phase + 1000) % TRIG_MAX_ANGLE;
        
        // Cloud Parallax (animate slowly in menu)
        if (s_tick % 5 == 0) s_cloud1_x -= 1;
        if (s_tick % 3 == 0) s_cloud2_x -= 1;
        if (s_cloud1_x < SX(-40)) s_cloud1_x = SCREEN_W + SX(10);
        if (s_cloud2_x < SX(-40)) s_cloud2_x = SCREEN_W + SX(10);
        
        layer_mark_dirty(s_canvas_layer);
        s_timer = app_timer_register(1000 / FPS, game_loop, NULL);
        return;
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

    s_water_base_y = (SCREEN_H / 2) + (s_water_angle * SY(13)) / 10;
    
    int center_wave_angle = (s_wave_phase + (s_boat_x * 300)) % TRIG_MAX_ANGLE;
    int center_wave_height = (sin_lookup(center_wave_angle) * SY(4)) / TRIG_MAX_RATIO;
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
        int smoke_y = s_boat_y - SY(20);
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
    if (s_cloud1_x < SX(-40)) s_cloud1_x = SCREEN_W + SX(10);
    if (s_cloud2_x < SX(-40)) s_cloud2_x = SCREEN_W + SX(10);

    // Parallax Islands Depth
    if (s_tick % 2 == 0) {
        s_island_x -= 1; 
        s_lighthouse_island_x -= 1;
    } 
    if (s_island_x < SX(-80)) {
        s_island_x = SCREEN_W + SX(150) + (rand() % SX(100));
    }
    if (s_lighthouse_island_x < SX(-100)) {
        s_lighthouse_island_x = SCREEN_W + SX(180) + (rand() % SX(100));
    }

    // Obstacle Logic
    s_obstacle_speed = 4 + (s_score / 4);
    if (s_obstacle_speed > 9) s_obstacle_speed = 9;
    
    s_water_distance += s_obstacle_speed;

    int old_obs_x = s_obstacle_x;
    s_obstacle_x -= s_obstacle_speed; 
    
    // Near miss detection when crossing boat
    if (old_obs_x >= s_boat_x && s_obstacle_x < s_boat_x) {
        int dist = abs(s_boat_y - s_obstacle_gap_y);
        int col_hy = SY(25);
        if (dist >= col_hy && dist < col_hy + SY(15)) {
            // Near miss!
            s_sun_cool_timer = 45; // 1.5 seconds at 30 FPS
        }
    }
    
    if (s_obstacle_x < SX(-40)) {
        s_obstacle_x = SCREEN_W + SX(20);
        s_obstacle_gap_y = (rand() % SY(80)) + (SCREEN_H / 2 - SY(40));
        s_score++;
        vibes_short_pulse();
        save_high_score();

        if ((rand() % 100) < 35 && !s_shield_active) {
            s_powerup_active = true;
            s_powerup_x = s_obstacle_x + SX(10);
            s_powerup_y = s_obstacle_gap_y;
        }
    }

    // Power-up Logic
    if (s_powerup_active) {
        s_powerup_x -= s_obstacle_speed;
        int pdx = s_powerup_x - s_boat_x;
        int pdy = s_powerup_y - s_boat_y;
        int pickup_r = SX(22);
        if (pdx * pdx + pdy * pdy < pickup_r * pickup_r) {
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
    if (s_sun_cool_timer > 0) {
        s_sun_cool_timer--;
    }

    // Collision Check
    int col_hx = SX(15);
    int col_hy = SY(25);
    if (s_obstacle_x < s_boat_x + col_hx && s_obstacle_x > s_boat_x - col_hx) {
        if (s_boat_y < s_obstacle_gap_y - col_hy || s_boat_y > s_obstacle_gap_y + col_hy) {
            if (s_shield_active) {
                s_shield_active = false;
                s_obstacle_x = SX(-50);
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

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorPictonBlue);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);

    // Living Sky Layer
    int sun_x = SX(145);
    int sun_y = SY(10);
    
    int dist_to_hazard = s_obstacle_x > s_boat_x ? s_obstacle_x - s_boat_x : s_boat_x - s_obstacle_x;
    bool is_panicking = (dist_to_hazard < SX(50) && (s_state == GAME_STATE_PLAYING || s_state == GAME_STATE_COUNTDOWN));
    
    GBitmap *sun_bmp = s_bmp_sun_normal;
    if (s_sun_cool_timer > 0) {
        sun_bmp = s_bmp_sun_cool;
    } else if (is_panicking) {
        sun_bmp = s_bmp_sun_surprised;
    }
    GRect sun_b = gbitmap_get_bounds(sun_bmp);
    graphics_draw_bitmap_in_rect(ctx, sun_bmp, GRect(sun_x, sun_y, sun_b.size.w, sun_b.size.h));

    // Clouds Parallax
    int c1_pop = abs((sin_lookup((s_tick * 250) % TRIG_MAX_ANGLE) * SY(5)) / TRIG_MAX_RATIO);
    GRect c1_b = gbitmap_get_bounds(s_bmp_cloud_double);
    graphics_draw_bitmap_in_rect(ctx, s_bmp_cloud_double, GRect(s_cloud1_x, SY(25) - c1_pop, c1_b.size.w, c1_b.size.h));
    
    int c2_pop = abs((sin_lookup(((s_tick + 60) * 300) % TRIG_MAX_ANGLE) * SY(4)) / TRIG_MAX_RATIO);
    GRect c2_b = gbitmap_get_bounds(s_bmp_cloud_single);
    graphics_draw_bitmap_in_rect(ctx, s_bmp_cloud_single, GRect(s_cloud2_x, SY(45) - c2_pop, c2_b.size.w, c2_b.size.h));

    // --- Horizon Calculation (Static Y, but tilted) ---
    int bg_angle = (s_water_angle * 7) / 10;
    int static_horizon_y = (SCREEN_H / 2) - SY(25);

    // --- Lighthouse Island Layer ---
    int lh_off = SX(15);
    int lh_horizon_y = static_horizon_y + ((s_lighthouse_island_x + lh_off - (SCREEN_W / 2)) * bg_angle) / 100;
    
    // Draw Rocky Island Base
    int base_x = s_lighthouse_island_x + lh_off;
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    
    GPoint rock_points[] = {
        GPoint(base_x - SX(60), SCREEN_H),
        GPoint(base_x - SX(30), lh_horizon_y + SY(5)),
        GPoint(base_x - SX(20), lh_horizon_y - SY(8)),
        GPoint(base_x - SX(10), lh_horizon_y - SY(4)),
        GPoint(base_x + SX(5),  lh_horizon_y - SY(12)),
        GPoint(base_x + SX(20), lh_horizon_y - SY(5)),
        GPoint(base_x + SX(35), lh_horizon_y + SY(10)),
        GPoint(base_x + SX(65), SCREEN_H)
    };
    GPathInfo rock_info = { .num_points = 8, .points = rock_points };
    GPath *rock_path = gpath_create(&rock_info);
    gpath_draw_filled(ctx, rock_path);
    
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);
    for (int i = 0; i < 7; i++) {
        graphics_draw_line(ctx, rock_points[i], rock_points[i+1]);
    }
    
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(base_x - SX(25), lh_horizon_y + SY(15), SX(5), SY(2)), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(base_x + SX(10), lh_horizon_y + SY(25), SX(4), SY(3)), 0, GCornerNone);
    
    gpath_destroy(rock_path);
    
    // Draw Lighthouse Bitmap
    GRect lh_b = gbitmap_get_bounds(s_bmp_island_lighthouse);
    graphics_draw_bitmap_in_rect(ctx, s_bmp_island_lighthouse, GRect(base_x - lh_b.size.w/2, lh_horizon_y - lh_b.size.h + SY(5), lh_b.size.w, lh_b.size.h));

    // --- Standard Island Layer ---
    GRect island_b = gbitmap_get_bounds(s_bmp_island_coconut);
    int island_horizon_y = static_horizon_y + ((s_island_x - (SCREEN_W / 2)) * bg_angle) / 100;
    
    // Draw dune-shaped base below the island
    int island_bottom_y = island_horizon_y + SY(10);
    int base_top_l = s_island_x + SX(6);
    int base_top_r = s_island_x + island_b.size.w - SX(10);
    
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    GPoint dune_points[] = {
        GPoint(base_top_l - SX(35), SCREEN_H),
        GPoint(base_top_l, island_bottom_y),
        GPoint(base_top_r, island_bottom_y),
        GPoint(base_top_r + SX(45), SCREEN_H)
    };
    GPathInfo dune_info = { .num_points = 4, .points = dune_points };
    GPath *dune_path = gpath_create(&dune_info);
    gpath_draw_filled(ctx, dune_path);
    
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, dune_points[0], dune_points[1]);
    graphics_draw_line(ctx, dune_points[2], dune_points[3]);
    
    graphics_context_set_fill_color(ctx, GColorWindsorTan);
    graphics_fill_rect(ctx, GRect(base_top_l - SX(5), island_bottom_y + SY(15), SX(4), SY(2)), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(base_top_r - SX(20), island_bottom_y + SY(25), SX(6), SY(2)), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(base_top_l + SX(15), island_bottom_y + SY(10), SX(3), SY(2)), 0, GCornerNone);

    gpath_destroy(dune_path);

    graphics_draw_bitmap_in_rect(ctx, s_bmp_island_coconut, GRect(s_island_x, island_horizon_y - island_b.size.h + SY(12), island_b.size.w, island_b.size.h));

    // --- Background Water Layer (3D Depth) ---
    GPoint bg_wave_points[NUM_WAVE_POINTS + 3];
    int step_x = SCREEN_W / (NUM_WAVE_POINTS - 1);
    int bg_base_y = s_water_base_y - SY(12);

    for (int i = 0; i < NUM_WAVE_POINTS; i++) {
        int px = i * step_x;
        int tilt_offset = ((px - (SCREEN_W / 2)) * bg_angle) / 100;
        int wave_angle = (s_wave_phase + (px * 300) + 1000) % TRIG_MAX_ANGLE;
        int wave_height = (sin_lookup(wave_angle) * SY(3)) / TRIG_MAX_RATIO;
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
    int tex_gap = SY(25);
    int tex_seg = SX(12);
    int tex_sp = SX(40);
    for (int row = 0; row < 5; row++) {
        int y_base = s_water_base_y + tex_gap + (row * tex_gap);
        // Subtract distance to move left, wrap around tex_sp
        int offset_x = tex_sp - ((s_water_distance + row * 15) % tex_sp); 
        
        for (int x = -tex_sp; x < SCREEN_W; x += tex_sp) {
            int px = x + offset_x;
            int py = y_base + ((px - (SCREEN_W / 2)) * s_water_angle) / 100; 
            if (px > 0 && px < SCREEN_W && py < SCREEN_H) {
                graphics_draw_line(ctx, GPoint(px, py), GPoint(px + tex_seg, py));
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
    int obs_cx = SX(10);
    int obs_gap = SY(40);
    int chain_seg = SY(10);
    
    // Draw chain base lines
    graphics_context_set_stroke_width(ctx, 4);
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_line(ctx, GPoint(s_obstacle_x + obs_cx, 0), GPoint(s_obstacle_x + obs_cx, s_obstacle_gap_y - obs_gap));
    graphics_draw_line(ctx, GPoint(s_obstacle_x + obs_cx, s_obstacle_gap_y + obs_gap), GPoint(s_obstacle_x + obs_cx, SCREEN_H));

    // Draw chain links
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);
    for (int y = 0; y < s_obstacle_gap_y - obs_gap; y += chain_seg) {
        graphics_draw_circle(ctx, GPoint(s_obstacle_x + obs_cx, y), SX(3));
    }
    for (int y = s_obstacle_gap_y + obs_gap; y < SCREEN_H; y += chain_seg) {
        graphics_draw_circle(ctx, GPoint(s_obstacle_x + obs_cx, y), SX(3));
    }

    // Draw Anchor (Top)
    GRect anchor_b = gbitmap_get_bounds(s_bmp_anchor);
    graphics_draw_bitmap_in_rect(ctx, s_bmp_anchor, GRect(s_obstacle_x + obs_cx - anchor_b.size.w/2, s_obstacle_gap_y - obs_gap - anchor_b.size.h, anchor_b.size.w, anchor_b.size.h));

    // Draw Mine (Bottom)
    GRect mine_b = gbitmap_get_bounds(s_bmp_mine);
    graphics_draw_bitmap_in_rect(ctx, s_bmp_mine, GRect(s_obstacle_x + obs_cx - mine_b.size.w/2, s_obstacle_gap_y + obs_gap, mine_b.size.w, mine_b.size.h));


    // Shield Power-Up
    if (s_powerup_active) {
        int bob_y = s_powerup_y + (sin_lookup((s_tick * 800) % TRIG_MAX_ANGLE) * SY(4)) / TRIG_MAX_RATIO;
        int pu_r = SX(7);
        graphics_context_set_fill_color(ctx, GColorCyan);
        graphics_fill_circle(ctx, GPoint(s_powerup_x, bob_y), pu_r);
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_circle(ctx, GPoint(s_powerup_x, bob_y), pu_r);
    }

    // Particles (Behind Boat)
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (s_particles[i].life > 0) {
            if (s_particles[i].type == PARTICLE_SPLASH) {
                graphics_context_set_fill_color(ctx, GColorWhite);
                graphics_fill_circle(ctx, GPoint(s_particles[i].x, s_particles[i].y), SX(2));
            } else if (s_particles[i].type == PARTICLE_SMOKE) {
                graphics_context_set_fill_color(ctx, GColorDarkGray);
                int radius = SX(1 + ((s_particles[i].max_life - s_particles[i].life) / 4));
                graphics_fill_circle(ctx, GPoint(s_particles[i].x, s_particles[i].y), radius);
            }
        }
    }

    // Shield Aura
    if (s_shield_active) {
        GColor shield_color = (s_shield_timer < 60 && (s_tick % 6 < 3)) ? GColorBrightGreen : GColorElectricBlue;
        graphics_context_set_stroke_color(ctx, shield_color);
        graphics_context_set_stroke_width(ctx, 3);
        int shield_r = SX(24) + (sin_lookup((s_tick * 600) % TRIG_MAX_ANGLE) * SX(2)) / TRIG_MAX_RATIO;
        graphics_draw_circle(ctx, GPoint(s_boat_x, s_boat_y - SY(10)), shield_r);
    }

    // Boat Sprite
    GRect boat_b = gbitmap_get_bounds(s_bmp_boat);
    graphics_draw_bitmap_in_rect(ctx, s_bmp_boat, GRect(s_boat_x - boat_b.size.w/2, s_boat_y - boat_b.size.h + SY(10), boat_b.size.w, boat_b.size.h));

    // Trajectory Dots (In front of boat)
    if (s_state == GAME_STATE_PLAYING) {
        int dot_sp = SX(12);
        graphics_context_set_fill_color(ctx, GColorYellow);
        for (int i = 1; i <= 6; i++) {
            int dot_x = s_boat_x + (i * dot_sp);
            if (dot_x < SCREEN_W) {
                int tilt_offset = ((dot_x - (SCREEN_W / 2)) * s_water_angle) / 100;
                int wave_angle = (s_wave_phase + (dot_x * 300)) % TRIG_MAX_ANGLE;
                int wave_height = (sin_lookup(wave_angle) * SY(4)) / TRIG_MAX_RATIO;
                int dot_y = s_water_base_y + tilt_offset + wave_height - SY(3);
                
                graphics_fill_circle(ctx, GPoint(dot_x, dot_y), 2);
            }
        }
    }

    // Score Display
    char score_buffer[32];
    snprintf(score_buffer, sizeof(score_buffer), "Score: %d", s_score);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, score_buffer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                       GRect(0, SCREEN_H - SY(30), SCREEN_W, SY(30)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // Menu Overlay
    if (s_state == GAME_STATE_MENU) {
        int box_m = SX(15);
        int box_h = SY(100);
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_rect(ctx, GRect(box_m, SCREEN_H / 2 - box_h / 2, SCREEN_W - box_m * 2, box_h), 6, GCornersAll);
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_rect(ctx, GRect(box_m, SCREEN_H / 2 - box_h / 2, SCREEN_W - box_m * 2, box_h));

        char *menu_text = "POINT NEMO\n\nUP: Tilt\nDOWN: Buttons";
        
        graphics_context_set_text_color(ctx, GColorBlack);
        graphics_draw_text(ctx, menu_text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 
                           GRect(box_m, SCREEN_H / 2 - box_h / 2 + SY(7), SCREEN_W - box_m * 2, box_h - SY(10)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // Countdown Overlay
    if (s_state == GAME_STATE_COUNTDOWN) {
        int count = (s_countdown_timer / 30) + 1;
        char count_buf[16];
        snprintf(count_buf, sizeof(count_buf), "%d", count);
        
        graphics_context_set_text_color(ctx, GColorYellow);
        graphics_draw_text(ctx, count_buf, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), 
                           GRect(0, SCREEN_H / 2 - SY(40), SCREEN_W, SY(50)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // Game Over Overlay
    if (s_state == GAME_STATE_GAME_OVER) {
        int box_m = SX(15);
        int box_h = SY(100);
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_rect(ctx, GRect(box_m, SCREEN_H / 2 - box_h / 2, SCREEN_W - box_m * 2, box_h), 6, GCornersAll);
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_rect(ctx, GRect(box_m, SCREEN_H / 2 - box_h / 2, SCREEN_W - box_m * 2, box_h));

        char over_buf[64];
        snprintf(over_buf, sizeof(over_buf), "SUNK!\nScore: %d | Best: %d\nSELECT -> Menu", s_score, s_high_score);
        
        graphics_context_set_text_color(ctx, GColorBlack);
        graphics_draw_text(ctx, over_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 
                           GRect(box_m, SCREEN_H / 2 - box_h / 2 + SY(7), SCREEN_W - box_m * 2, box_h - SY(10)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
}

static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    // Detect actual screen dimensions at runtime
    SCREEN_W = bounds.size.w;
    SCREEN_H = bounds.size.h;

    load_high_score();
    reset_game();

    s_bmp_boat = gbitmap_create_with_resource(RESOURCE_ID_BOAT);
    s_bmp_anchor = gbitmap_create_with_resource(RESOURCE_ID_ANCHOR);
    s_bmp_mine = gbitmap_create_with_resource(RESOURCE_ID_MINE);
    s_bmp_sun_normal = gbitmap_create_with_resource(RESOURCE_ID_SUN_NORMAL);
    s_bmp_sun_cool = gbitmap_create_with_resource(RESOURCE_ID_SUN_COOL);
    s_bmp_sun_surprised = gbitmap_create_with_resource(RESOURCE_ID_SUN_SURPRISED);
    s_bmp_cloud_single = gbitmap_create_with_resource(RESOURCE_ID_CLOUD_SINGLE);
    s_bmp_cloud_double = gbitmap_create_with_resource(RESOURCE_ID_CLOUD_DOUBLE);
    s_bmp_island_coconut = gbitmap_create_with_resource(RESOURCE_ID_ISLAND_COCONUT);
    s_bmp_island_lighthouse = gbitmap_create_with_resource(RESOURCE_ID_ISLAND_LIGHTHOUSE);

    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    srand(time(NULL));
    s_timer = app_timer_register(1000 / FPS, game_loop, NULL);
}

static void main_window_unload(Window *window) {
    gbitmap_destroy(s_bmp_boat);
    gbitmap_destroy(s_bmp_anchor);
    gbitmap_destroy(s_bmp_mine);
    gbitmap_destroy(s_bmp_sun_normal);
    gbitmap_destroy(s_bmp_sun_cool);
    gbitmap_destroy(s_bmp_sun_surprised);
    gbitmap_destroy(s_bmp_cloud_single);
    gbitmap_destroy(s_bmp_cloud_double);
    gbitmap_destroy(s_bmp_island_coconut);
    gbitmap_destroy(s_bmp_island_lighthouse);

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
