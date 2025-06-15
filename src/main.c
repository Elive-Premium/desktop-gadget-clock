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

// Removed CONFIG_VERSION as migration code is being removed
#define TIMER_INTERVAL_SECONDS 1.0
#define TIMER_INTERVAL_MINUTES 60.0

#define CONFIG_FILE_SUFFIX "/config.eet"
#define CONFIG_FILE_SUFFIX_LEN (sizeof(CONFIG_FILE_SUFFIX) - 1)

// Clock display modes
#define CLOCK_MODE_LOCAL  0
#define CLOCK_MODE_UTC    1
#define CLOCK_MODE_SWATCH 2

/**
 * @brief Configuration data structure for persistent settings
 */
typedef struct _Config {
    Eina_Bool show_date;
    int clock_mode;     // 0 for local, 1 for UTC, 2 for Swatch
    int win_x;          // Saved window X position
    int win_y;          // Saved window Y position
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
    int clock_mode; // 0 for local, 1 for UTC, 2 for Swatch
    int win_x;      // Current window X position
    int win_y;      // Current window Y position

    /* Dragging state for window movement */
    Eina_Bool dragging;
    int drag_start_x;
    int drag_start_y;
    int win_start_x;
    int win_start_y;

    /* Click/Drag detection for Edje signals */
    Eina_Bool click_suppress; // New: Flag to suppress click actions if a drag occurred
    Evas_Coord mouse_down_x;  // New: X coordinate of mouse down
    Evas_Coord mouse_down_y;  // New: Y coordinate of mouse down
} App_Data;

/* Function prototypes */
static Eina_Bool _timer_cb(void *data);
static void _config_save(App_Data *ad);
static Config *_config_load(App_Data *ad);
static void _config_init(App_Data *ad);
static void _config_shutdown(App_Data *ad);
static Eet_Data_Descriptor *_config_descriptor_new(void);
static void _date_click_cb(void *data, Evas_Object *obj, const char *emission, const char *source);
static void _clock_mode_toggle_cb(void *data, Evas_Object *obj, const char *emission, const char *source);
static void _utc_indicator_click_cb(void *data, Evas_Object *obj, const char *emission, const char *source);
static void _mouse_down_cb(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void _mouse_up_cb(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void _mouse_move_cb(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void _win_move_cb(void *data, Evas_Object *obj, void *event_info);
static void _get_swatch_time(time_t rawtime, char *time_str, size_t time_str_len);
static Eina_Bool _minute_timer_cb(void *data);
static double _get_next_timer_interval(Eina_Bool show_seconds);


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
    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Config, "clock_mode", clock_mode, EET_T_INT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Config, "win_x", win_x, EET_T_INT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Config, "win_y", win_y, EET_T_INT);

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

    // Ensure enough space for the suffix in the full path
    snprintf(config_dir, sizeof(config_dir) - CONFIG_FILE_SUFFIX_LEN, "%s/.config/elive-clock", home);

    if (mkdir(config_dir, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "Warning: Failed to create config directory: %s\n", strerror(errno));
    }

    ad->config_file = malloc(PATH_MAX);
    // Concatenate the directory and the config file name
    snprintf(ad->config_file, PATH_MAX, "%s%s", config_dir, CONFIG_FILE_SUFFIX);

    ad->config = _config_load(ad);
    if (!ad->config) {
        // If no config file exists or loading failed, create a new one with defaults
        ad->config = calloc(1, sizeof(Config));
        ad->config->show_date = EINA_TRUE;
        ad->config->clock_mode = CLOCK_MODE_LOCAL; // Default to local time
        ad->config->win_x = 0; // Default position for new configs
        ad->config->win_y = 0;
        _config_save(ad); // Save the new default config
    }

    // Load current settings from config into app data
    ad->show_date = ad->config->show_date;
    ad->clock_mode = ad->config->clock_mode;
    ad->win_x = ad->config->win_x;
    ad->win_y = ad->config->win_y;
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
    ad->config->clock_mode = ad->clock_mode;
    ad->config->win_x = ad->win_x;
    ad->config->win_y = ad->win_y;

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
_close_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED,
          const char *emission EINA_UNUSED, const char *source EINA_UNUSED)
{
    ecore_main_loop_quit();
}

/**
 * @brief Date click callback - toggles date visibility (now unused, but kept for reference if needed)
 */
static void
_date_click_cb(void *data, Evas_Object *obj EINA_UNUSED,
               const char *emission EINA_UNUSED, const char *source EINA_UNUSED)
{
    App_Data *ad = data;
    if (ad->click_suppress) return; // Suppress if a drag was detected

    ad->show_date = !ad->show_date;

    elm_layout_signal_emit(obj, ad->show_date ? "date,show" : "date,hide", "elm");
    _config_save(ad);
    _timer_cb(ad);
}

/**
 * @brief Date click callback - toggles date visibility (now unused, but kept for reference if needed)
 */
static void
_utc_indicator_click_cb(void *data, Evas_Object *obj EINA_UNUSED,
               const char *emission EINA_UNUSED, const char *source EINA_UNUSED)
{
    App_Data *ad = data;
    if (ad->click_suppress) return; // Suppress if a drag was detected

    // If the current mode is Swatch Internet Time, launch web-launcher
    if (ad->clock_mode == CLOCK_MODE_SWATCH)
    {
        ecore_exe_run("web-launcher https://internettime.elivecd.org/", NULL);
    }

    _timer_cb(ad);
}

/**
 * @brief Callback for toggling clock display mode (Local, UTC, Swatch)
 */
static void
_clock_mode_toggle_cb(void *data, Evas_Object *obj EINA_UNUSED,
                      const char *emission EINA_UNUSED, const char *source EINA_UNUSED)
{
    App_Data *ad = data;
    if (ad->click_suppress) return; // Suppress if a drag was detected

    ad->clock_mode++;
    if (ad->clock_mode > CLOCK_MODE_SWATCH) {
        ad->clock_mode = CLOCK_MODE_LOCAL;
    }
    _config_save(ad);

    // Delete existing timer
    if (ad->timer) {
        ecore_timer_del(ad->timer);
        ad->timer = NULL;
    }

    // Set up new timer based on the selected mode and original show_seconds preference
    if (ad->clock_mode == CLOCK_MODE_SWATCH) {
        ad->timer = ecore_timer_add(TIMER_INTERVAL_SECONDS, _timer_cb, ad);
    } else { // CLOCK_MODE_LOCAL or CLOCK_MODE_UTC
        if (ad->show_seconds) {
            ad->timer = ecore_timer_add(TIMER_INTERVAL_SECONDS, _timer_cb, ad);
        } else {
            // Use _minute_timer_cb to align to the next minute, then switch to regular minute updates
            double interval = _get_next_timer_interval(EINA_FALSE);
            ad->timer = ecore_timer_add(interval, _minute_timer_cb, ad);
        }
    }

    _timer_cb(ad); // Immediately update the display
}

/**
 * @brief Calculates Swatch Internet Time (@beats)
 * @param rawtime The current time in UTC.
 * @param time_str Buffer to store the formatted time string.
 * @param time_str_len Length of the time_str buffer.
 */
static void
_get_swatch_time(time_t rawtime, char *time_str, size_t time_str_len)
{
    struct tm timeinfo_utc;
    struct tm *timeinfo;
    double beats;

    // Get UTC time
    timeinfo = gmtime_r(&rawtime, &timeinfo_utc);

    // Swatch Internet Time (Biel Mean Time - BMT) is UTC+1
    // Calculate seconds since midnight BMT
    int hour_bmt = timeinfo->tm_hour + 1; // UTC+1
    int min_bmt = timeinfo->tm_min;
    int sec_bmt = timeinfo->tm_sec;

    // Handle day wrap-around for BMT (e.g., 23:00 UTC becomes 00:00 BMT next day)
    if (hour_bmt >= 24) {
        hour_bmt -= 24;
    }

    long total_seconds_bmt = (long)hour_bmt * 3600 + (long)min_bmt * 60 + (long)sec_bmt;

    // 1 day = 1000 beats. 1 beat = 86.4 seconds.
    beats = (double)total_seconds_bmt / 86.4;

    snprintf(time_str, time_str_len, "@%06.2f", beats); // Format as @BBB.FF
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

    switch (ad->clock_mode) {
        case CLOCK_MODE_LOCAL:
            timeinfo = localtime_r(&rawtime, &timeinfo_buf); // Use local time
            strftime(time_str, sizeof(time_str),
                     ad->show_seconds ? "%H:%M:%S" : "%H:%M", timeinfo);
            strftime(date_str, sizeof(date_str), "%A, %B %d, %Y", timeinfo);
            edje_object_part_text_set(elm_layout_edje_get(ad->layout), "utc_indicator_text", "");
            break;
        case CLOCK_MODE_UTC:
            timeinfo = gmtime_r(&rawtime, &timeinfo_buf); // Use UTC time
            strftime(time_str, sizeof(time_str),
                     ad->show_seconds ? "%H:%M:%S" : "%H:%M", timeinfo);
            strftime(date_str, sizeof(date_str), "%A, %B %d, %Y", timeinfo);
            edje_object_part_text_set(elm_layout_edje_get(ad->layout), "utc_indicator_text", "UTC");
            break;
        case CLOCK_MODE_SWATCH:
            _get_swatch_time(rawtime, time_str, sizeof(time_str));
            strftime(date_str, sizeof(date_str), "%A, %B %d, %Y", localtime_r(&rawtime, &timeinfo_buf)); // Display local date for Swatch
            edje_object_part_text_set(elm_layout_edje_get(ad->layout), "utc_indicator_text", "Internet Time");
            break;
        default:
            // Should not happen, fall back to local
            timeinfo = localtime_r(&rawtime, &timeinfo_buf);
            strftime(time_str, sizeof(time_str),
                     ad->show_seconds ? "%H:%M:%S" : "%H:%M", timeinfo);
            strftime(date_str, sizeof(date_str), "%A, %B %d, %Y", timeinfo);
            edje_object_part_text_set(elm_layout_edje_get(ad->layout), "utc_indicator_text", "");
            break;
    }

    edje_object_part_text_set(elm_layout_edje_get(ad->layout), "time_text", time_str);
    edje_object_part_text_set(elm_layout_edje_get(ad->layout), "date_text", date_str);

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
 * @brief Mouse down callback - starts dragging or prepares for click detection
 */
static void
_mouse_down_cb(void *data, Evas *e EINA_UNUSED,
               Evas_Object *obj EINA_UNUSED, void *event_info)
{
    App_Data *ad = data;
    Evas_Event_Mouse_Down *ev = event_info;

    if (ev->button != 1) return;

    // Record initial mouse position for click/drag detection
    ad->mouse_down_x = ev->canvas.x;
    ad->mouse_down_y = ev->canvas.y;
    ad->click_suppress = EINA_FALSE; // Reset suppression flag for new click/drag
    ad->dragging = EINA_FALSE; // Not dragging yet

    // Save window position for potential drag
    evas_object_geometry_get(ad->win, &ad->win_start_x, &ad->win_start_y, NULL, NULL);

    // Save pointer position for potential drag
    Ecore_X_Window xwin = elm_win_xwindow_get(ad->win);
    if (!xwin) return;
    Ecore_X_Window root = ecore_x_window_root_get(xwin);
    ecore_x_pointer_xy_get(root, &ad->drag_start_x, &ad->drag_start_y);
    // Do not grab pointer yet; only grab if drag threshold is exceeded
}

/**
 * @brief Mouse up callback - ends dragging and allows click if no drag occurred
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
        // click_suppress remains set, so click is not allowed after drag
    } else {
        // If not dragging, allow click (click_suppress should be false)
        ad->click_suppress = EINA_FALSE;
    }
    // click_suppress will be reset on next mouse down
}

/**
 * @brief Mouse move callback - handles dragging and updates click suppression
 */
static void
_mouse_move_cb(void *data, Evas *e EINA_UNUSED,
               Evas_Object *obj EINA_UNUSED, void *event_info)
{
    App_Data *ad = data;
    Evas_Event_Mouse_Move *ev = event_info;

    if (ev->buttons != 1) return; // Only handle left button moves

    // Calculate drag distance from initial mouse down
    int dx = ev->cur.canvas.x - ad->mouse_down_x;
    int dy = ev->cur.canvas.y - ad->mouse_down_y;
    int dist_sq = dx * dx + dy * dy;
    const int drag_threshold = 5; // pixels
    const int drag_threshold_sq = drag_threshold * drag_threshold;

    if (!ad->dragging) {
        // Only start dragging if moved more than threshold
        if (dist_sq > drag_threshold_sq) {
            ad->dragging = EINA_TRUE;
            ad->click_suppress = EINA_TRUE; // Suppress click if dragging
            // Grab pointer now
            Ecore_X_Window xwin = elm_win_xwindow_get(ad->win);
            if (xwin) ecore_x_pointer_grab(xwin);
        } else {
            // Not enough movement, do not drag, do not suppress click
            return;
        }
    }

    // If dragging, move the window
    Ecore_X_Window xwin = elm_win_xwindow_get(ad->win);
    if (!xwin) return;

    int x_pointer, y_pointer;
    Ecore_X_Window root = ecore_x_window_root_get(xwin);
    ecore_x_pointer_xy_get(root, &x_pointer, &y_pointer);

    int new_x = ad->win_start_x + (x_pointer - ad->drag_start_x);
    int new_y = ad->win_start_y + (y_pointer - ad->drag_start_y);

    // Clamp to allow dragging window up to 30% outside the screen
    int screen_x, screen_y, screen_w, screen_h;
    elm_win_screen_size_get(ad->win, &screen_x, &screen_y, &screen_w, &screen_h);

    int win_w, win_h;
    evas_object_geometry_get(ad->win, NULL, NULL, &win_w, &win_h);

    int min_x = screen_x - (int)(win_w * 0.3);
    int max_x = screen_x + screen_w - (int)(win_w * 0.7);
    if (new_x < min_x) new_x = min_x;
    if (new_x > max_x) new_x = max_x;

    int min_y = screen_y - (int)(win_h * 0.3);
    int max_y = screen_y + screen_h - (int)(win_h * 0.7);
    if (new_y < min_y) new_y = min_y;
    if (new_y > max_y) new_y = max_y;

    ecore_x_window_move(xwin, new_x, new_y);
    evas_object_move(ad->win, new_x, new_y);
}

/**
 * @brief Callback for window move events to save position
 */
static void
_win_move_cb(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
    App_Data *ad = data;
    evas_object_geometry_get(ad->win, &ad->win_x, &ad->win_y, NULL, NULL);
    _config_save(ad);
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
 * @brief Clamp window position to fit within screen limits and allowed clamping
 */
static void
_clamp_window_position(App_Data *ad, int win_w, int win_h, int screen_x, int screen_y, int screen_w, int screen_h)
{
    int x = ad->win_x;
    int y = ad->win_y;
    Eina_Bool position_adjusted = EINA_FALSE;

    // Allow window to be up to 30% outside the screen horizontally
    int min_x = screen_x - (int)(win_w * 0.3);
    int max_x = screen_x + screen_w - (int)(win_w * 0.7);
    if (x < min_x) {
        if (ad->debug) fprintf(stderr, "DEBUG: Adjusting window X from %d to %d (left 30%% clamp)\n", x, min_x);
        x = min_x;
        position_adjusted = EINA_TRUE;
    }
    if (x > max_x) {
        if (ad->debug) fprintf(stderr, "DEBUG: Adjusting window X from %d to %d (right 30%% clamp)\n", x, max_x);
        x = max_x;
        position_adjusted = EINA_TRUE;
    }

    // Allow window to be up to 30% outside the screen vertically
    int min_y = screen_y - (int)(win_h * 0.3);
    int max_y = screen_y + screen_h - (int)(win_h * 0.7);
    if (y < min_y) {
        if (ad->debug) fprintf(stderr, "DEBUG: Adjusting window Y from %d to %d (top 30%% clamp)\n", y, min_y);
        y = min_y;
        position_adjusted = EINA_TRUE;
    }
    if (y > max_y) {
        if (ad->debug) fprintf(stderr, "DEBUG: Adjusting window Y from %d to %d (bottom 30%% clamp)\n", y, max_y);
        y = max_y;
        position_adjusted = EINA_TRUE;
    }

    ad->win_x = x;
    ad->win_y = y;

    if (position_adjusted) {
        if (ad->debug) fprintf(stderr, "DEBUG: Window position adjusted to (%d, %d). Saving config.\n", ad->win_x, ad->win_y);
        _config_save(ad);
    }
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
        DATA_DIR "/themes/default.edj",
        "data/default.edj",
        "build/data/default.edj",
        "../data/default.edj",
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

    // Always set borderless, regardless of normal_window mode
    elm_win_borderless_set(ad->win, EINA_TRUE);

    if (!ad->normal_window) {
        elm_win_sticky_set(ad->win, EINA_TRUE);
    }

    /* Create layout */
    ad->layout = elm_layout_add(ad->win);

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
    evas_object_smart_callback_add(ad->win, "move", _win_move_cb, ad); // Add move callback
    elm_object_signal_callback_add(ad->layout, "close,clicked", "*", _close_cb, ad);
    elm_object_signal_callback_add(ad->layout, "date,clicked", "date_event_area", _date_click_cb, ad);
    elm_object_signal_callback_add(ad->layout, "utc_indicator,clicked", "elm", _utc_indicator_click_cb, ad);

    /* Mouse event callbacks */
    evas_object_event_callback_add(ad->layout, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down_cb, ad);
    evas_object_event_callback_add(ad->layout, EVAS_CALLBACK_MOUSE_UP, _mouse_up_cb, ad);
    evas_object_event_callback_add(ad->layout, EVAS_CALLBACK_MOUSE_MOVE, _mouse_move_cb, ad);

    // Connect EDC signal for clock mode toggle
    elm_object_signal_callback_add(ad->layout, "clock,mode_toggle", "elm", _clock_mode_toggle_cb, ad); // New signal for cycling modes

    /* Initial update */
    _timer_cb(ad);

    /* Apply saved date visibility */
    elm_layout_signal_emit(ad->layout, ad->show_date ? "date,show" : "date,hide", "elm");

    /* Set up initial timer based on config and arguments */
    if (ad->clock_mode == CLOCK_MODE_SWATCH) {
        ad->timer = ecore_timer_add(TIMER_INTERVAL_SECONDS, _timer_cb, ad);
    } else {
        if (ad->show_seconds) {
            ad->timer = ecore_timer_add(TIMER_INTERVAL_SECONDS, _timer_cb, ad);
        } else {
            double interval = _get_next_timer_interval(ad->show_seconds);
            ad->timer = ecore_timer_add(interval, _minute_timer_cb, ad);
        }
    }

    /* Show window */
    // Get minimum size from theme/layout
    Evas_Coord min_w = 0, min_h = 0;
    evas_object_size_hint_min_get(ad->layout, &min_w, &min_h);
    if (min_w < 1) min_w = 300;
    if (min_h < 1) min_h = 120;
    evas_object_resize(ad->win, min_w, min_h);
    evas_object_show(ad->layout); // Show layout first
    evas_object_show(ad->win);    // Then show window to allow size negotiation

    // Get actual window dimensions after it's been shown and potentially resized by the system/theme
    int win_w, win_h;
    evas_object_geometry_get(ad->win, NULL, NULL, &win_w, &win_h);

    // Get screen geometry (x, y, width, height)
    int screen_x, screen_y, screen_w, screen_h;
    elm_win_screen_size_get(ad->win, &screen_x, &screen_y, &screen_w, &screen_h);

    // Clamp window position to fit on screen and allowed clamping
    _clamp_window_position(ad, win_w, win_h, screen_x, screen_y, screen_w, screen_h);

    // Move the window to the loaded/adjusted position
    evas_object_move(ad->win, ad->win_x, ad->win_y);

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
