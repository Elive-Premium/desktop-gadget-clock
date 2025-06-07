/**
 * @file main.c
 * @brief Elive Clock - A beautiful desktop clock application
 * @author Elive Team
 * @date 2024
 * @version 1.0
 *
 * This application displays the current time and date on the desktop.
 * It can run as a normal window or as a desktop gadget, supports
 * dragging to reposition, and saves user preferences.
 */

#define _GNU_SOURCE
#include <Elementary.h>
#include <Ecore_X.h>
#include <Eet.h>
#include <time.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define CONFIG_VERSION 1
#define WINDOW_WIDTH 300
#define WINDOW_HEIGHT 120
#define TIMER_INTERVAL_SECONDS 1.0
#define TIMER_INTERVAL_MINUTES 60.0

/**
 * @brief Configuration data structure for persistent settings
 */
typedef struct _Config {
    Eina_Bool show_date;
    int version;
    Eina_Bool utc_mode; // Added: Flag to store UTC mode preference
} Config;

/**
 * @brief Application data structure
 */
typedef struct _App_Data {
    /* Window and UI elements */
    Evas_Object *win;
    Evas_Object *layout;
    Ecore_Timer *timer;

    /* Configuration */
    Config *config;
    char *config_file;

    /* Application state */
    Eina_Bool debug;
    Eina_Bool normal_window;
    Eina_Bool show_seconds;
    Eina_Bool show_date;
    Eina_Bool utc_mode; // Added: Flag to track current UTC display mode

    /* Dragging state */
    Eina_Bool dragging;
    int drag_start_x;
    int drag_start_y;
    int win_start_x;
    int win_start_y;
} App_Data;

/* Function prototypes */
static Eina_Bool _timer_cb(void *data);
static void _config_save(App_Data *ad);
static Config *_config_load(App_Data *ad);
static void _config_init(App_Data *ad);
static void _config_shutdown(App_Data *ad);
static Eet_Data_Descriptor *_config_descriptor_new(void);
static void _utc_toggle_cb(void *data, Evas_Object *obj, const char *emission, const char *source);

/**
 * @brief Creates EET data descriptor for configuration
 */
static Eet_Data_Descriptor *
_config_descriptor_new(void)
{
    Eet_Data_Descriptor_Class eddc;
    Eet_Data_Descriptor *edd;

    EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Config);
    edd = eet_data_descriptor_stream_new(&eddc);

    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Config, "show_date", show_date, EET_T_UCHAR);
    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Config, "version", version, EET_T_INT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Config, "utc_mode", utc_mode, EET_T_UCHAR); // Added: UTC mode to config

    return edd;
}

/**
 * @brief Initializes the configuration system
 */
static void
_config_init(App_Data *ad)
{
    char config_dir[PATH_MAX];
    const char *home;

    home = getenv("HOME");
    if (!home) home = "/tmp";

    snprintf(config_dir, sizeof(config_dir), "%s/.config/elive-clock", home);

    if (mkdir(config_dir, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "Warning: Failed to create config directory: %s\n", strerror(errno));
    }

    ad->config_file = malloc(PATH_MAX);
    snprintf(ad->config_file, PATH_MAX, "%s/config.eet", config_dir);

    ad->config = _config_load(ad);
    if (!ad->config) {
        ad->config = calloc(1, sizeof(Config));
        ad->config->show_date = EINA_TRUE;
        ad->config->version = CONFIG_VERSION;
        ad->config->utc_mode = EINA_FALSE; // Default to local time
        _config_save(ad);
    }

    ad->show_date = ad->config->show_date;
    ad->utc_mode = ad->config->utc_mode; // Load UTC mode from config
}

/**
 * @brief Shuts down the configuration system
 */
static void
_config_shutdown(App_Data *ad)
{
    if (ad->config) {
        _config_save(ad);
        free(ad->config);
        ad->config = NULL;
    }

    if (ad->config_file) {
        free(ad->config_file);
        ad->config_file = NULL;
    }
}

/**
 * @brief Loads configuration from file
 */
static Config *
_config_load(App_Data *ad)
{
    Eet_File *ef;
    Config *config = NULL;
    Eet_Data_Descriptor *edd;

    ef = eet_open(ad->config_file, EET_FILE_MODE_READ);
    if (!ef) return NULL;

    edd = _config_descriptor_new();
    config = eet_data_read(ef, edd, "config");
    eet_data_descriptor_free(edd);
    eet_close(ef);

    return config;
}

/**
 * @brief Saves configuration to file
 */
static void
_config_save(App_Data *ad)
{
    Eet_File *ef;
    Eet_Data_Descriptor *edd;

    if (!ad->config) return;

    ad->config->show_date = ad->show_date;
    ad->config->utc_mode = ad->utc_mode; // Save UTC mode

    ef = eet_open(ad->config_file, EET_FILE_MODE_WRITE);
    if (!ef) {
        fprintf(stderr, "Warning: Could not save configuration\n");
        return;
    }

    edd = _config_descriptor_new();
    eet_data_write(ef, edd, "config", ad->config, EINA_TRUE);
    eet_data_descriptor_free(edd);
    eet_close(ef);
}

/**
 * @brief Close button callback
 */
static void
_close_cb(void *data, Evas_Object *obj EINA_UNUSED,
          const char *emission EINA_UNUSED, const char *source EINA_UNUSED)
{
    ecore_main_loop_quit();
}

/**
 * @brief Callback for toggling UTC mode
 */
static void
_utc_toggle_cb(void *data, Evas_Object *obj EINA_UNUSED,
               const char *emission EINA_UNUSED, const char *source EINA_UNUSED)
{
    App_Data *ad = data;
    ad->utc_mode = !ad->utc_mode;
    _config_save(ad); // Save the new UTC mode preference
    _timer_cb(ad); // Immediately update the display
}

/**
 * @brief Date click callback - toggles date visibility (now unused, but kept for reference if needed)
 */
static void
_date_click_cb(void *data, Evas_Object *obj EINA_UNUSED,
               const char *emission EINA_UNUSED, const char *source EINA_UNUSED)
{
    App_Data *ad = data;

    ad->show_date = !ad->show_date;

    elm_layout_signal_emit(obj, ad->show_date ? "date,show" : "date,hide", "elm");
    _config_save(ad);
    _timer_cb(ad);
}

/**
 * @brief Timer callback - updates time and date display
 */
static Eina_Bool
_timer_cb(void *data)
{
    App_Data *ad = data;
    time_t rawtime;
    struct tm timeinfo_buf; // Buffer for reentrant time functions
    struct tm *timeinfo;
    char time_str[32];
    char date_str[64];

    time(&rawtime);
    if (ad->utc_mode) {
        timeinfo = gmtime_r(&rawtime, &timeinfo_buf); // Use UTC time
    } else {
        timeinfo = localtime_r(&rawtime, &timeinfo_buf); // Use local time
    }

    strftime(time_str, sizeof(time_str),
             ad->show_seconds ? "%H:%M:%S" : "%H:%M", timeinfo);
    strftime(date_str, sizeof(date_str), "%A, %B %d, %Y", timeinfo);

    edje_object_part_text_set(elm_layout_edje_get(ad->layout), "time_text", time_str);
    edje_object_part_text_set(elm_layout_edje_get(ad->layout), "date_text", date_str);
    // Set UTC indicator text
    edje_object_part_text_set(elm_layout_edje_get(ad->layout), "utc_indicator_text",
                              ad->utc_mode ? "UTC" : "");

    return ECORE_CALLBACK_RENEW;
}

/**
 * @brief Calculates interval until next minute
 */
static double
_get_next_timer_interval(Eina_Bool show_seconds)
{
    if (show_seconds) return TIMER_INTERVAL_SECONDS;

    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    return (double)(60 - timeinfo->tm_sec);
}

/**
 * @brief Window delete callback
 */
static void
_win_del_cb(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
    App_Data *ad = data;

    if (ad->timer) ecore_timer_del(ad->timer);
    _config_shutdown(ad);
    ecore_main_loop_quit();
}

/**
 * @brief Mouse down callback - starts dragging
 */
static void
_mouse_down_cb(void *data, Evas *e EINA_UNUSED,
               Evas_Object *obj EINA_UNUSED, void *event_info)
{
    App_Data *ad = data;
    Evas_Event_Mouse_Down *ev = event_info;

    if (ev->button != 1) return;

    Ecore_X_Window xwin = elm_win_xwindow_get(ad->win);
    if (!xwin) return;

    ad->dragging = EINA_TRUE;

    Ecore_X_Window root = ecore_x_window_root_get(xwin);
    ecore_x_pointer_xy_get(root, &ad->drag_start_x, &ad->drag_start_y);
    evas_object_geometry_get(ad->win, &ad->win_start_x, &ad->win_start_y, NULL, NULL);
    ecore_x_pointer_grab(xwin);
}

/**
 * @brief Mouse up callback - ends dragging (UTC toggle moved to EDC signal)
 */
static void
_mouse_up_cb(void *data, Evas *e EINA_UNUSED,
             Evas_Object *obj EINA_UNUSED, void *event_info)
{
    App_Data *ad = data;
    Evas_Event_Mouse_Up *ev = event_info;

    if (ev->button != 1) return;

    if (ad->dragging) {
        ecore_x_pointer_ungrab();
        ad->dragging = EINA_FALSE;

        Ecore_X_Window xwin = elm_win_xwindow_get(ad->win);
        if (xwin) {
            int x, y;
            Ecore_X_Window root = ecore_x_window_root_get(xwin);
            ecore_x_pointer_xy_get(root, &x, &y);

            int final_x = ad->win_start_x + (x - ad->drag_start_x);
            int final_y = ad->win_start_y + (y - ad->drag_start_y);

            evas_object_move(ad->win, final_x, final_y);
        }
    }
    // UTC toggle logic removed from here, now handled by _utc_toggle_cb via EDC signal
}

/**
 * @brief Mouse move callback - handles dragging
 */
static void
_mouse_move_cb(void *data, Evas *e EINA_UNUSED,
               Evas_Object *obj EINA_UNUSED, void *event_info)
{
    App_Data *ad = data;
    Evas_Event_Mouse_Move *ev = event_info;

    if (!ad->dragging || ev->buttons != 1) return;

    Ecore_X_Window xwin = elm_win_xwindow_get(ad->win);
    if (!xwin) return;

    int x, y;
    Ecore_X_Window root = ecore_x_window_root_get(xwin);
    ecore_x_pointer_xy_get(root, &x, &y);

    int new_x = ad->win_start_x + (x - ad->drag_start_x);
    int new_y = ad->win_start_y + (y - ad->drag_start_y);

    ecore_x_window_move(xwin, new_x, new_y);
    evas_object_move(ad->win, new_x, new_y);
}

/**
 * @brief Prints help message
 */
static void
_print_help(const char *prog_name)
{
    printf("Elive Clock v1.0 - A beautiful desktop clock\n\n");
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("Options:\n");
    printf("  --debug    Enable debug output\n");
    printf("  --normal   Create a normal window (not a desktop gadget)\n");
    printf("  --seconds  Show seconds in the time display\n");
    printf("  --help     Show this help message\n\n");
}

/**
 * @brief One-shot timer for minute synchronization
 */
static Eina_Bool
_minute_timer_cb(void *data)
{
    App_Data *ad = data;

    _timer_cb(ad);

    if (ad->timer) ecore_timer_del(ad->timer);
    ad->timer = ecore_timer_add(TIMER_INTERVAL_MINUTES, _timer_cb, ad);

    return ECORE_CALLBACK_CANCEL;
}

/**
 * @brief Main entry point
 */
EAPI_MAIN int
elm_main(int argc, char **argv)
{
    App_Data *ad;
    char edj_path[PATH_MAX];
    const char *theme_locations[] = {
        "data/default.edj",
        "build/data/default.edj",
        "../data/default.edj",
        DATA_DIR "/themes/default.edj",
        NULL
    };
    Eina_Bool theme_found = EINA_FALSE;

    /* Initialize */
    eet_init();
    ad = calloc(1, sizeof(App_Data));

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--debug")) {
            ad->debug = EINA_TRUE;
        } else if (!strcmp(argv[i], "--normal")) {
            ad->normal_window = EINA_TRUE;
        } else if (!strcmp(argv[i], "--seconds")) {
            ad->show_seconds = EINA_TRUE;
        } else if (!strcmp(argv[i], "--help")) {
            _print_help(argv[0]);
            free(ad);
            eet_shutdown();
            return 0;
        }
    }

    /* Load configuration */
    _config_init(ad);

    /* Create window */
    ad->win = elm_win_add(NULL, "clock-elive",
                          ad->normal_window ? ELM_WIN_BASIC : ELM_WIN_DESKTOP);
    elm_win_title_set(ad->win, "Elive Clock");
    elm_win_autodel_set(ad->win, EINA_TRUE);
    elm_win_alpha_set(ad->win, EINA_TRUE);

    if (!ad->normal_window) {
        elm_win_borderless_set(ad->win, EINA_TRUE);
        elm_win_sticky_set(ad->win, EINA_TRUE);
    }

    /* Create layout */
    ad->layout = elm_layout_add(ad->win);

    /* Find theme file */
        /* Find theme file */
    for (int i = 0; theme_locations[i]; i++) {
        if (ecore_file_exists(theme_locations[i])) {
            snprintf(edj_path, sizeof(edj_path), "%s", theme_locations[i]);
            theme_found = EINA_TRUE;
            break;
        }
    }

    if (!theme_found) {
        fprintf(stderr, "ERROR: Could not find default.edj theme file!\n");
        _config_shutdown(ad);
        elm_exit();
        free(ad);
        eet_shutdown();
        return 1;
    }

    if (!elm_layout_file_set(ad->layout, edj_path, "clock/main")) {
        fprintf(stderr, "ERROR: Could not load theme from %s\n", edj_path);
        _config_shutdown(ad);
        elm_exit();
        free(ad);
        eet_shutdown();
        return 1;
    }

    evas_object_size_hint_weight_set(ad->layout, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    elm_win_resize_object_add(ad->win, ad->layout);

    /* Set up callbacks */
    evas_object_smart_callback_add(ad->win, "delete,request", _win_del_cb, ad);
    elm_object_signal_callback_add(ad->layout, "close,clicked", "*", _close_cb, ad);
    elm_object_signal_callback_add(ad->layout, "date,clicked", "date_event_area", _date_click_cb, ad);

    /* Mouse event callbacks */
    evas_object_event_callback_add(ad->layout, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down_cb, ad);
    evas_object_event_callback_add(ad->layout, EVAS_CALLBACK_MOUSE_UP, _mouse_up_cb, ad);
    evas_object_event_callback_add(ad->layout, EVAS_CALLBACK_MOUSE_MOVE, _mouse_move_cb, ad);

    // Connect EDC signal for UTC toggle
    elm_object_signal_callback_add(ad->layout, "clock,utc_toggle", "elm", _utc_toggle_cb, ad);

    /* Initial update */
    _timer_cb(ad);

    /* Apply saved date visibility */
    elm_layout_signal_emit(ad->layout, ad->show_date ? "date,show" : "date,hide", "elm");

    /* Set up timer */
    if (ad->show_seconds) {
        ad->timer = ecore_timer_add(TIMER_INTERVAL_SECONDS, _timer_cb, ad);
    } else {
        double interval = _get_next_timer_interval(ad->show_seconds);
        ad->timer = ecore_timer_add(interval, _minute_timer_cb, ad);
    }

    /* Show window */
    evas_object_resize(ad->win, WINDOW_WIDTH, WINDOW_HEIGHT);
    evas_object_show(ad->layout);
    evas_object_show(ad->win);

    /* Window properties */
    elm_win_prop_focus_skip_set(ad->win, !ad->normal_window);

    if (!ad->normal_window) {
        elm_win_layer_set(ad->win, ELM_OBJECT_LAYER_BACKGROUND);
    }

    /* Main loop */
    elm_run();

    /* Cleanup */
    _config_shutdown(ad);
    free(ad);
    eet_shutdown();

    return 0;
}
ELM_MAIN()
