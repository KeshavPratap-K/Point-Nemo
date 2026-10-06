#include "game.h"
#include "globals.h"
#include "settings.h"

#define NUM_DEATH_TEXTS 8
static const char *s_death_texts[NUM_DEATH_TEXTS] = {
    "FISH FOOD!",
    "WRECKED!",
    "GLUB GLUB!",
    "CRASHED!",
    "CAPSIZED!",
    "DESTROYED!",
    "SUNK!",
    "SPLAT!"
};
static const char *s_hit_message = "";
static int s_menu_selection = 0; // 0 = Tilt, 1 = Buttons

void load_high_score(void) {
    if (persist_exists(PERSIST_KEY_HIGH_SCORE)) {
        s_high_score = persist_read_int(PERSIST_KEY_HIGH_SCORE);
    } else {
        s_high_score = 0;
    }
    if (persist_exists(PERSIST_KEY_LAST_SCORE)) {
        s_last_death_score = persist_read_int(PERSIST_KEY_LAST_SCORE);
    } else {
        s_last_death_score = -1;
    }
}

static void save_high_score(void) {
    if (s_score > s_high_score) {
        s_high_score = s_score;
        persist_write_int(PERSIST_KEY_HIGH_SCORE, s_high_score);
    }
}

void reset_game(void) {
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
    s_sink_timer = 0;
    s_sink_angle = 0;
    s_sink_y_offset = 0;
    s_shake_offset_x = 0;
    s_shake_offset_y = 0;
    
    // Ghost buoy: will appear when the player passes the score where they last died
    s_ghost_buoy_active = false;
    s_ghost_buoy_x = -100;
    
    for (int i = 0; i < MAX_PARTICLES; i++) {
        s_particles[i].life = 0;
    }
}



static void select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
    settings_window_push();
}

static void up_down_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_state == GAME_STATE_MODE_SELECT) {
        s_menu_selection = 0; // highlight Tilt
    } else if (s_state == GAME_STATE_PLAYING && s_control_mode == CONTROL_BUTTONS) {
        s_up_pressed = true;
    }
}

static void up_up_handler(ClickRecognizerRef recognizer, void *context) {
    s_up_pressed = false;
}

static void down_down_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_state == GAME_STATE_MODE_SELECT) {
        s_menu_selection = 1; // highlight Buttons
    } else if (s_state == GAME_STATE_PLAYING && s_control_mode == CONTROL_BUTTONS) {
        s_down_pressed = true;
    }
}

static void down_up_handler(ClickRecognizerRef recognizer, void *context) {
    s_down_pressed = false;
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_state == GAME_STATE_MENU) {
        s_state = GAME_STATE_MODE_SELECT;
        s_menu_selection = 0;
    } else if (s_state == GAME_STATE_MODE_SELECT) {
        // Confirm the highlighted selection
        if (s_menu_selection == 0) {
            s_control_mode = CONTROL_TILT;
        } else {
            s_control_mode = CONTROL_BUTTONS;
            s_button_angle = 0;
        }
        reset_game();
        s_state = GAME_STATE_COUNTDOWN;
    } else if (s_state == GAME_STATE_GAME_OVER) {
        s_state = GAME_STATE_MENU;
        s_timer = app_timer_register(1000 / FPS, game_loop, NULL);
    }
}

void click_config_provider(void *context) {
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

void game_loop(void *data) {
    s_tick++;

    int target_angle = 0;
    if (s_state != GAME_STATE_MENU && s_state != GAME_STATE_MODE_SELECT) {
        if (s_control_mode == CONTROL_TILT) {
            AccelData accel;
            if (accel_service_peek(&accel) == 0) {
                // Use vertical tilt (Y-axis) so it works while looking at the wrist
                // Natural viewing angle is usually around Y = -600
                int baseline_y = -600;
                target_angle = (accel.y - baseline_y) / 15;
                
                // Clamp target_angle to prevent excessive rotation
                if (target_angle > 60) target_angle = 60;
                if (target_angle < -60) target_angle = -60;
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
    }
    
    // Smooth angle interpolation
    s_water_angle = (s_water_angle * 3 + target_angle) / 4; 

    if (s_state == GAME_STATE_MENU || s_state == GAME_STATE_MODE_SELECT) {
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

    if (s_state == GAME_STATE_SINKING) {
        s_sink_timer--;
        s_sink_angle += 3; // Boat tips over
        s_sink_y_offset += 2; // Boat sinks into water
        
        // Screen shake only on initial impact (first 15 frames of 60)
        if (s_sink_timer > 45) {
            s_shake_offset_x = (rand() % 9) - 4;
            s_shake_offset_y = (rand() % 9) - 4;
        } else {
            s_shake_offset_x = 0;
            s_shake_offset_y = 0;
        }
        
        // Keep waves moving
        s_wave_phase = (s_wave_phase + 1000) % TRIG_MAX_ANGLE;
        
        if (s_sink_timer <= 0) {
            s_state = GAME_STATE_GAME_OVER;
            s_shake_offset_x = 0;
            s_shake_offset_y = 0;
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

        // Ghost buoy: spawn when player reaches the score where they last died
        if (s_last_death_score >= 0 && s_score == s_last_death_score && !s_ghost_buoy_active) {
            s_ghost_buoy_active = true;
            // Spawn a little before the anchor/mine obstacle
            s_ghost_buoy_x = s_obstacle_x - SX(50);
        }

        if ((rand() % 100) < 35 && !s_shield_active) {
            s_powerup_active = true;
            s_powerup_x = s_obstacle_x + SX(10);
            s_powerup_y = s_obstacle_gap_y;
        }
    }

    // Ghost Buoy Logic
    if (s_ghost_buoy_active) {
        s_ghost_buoy_x -= s_obstacle_speed;
        if (s_ghost_buoy_x < SX(-40)) {
            s_ghost_buoy_active = false;
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
                // Start sinking animation
                s_state = GAME_STATE_SINKING;
                s_sink_timer = 60; // 2 seconds at 30 FPS
                s_sink_angle = 0;
                s_sink_y_offset = 0;
                s_death_text_idx = rand() % NUM_DEATH_TEXTS;
                
                // Determine hit reason
                if (s_boat_y < s_obstacle_gap_y - col_hy) {
                    // Hit top obstacle (Anchor)
                    if (s_water_angle < -15) s_hit_message = "Over corrected!";
                    else if (rand() % 3 == 0) s_hit_message = "Watch out!";
                    else s_hit_message = "Too high!";
                } else {
                    // Hit bottom obstacle (Mine)
                    if (s_water_angle > 15) s_hit_message = "Over corrected!";
                    else if (rand() % 3 == 0) s_hit_message = "Watch out!";
                    else s_hit_message = "Too low!";
                }
                
                vibes_double_pulse();
                save_high_score();
                // Save death score for ghost buoy
                persist_write_int(PERSIST_KEY_LAST_SCORE, s_score);
                s_last_death_score = s_score;
            }
        }
    }

    layer_mark_dirty(s_canvas_layer);
    s_timer = app_timer_register(1000 / FPS, game_loop, NULL);
}

void canvas_update_proc(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorPictonBlue);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    
    // shake offsets (sx, sy) applied to key elements during sinking
    int sx = s_shake_offset_x;
    int sy = s_shake_offset_y;

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

    // --- Horizon Calculation ---
    int bg_angle = (s_water_angle * 7) / 10;
    int static_horizon_y = (SCREEN_H / 2) - SY(25);

    // --- Static Beach Horizon Strip ---
    // A sandy strip that spans the full width, sitting at the static horizon.
    // Islands rest on this, so they never float in empty sky.
    int beach_top = static_horizon_y - SY(5);
    // Extend the beach all the way to the bottom to avoid showing sky when water tilts down
    int beach_bottom = SCREEN_H;
    int beach_band_y = static_horizon_y + SY(18);
    
    // Main sand fill
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    graphics_fill_rect(ctx, GRect(0, beach_top, SCREEN_W, beach_bottom - beach_top), 0, GCornerNone);
    
    // Darker sand band at bottom edge (near water level) for depth
    graphics_context_set_fill_color(ctx, GColorWindsorTan);
    graphics_fill_rect(ctx, GRect(0, beach_band_y - SY(5), SCREEN_W, SY(5)), 0, GCornerNone);
    
    // Sand texture dots
    graphics_context_set_fill_color(ctx, GColorRajah);
    graphics_fill_rect(ctx, GRect(SX(12), beach_top + SY(4), SX(3), SY(2)), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(SX(55), beach_top + SY(8), SX(4), SY(2)), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(SX(95), beach_top + SY(3), SX(3), SY(2)), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(SX(140), beach_top + SY(6), SX(4), SY(2)), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(SX(170), beach_top + SY(10), SX(3), SY(2)), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(SX(35), beach_top + SY(12), SX(3), SY(1)), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(SX(120), beach_top + SY(11), SX(3), SY(1)), 0, GCornerNone);

    // --- Lighthouse Island (sits on beach) ---
    int lh_off = SX(15);
    int base_x = s_lighthouse_island_x + lh_off;
    GRect lh_b = gbitmap_get_bounds(s_bmp_island_lighthouse);
    // Bottom of sprite aligns with the beach surface
    int lh_draw_y = beach_top - lh_b.size.h + SY(6);
    graphics_draw_bitmap_in_rect(ctx, s_bmp_island_lighthouse, GRect(base_x - lh_b.size.w/2, lh_draw_y, lh_b.size.w, lh_b.size.h));

    // --- Coconut Island (sits on beach) ---
    GRect island_b = gbitmap_get_bounds(s_bmp_island_coconut);
    int coco_draw_y = beach_top - island_b.size.h + SY(6);
    graphics_draw_bitmap_in_rect(ctx, s_bmp_island_coconut, GRect(s_island_x, coco_draw_y, island_b.size.w, island_b.size.h));

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

    // Particles
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
    // Ghost Buoy (Point Nemo Marker)
    if (s_ghost_buoy_active) {
        int buoy_bob = (sin_lookup((s_tick * 600) % TRIG_MAX_ANGLE) * SY(3)) / TRIG_MAX_RATIO;
        int buoy_y = s_water_base_y + buoy_bob;
        int bx = s_ghost_buoy_x;
        
        // Draw Tower Bitmap
        GRect tower_b = gbitmap_get_bounds(s_bmp_tower);
        graphics_draw_bitmap_in_rect(ctx, s_bmp_tower, GRect(bx - tower_b.size.w/2, buoy_y - tower_b.size.h + SY(5), tower_b.size.w, tower_b.size.h));
        
        // Label
        graphics_context_set_text_color(ctx, GColorRed);
        graphics_draw_text(ctx, "RIP", fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(bx - SX(15), buoy_y - tower_b.size.h - SY(10), SX(30), SY(14)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // Boat Sprite (with sinking animation)
    GRect boat_b = gbitmap_get_bounds(s_bmp_boat);
    int draw_boat_x = s_boat_x - boat_b.size.w/2 + sx;
    int draw_boat_y = s_boat_y - boat_b.size.h + SY(10) + sy + s_sink_y_offset;
    graphics_draw_bitmap_in_rect(ctx, s_bmp_boat, GRect(draw_boat_x, draw_boat_y, boat_b.size.w, boat_b.size.h));
    
    // Funny text above boat during sinking
    if (s_state == GAME_STATE_SINKING && s_sink_timer > 20) {
        graphics_context_set_text_color(ctx, GColorSunsetOrange);
        graphics_draw_text(ctx, s_hit_message, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                           GRect(draw_boat_x - SX(25), draw_boat_y - SY(25), boat_b.size.w + SX(50), SY(20)), 
                           GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

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
    if (s_state != GAME_STATE_GAME_OVER) {
        char score_buffer[32];
        snprintf(score_buffer, sizeof(score_buffer), "Score: %d", s_score);
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, score_buffer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                           GRect(0, SCREEN_H - SY(30), SCREEN_W, SY(30)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // Title Screen Overlay
    if (s_state == GAME_STATE_MENU) {
        // "POINT NEMO" centered bold title
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, "POINT NEMO", fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK), 
                           GRect(0, SCREEN_H / 2 - SY(65), SCREEN_W, SY(70)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        
        // "Press to Start" text below, with enough gap
        graphics_context_set_text_color(ctx, GColorYellow);
        graphics_draw_text(ctx, "Press to Start", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 
                           GRect(0, SCREEN_H / 2 + SY(15), SCREEN_W, SY(25)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
    
    // Mode Select Overlay
    if (s_state == GAME_STATE_MODE_SELECT) {
        // Heading
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, "Controls", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                           GRect(0, SCREEN_H / 2 - SY(70), SCREEN_W, SY(30)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        
        // Centered wide buttons
        int btn_w = SCREEN_W - SX(40);
        int btn_h = SY(32);
        int btn_x = (SCREEN_W - btn_w) / 2;
        int btn_tilt_y = SCREEN_H / 2 - SY(34);
        int btn_btns_y = SCREEN_H / 2 + SY(12);
        
        // Tilt button
        bool tilt_sel = (s_menu_selection == 0);
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(btn_x - 2, btn_tilt_y - 2, btn_w + 4, btn_h + 4), 10, GCornersAll);
        graphics_context_set_fill_color(ctx, tilt_sel ? GColorVividCerulean : GColorDarkGray);
        graphics_fill_rect(ctx, GRect(btn_x, btn_tilt_y, btn_w, btn_h), 10, GCornersAll);
        if (tilt_sel) {
            // Highlight border
            graphics_context_set_stroke_color(ctx, GColorWhite);
            graphics_context_set_stroke_width(ctx, 3);
            graphics_draw_round_rect(ctx, GRect(btn_x, btn_tilt_y, btn_w, btn_h), 10);
        }
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, "Tilt", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                           GRect(btn_x, btn_tilt_y + SY(2), btn_w, btn_h), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        
        // Buttons button
        bool btns_sel = (s_menu_selection == 1);
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(btn_x - 2, btn_btns_y - 2, btn_w + 4, btn_h + 4), 10, GCornersAll);
        graphics_context_set_fill_color(ctx, btns_sel ? GColorOrange : GColorDarkGray);
        graphics_fill_rect(ctx, GRect(btn_x, btn_btns_y, btn_w, btn_h), 10, GCornersAll);
        if (btns_sel) {
            // Highlight border
            graphics_context_set_stroke_color(ctx, GColorWhite);
            graphics_context_set_stroke_width(ctx, 3);
            graphics_draw_round_rect(ctx, GRect(btn_x, btn_btns_y, btn_w, btn_h), 10);
        }
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, "Buttons", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                           GRect(btn_x, btn_btns_y + SY(2), btn_w, btn_h), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // Countdown Overlay (Static text at top)
    if (s_state == GAME_STATE_COUNTDOWN) {
        int count = (s_countdown_timer / 30) + 1; // 3, 2, 1
        char count_buf[16];
        snprintf(count_buf, sizeof(count_buf), "%d", count);
        
        graphics_context_set_text_color(ctx, GColorYellow);
        graphics_draw_text(ctx, count_buf, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), 
                           GRect(0, SY(10), SCREEN_W, SY(50)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // Iris Wipe (Cartoon circle closing)
    int iris_radius = 200; // Fully open
    if (s_state == GAME_STATE_SINKING) {
        if (s_sink_timer < 40) {
            iris_radius = (s_sink_timer * 150) / 40;
        }
    } else if (s_state == GAME_STATE_GAME_OVER) {
        iris_radius = 0;
    }

    if (iris_radius < 200) {
        int max_r = 250;
        int thickness = max_r - iris_radius;
        if (thickness > 0) {
            graphics_context_set_stroke_color(ctx, GColorBlack);
            graphics_context_set_stroke_width(ctx, thickness);
            graphics_draw_circle(ctx, GPoint(SCREEN_W / 2, SCREEN_H / 2), iris_radius + thickness / 2);
        }
    }

    // Game Over Overlay
    if (s_state == GAME_STATE_GAME_OVER) {
        // Expand the box to almost the full width to fit text
        int box_m = SX(4);
        int box_w = SCREEN_W - (box_m * 2);
        int box_h = SY(110);
        int box_x = box_m;
        int box_y = (SCREEN_H / 2) - (box_h / 2);
        
        // Background and Border
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(box_x - 2, box_y - 2, box_w + 4, box_h + 4), 8, GCornersAll);
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_rect(ctx, GRect(box_x, box_y, box_w, box_h), 8, GCornersAll);
        
        // Death Text (Big, Light Red, New Font)
        graphics_context_set_text_color(ctx, GColorSunsetOrange);
        graphics_draw_text(ctx, s_death_texts[s_death_text_idx], fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), 
                           GRect(box_x, box_y + SY(2), box_w, SY(36)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
                           
        // Score (Medium, Blue)
        graphics_context_set_text_color(ctx, GColorCobaltBlue);
        char score_buf[32];
        snprintf(score_buf, sizeof(score_buf), "Score: %d", s_score);
        graphics_draw_text(ctx, score_buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                           GRect(box_x, box_y + SY(37), box_w, SY(28)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
                           
        // Best (Small, Dark Gray)
        graphics_context_set_text_color(ctx, GColorDarkGray);
        char best_buf[32];
        snprintf(best_buf, sizeof(best_buf), "Best: %d", s_high_score);
        graphics_draw_text(ctx, best_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 
                           GRect(box_x, box_y + SY(62), box_w, SY(24)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
                           
        // Restart (Smallest, Light Gray)
        graphics_context_set_text_color(ctx, GColorDarkGray);
        graphics_draw_text(ctx, "Press SELECT to restart", fonts_get_system_font(FONT_KEY_GOTHIC_14), 
                           GRect(box_x, box_y + SY(87), box_w, SY(20)), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
}

