/**
 * @file main.c
 * @brief Elive Clock application main source file.
 *
 * This application displays the current time and date on the desktop.
 * It can run as a normal window or as a desktop gadget, and supports
 * dragging to reposition the window.
 */
#define _GNU_SOURCE
#include <Elementary.h>
#include <Ecore_X.h>
#include <time.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Application data structure.
 *
 * This structure holds all the necessary data for the application's state
 * and UI elements.
 */
typedef struct _App_Data {
    Evas_Object *win; /**< @brief The main application window. */
    Evas_Object *layout; /**< @brief The layout object managing the UI elements, loaded from an EDJ theme. */
    Ecore_Timer *timer; /**< @brief The timer used for updating the time display. */
    Eina_Bool debug; /**< @brief Flag to enable/disable debug output to stdout. */
    Eina_Bool normal_window; /**< @brief Flag to determine if the window is a normal application window or a desktop gadget. */
    Eina_Bool show_seconds; /**< @brief Flag to determine if seconds should be displayed in the time string. */
    Eina_Bool dragging; /**< @brief Flag indicating if the window is currently being dragged. */
    int drag_start_x; /**< @brief X coordinate of the mouse pointer when dragging started. */
    int drag_start_y; /**< @brief Y coordinate of the mouse pointer when dragging started. */
    int win_start_x; /**< @brief X coordinate of the window when dragging started. */
    int win_start_y; /**< @brief Y coordinate of the window when dragging started. */
} App_Data;

/**
 * @brief Callback for the close button signal from the EDJ layout.
 *
 * This function is triggered when the "close,clicked" signal is emitted
 * from any part matching "*" within the EDJ layout. It quits the main loop,
 * effectively closing the application.
 *
 * @param data A pointer to the App_Data structure.
 * @param obj The Evas_Object that emitted the signal (the layout object).
 * @param emission The emission string ("close,clicked").
 * @param source The source string ("*").
 */
static void
_close_cb(void *data, Evas_Object *obj EINA_UNUSED, const char *emission EINA_UNUSED, const char *source EINA_UNUSED)
{
    App_Data *ad = data;
    if (ad->debug) printf("Close button clicked\n");
    ecore_main_loop_quit();
}

/**
 * @brief Timer callback to update the time and date display.
 *
 * This function is called periodically by an Ecore_Timer. It retrieves the current
 * time and date, formats them into strings, and updates the "time_text" and
 * "date_text" parts of the EDJ layout.
 *
 * The time format depends on the `show_seconds` flag in `App_Data`.
 *
 * @param data A pointer to the App_Data structure.
 * @return ECORE_CALLBACK_RENEW to keep the timer running, or ECORE_CALLBACK_CANCEL
 *         if it's a one-shot timer (though this function typically renews).
 */
static Eina_Bool
_timer_cb(void *data)
{
    App_Data *ad = data;
    time_t rawtime;
    struct tm *timeinfo;
    char time_str[32];
    char date_str[64];

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    if (ad->show_seconds) {
        strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    } else {
        strftime(time_str, sizeof(time_str), "%H:%M", timeinfo);
    }
    strftime(date_str, sizeof(date_str), "%A, %B %d, %Y", timeinfo);

    if (ad->debug) {
        printf("Setting time: %s\n", time_str);
        printf("Setting date: %s\n", date_str);
    }

    edje_object_part_text_set(elm_layout_edje_get(ad->layout), "time_text", time_str);
    edje_object_part_text_set(elm_layout_edje_get(ad->layout), "date_text", date_str);

    return ECORE_CALLBACK_RENEW;
}

/**
 * @brief Calculates the time interval until the start of the next minute.
 *
 * This function is used when `show_seconds` is EINA_FALSE to synchronize
 * the clock updates precisely with the minute change. It calculates how many
 * seconds are left until the current minute ends.
 *
 * @param show_seconds A boolean flag indicating if seconds are currently shown.
 *                     If true, the interval is always 1.0 second.
 * @return The number of seconds until the next minute, or 1.0 if `show_seconds` is true.
 *         Example: If current time is 10:30:15, returns 45.0.
 *                  If current time is 10:30:00, returns 60.0.
 */
static double
_get_next_timer_interval(Eina_Bool show_seconds)
{
    if (show_seconds) {
        return 1.0;
    } else {
        // Calculate seconds until next minute
        time_t rawtime;
        struct tm *timeinfo;

        time(&rawtime);
        timeinfo = localtime(&rawtime);

        int seconds_left = 60 - timeinfo->tm_sec;
        return (double)seconds_left;
    }
}

/**
 * @brief Callback for window delete requests.
 *
 * This function is called when the window receives a "delete,request" smart callback,
 * typically from the window manager when the user tries to close the window.
 * It cleans up the timer and quits the main loop.
 *
 * @param data A pointer to the App_Data structure.
 * @param obj The Evas_Object that emitted the signal (the window object).
 * @param event_info Event information (unused).
 */
static void
_win_del_cb(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
    App_Data *ad = data;
    if (ad->timer) ecore_timer_del(ad->timer);
    ecore_main_loop_quit();
}

/**
 * @brief Callback for mouse down events on the layout.
 *
 * This function handles the start of a window drag operation.
 * If the left mouse button (button 1) is pressed, it records the initial
 * mouse and window positions, then grabs the X pointer to ensure all
 * subsequent mouse events are delivered to this window for dragging.
 *
 * @param data A pointer to the App_Data structure.
 * @param e The Evas canvas.
 * @param obj The Evas_Object that received the event (the layout object).
 * @param event_info Pointer to Evas_Event_Mouse_Down containing mouse event details.
 */
static void
_mouse_down_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
    App_Data *ad = data;
    Evas_Event_Mouse_Down *ev = event_info;

    if (ev->button == 1) {
        Ecore_X_Window xwin = elm_win_xwindow_get(ad->win);
        if (xwin) {
            ad->dragging = EINA_TRUE;

            // Get current pointer position
            Ecore_X_Window root = ecore_x_window_root_get(xwin);
            ecore_x_pointer_xy_get(root, &ad->drag_start_x, &ad->drag_start_y);

            // Get current window position from Elementary
            evas_object_geometry_get(ad->win, &ad->win_start_x, &ad->win_start_y, NULL, NULL);

            // Grab the pointer for dragging
            ecore_x_pointer_grab(xwin);

            if (ad->debug) {
                printf("Started dragging - pointer at: %d,%d, window at: %d,%d\n",
                       ad->drag_start_x, ad->drag_start_y, ad->win_start_x, ad->win_start_y);
            }
        }
    }
}

/**
 * @brief Callback for mouse up events on the layout.
 *
 * This function handles the end of a window drag operation or a simple click.
 * If a drag was in progress (left mouse button released), it releases the
 * X pointer grab and updates the window's final position.
 * If it was a simple click (not a drag), it prints the current time to stdout.
 *
 * @param data A pointer to the App_Data structure.
 * @param e The Evas canvas.
 * @param obj The Evas_Object that received the event (the layout object).
 * @param event_info Pointer to Evas_Event_Mouse_Up containing mouse event details.
 */
static void
_mouse_up_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
    App_Data *ad = data;
    Evas_Event_Mouse_Up *ev = event_info;

    if (ev->button == 1) {
        if (ad->dragging) {
            // Release pointer grab
            ecore_x_pointer_ungrab();
            ad->dragging = EINA_FALSE;

            // Sync Elementary position with X11 position
            Ecore_X_Window xwin = elm_win_xwindow_get(ad->win);
            if (xwin) {
                int x, y;
                Ecore_X_Window root = ecore_x_window_root_get(xwin);
                ecore_x_pointer_xy_get(root, &x, &y);

                // Calculate final position
                int final_x = ad->win_start_x + (x - ad->drag_start_x);
                int final_y = ad->win_start_y + (y - ad->drag_start_y);

                // Update Elementary window position
                evas_object_move(ad->win, final_x, final_y);

                if (ad->debug) {
                    printf("Stopped dragging - window now at: %d,%d\n", final_x, final_y);
                }
            }
        } else {
            // Print time on click
            time_t rawtime;
            struct tm *timeinfo;
            char time_str[32];

            time(&rawtime);
            timeinfo = localtime(&rawtime);

            if (ad->show_seconds) {
                strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
            } else {
                strftime(time_str, sizeof(time_str), "%H:%M", timeinfo);
            }

            printf("Current time: %s\n", time_str);
        }
    }
}

/**
 * @brief Callback for mouse move events on the layout.
 *
 * This function handles the continuous movement of the window during a drag operation.
 * If `dragging` is true and the left mouse button is held down, it calculates
 * the new window position based on the mouse's displacement from the drag start
 * and moves both the X11 window and the Elementary window.
 *
 * @param data A pointer to the App_Data structure.
 * @param e The Evas canvas.
 * @param obj The Evas_Object that received the event (the layout object).
 * @param event_info Pointer to Evas_Event_Mouse_Move containing mouse event details.
 */
static void
_mouse_move_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
    App_Data *ad = data;
    Evas_Event_Mouse_Move *ev = event_info;

    if (ad->dragging && ev->buttons == 1) {
        Ecore_X_Window xwin = elm_win_xwindow_get(ad->win);
        if (xwin) {
            // Get current pointer position
            int x, y;
            Ecore_X_Window root = ecore_x_window_root_get(xwin);
            ecore_x_pointer_xy_get(root, &x, &y);

            // Calculate new window position
            int new_x = ad->win_start_x + (x - ad->drag_start_x);
            int new_y = ad->win_start_y + (y - ad->drag_start_y);

            // Move both X11 window and Elementary window
            ecore_x_window_move(xwin, new_x, new_y);
            evas_object_move(ad->win, new_x, new_y);

            if (ad->debug) {
                printf("Moving window to: %d, %d\n", new_x, new_y);
            }
        }
    }
}

/**
 * @brief Prints the application's command-line help message.
 *
 * @param prog_name The name of the executable (e.g., argv[0]).
 */
static void
_print_help(const char *prog_name)
{
    printf("Usage: %s [OPTIONS]\n\nElive Clock - A beautiful desktop clock\n\n", prog_name);
    printf("Options:\n");
    printf("  --debug    Enable debug output\n");
    printf("  --normal   Create a normal window (not a desktop gadget)\n");
    printf("  --seconds  Show seconds in the time display\n");
    printf("  --help     Show this help message\n");
}

/**
 * @brief One-shot timer callback to synchronize the clock to the next minute.
 *
 * This function is used when `show_seconds` is EINA_FALSE. It is scheduled
 * to run exactly at the start of the next minute. Once executed, it updates
 * the display and then sets up a recurring `_timer_cb` to run every 60 seconds.
 * This ensures the clock updates precisely on the minute mark.
 *
 * @param data A pointer to the App_Data structure.
 * @return ECORE_CALLBACK_CANCEL to ensure this timer runs only once.
 */
static Eina_Bool
_minute_timer_cb(void *data)
{
    App_Data *ad = data;

    // Update the display immediately
    _timer_cb(ad);

    // Set up the regular minute timer to run every 60 seconds from now on
    if (ad->timer) ecore_timer_del(ad->timer);
    ad->timer = ecore_timer_add(60.0, _timer_cb, ad);

    return ECORE_CALLBACK_CANCEL; // This timer runs only once
}

/**
 * @brief Main entry point for the Enlightenment Foundation Libraries (EFL) application.
 *
 * This function initializes the application, parses command-line arguments,
 * creates and configures the main window (either normal or desktop gadget),
 * loads the UI theme, sets up event callbacks for window management and
 * mouse interactions, initializes the time display, and starts the update timer.
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of command-line argument strings.
 *             Example: `argv` could be `{"./elive_clock", "--debug", "--seconds"}`
 * @return 0 on successful execution, 1 on error (e.g., theme file not found).
 */
EAPI_MAIN int
elm_main(int argc, char **argv)
{
    App_Data *ad = calloc(1, sizeof(App_Data));
    char edj_path[PATH_MAX];

    // Parse command line arguments
    ad->debug = EINA_FALSE;
    ad->normal_window = EINA_FALSE;
    ad->show_seconds = EINA_FALSE;
    ad->dragging = EINA_FALSE;

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
            return 0;
        }
    }

    // Create window
    if (ad->normal_window) {
        // Normal window mode
        ad->win = elm_win_add(NULL, "clock-elive", ELM_WIN_BASIC);
        elm_win_title_set(ad->win, "Elive Clock");
        elm_win_autodel_set(ad->win, EINA_TRUE);
        elm_win_alpha_set(ad->win, EINA_TRUE);

        if (ad->debug) printf("Creating normal window\n");
    } else {
        // Desktop gadget mode
        ad->win = elm_win_add(NULL, "clock-elive", ELM_WIN_DESKTOP);
        elm_win_title_set(ad->win, "Elive Clock");
        elm_win_autodel_set(ad->win, EINA_TRUE);
        elm_win_alpha_set(ad->win, EINA_TRUE);
        elm_win_borderless_set(ad->win, EINA_TRUE);
        elm_win_sticky_set(ad->win, EINA_TRUE);  // On all desktops

        if (ad->debug) printf("Creating desktop gadget window\n");
    }

    // Create layout
    ad->layout = elm_layout_add(ad->win);

    // Try to find the theme file in multiple locations
    /**
     * @brief Array of possible theme file locations.
     *
     * The application attempts to load the `default.edj` theme file
     * from these paths in order until one is found.
     *
     * Example structure:
     * @code
     * const char *locations[] = {
     *     "data/default.edj",         // Relative path, e.g., from build directory
     *     "build/data/default.edj",   // Another relative path
     *     "../data/default.edj",      // Relative to binary location
     *     DATA_DIR "/themes/default.edj", // Absolute path (defined at compile time)
     *     NULL                        // Sentinel value to mark end of array
     * };
     * @endcode
     */
    const char *locations[] = {
        "data/default.edj",  // In build directory
        "build/data/default.edj",  // In parent build directory
        "../data/default.edj", // Relative to binary
        DATA_DIR "/themes/default.edj", // Source directory
        NULL
    };

    Eina_Bool found = EINA_FALSE;
    for (int i = 0; locations[i]; i++) {
        if (ecore_file_exists(locations[i])) {
            snprintf(edj_path, sizeof(edj_path), "%s", locations[i]);
            found = EINA_TRUE;
            if (ad->debug) {
                printf("Found theme at: %s\n", edj_path);
            }
            break;
        }
    }

    if (!found) {
        fprintf(stderr, "ERROR: Could not find default.edj file!\n");
        elm_exit();
        free(ad);
        return 1;
    }

    if (!elm_layout_file_set(ad->layout, edj_path, "clock/main")) {
        fprintf(stderr, "ERROR: Could not load theme from %s\n", edj_path);
        elm_exit();
        free(ad);
        return 1;
    }

    evas_object_size_hint_weight_set(ad->layout, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    elm_win_resize_object_add(ad->win, ad->layout);

    // Set up callbacks
    evas_object_smart_callback_add(ad->win, "delete,request", _win_del_cb, ad);
    elm_object_signal_callback_add(ad->layout, "close,clicked", "*", _close_cb, ad);

    // Mouse event callbacks
    evas_object_event_callback_add(ad->layout, EVAS_CALLBACK_MOUSE_DOWN, _mouse_down_cb, ad);
    evas_object_event_callback_add(ad->layout, EVAS_CALLBACK_MOUSE_UP, _mouse_up_cb, ad);
    evas_object_event_callback_add(ad->layout, EVAS_CALLBACK_MOUSE_MOVE, _mouse_move_cb, ad);

    // Initial time update
    _timer_cb(ad);

    // Set up timer for updates
    if (ad->show_seconds) {
        // Update every second
        ad->timer = ecore_timer_add(1.0, _timer_cb, ad);
    } else {
        // Update at the start of next minute, then every minute
        double interval = _get_next_timer_interval(ad->show_seconds);
        ad->timer = ecore_timer_add(interval, _minute_timer_cb, ad);
    }

    // Show window
    evas_object_resize(ad->win, 300, 120);
    evas_object_show(ad->layout);
    evas_object_show(ad->win);

    // Make window ignore focus
    elm_win_prop_focus_skip_set(ad->win, EINA_TRUE);

    // Lower the window to bottom if in gadget mode
    // TODO: move it to layers instead, in the same layer as conky
    if (!ad->normal_window) {
        elm_win_lower(ad->win);
    }

    elm_run();

    free(ad);
    return 0;
}
ELM_MAIN()
