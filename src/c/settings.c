#include "settings.h"
#include "globals.h"

static Window *s_settings_window;
static SimpleMenuLayer *s_simple_menu_layer;
static SimpleMenuSection s_menu_sections[1];
static SimpleMenuItem s_menu_items[1];

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

void settings_window_push(void) {
    if (!s_settings_window) {
        s_settings_window = window_create();
        window_set_window_handlers(s_settings_window, (WindowHandlers) {
            .load = settings_window_load,
            .unload = settings_window_unload,
        });
    }
    window_stack_push(s_settings_window, true);
}
