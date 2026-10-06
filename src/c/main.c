#include <pebble.h>
#include "globals.h"
#include "game.h"

static Window *s_main_window;

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
    s_bmp_tower = gbitmap_create_with_resource(RESOURCE_ID_TOWER);

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
    gbitmap_destroy(s_bmp_tower);

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

static void prv_update_app_glance(AppGlanceReloadSession *session, size_t limit, void *context) {
    if (limit < 1) return;
    
    int hs = persist_exists(PERSIST_KEY_HIGH_SCORE) ? persist_read_int(PERSIST_KEY_HIGH_SCORE) : 0;
    static char subtitle[32];
    snprintf(subtitle, sizeof(subtitle), "Best Score: %d", hs);
    
    AppGlanceSlice entry = (AppGlanceSlice) {
        .layout = {
            .icon = APP_GLANCE_SLICE_DEFAULT_ICON,
            .subtitle_template_string = subtitle
        },
        .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION
    };
    app_glance_add_slice(session, entry);
}

static void deinit(void) {
    window_destroy(s_main_window);
    
#if PBL_API_EXISTS(app_glance_reload)
    app_glance_reload(prv_update_app_glance, NULL);
#endif
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
