#include <gtk/gtk.h>
#include <ImageStreamIO/ImageStreamIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define TRACE_MAX_SAMPLES 360000
#define TRACE_HIST_BINS 256

// Enums for Dropdowns
enum {
    COLORMAP_GREY = 0,
    COLORMAP_RED,
    COLORMAP_GREEN,
    COLORMAP_BLUE,
    COLORMAP_HEAT,
    COLORMAP_COOL,
    COLORMAP_RAINBOW,
    COLORMAP_A,
    COLORMAP_B,
    COLORMAP_COUNT
};

enum {
    SCALE_LINEAR = 0,
    SCALE_LOG,
    SCALE_SQRT,
    SCALE_SQUARE,
    SCALE_ASINH,
    SCALE_COUNT
};

// Application state
typedef struct {
    IMAGE *image;
    GtkWidget *image_area; // Replaces picture
    GtkWidget *colorbar;
    GtkWidget *selection_area;
    char *image_name;
    guint timeout_id;

    // Image Data Buffer for Cairo
    guchar *display_buffer;
    size_t display_buffer_size;

    // Raw Data Buffer (Cache for Pause)
    void *raw_buffer;
    size_t raw_buffer_size;

    int img_width;
    int img_height;

    // Scaling state
    double min_val;
    double max_val;
    gboolean fixed_min;
    gboolean fixed_max;

    // Colormap Windowing (0.0 - 1.0 relative to min_val/max_val)
    double cmap_min;
    double cmap_max;

    // Actual scaling used for display (for colorbar)
    double current_min;
    double current_max;

    // Palette & Scale
    int colormap_type;
    int scale_type;

    // Control flags
    gboolean force_redraw;
    gboolean paused;

    // Selection state (Left Click)
    gboolean selection_active;
    gboolean is_dragging;
    gboolean is_moving_selection;
    double start_x, start_y; // Widget coords
    double curr_x, curr_y;   // Widget coords

    // Contrast Adjustment state (Right Click)
    gboolean is_adjusting_contrast;
    double contrast_start_cmap_min;
    double contrast_start_cmap_max;
    double contrast_start_x;
    double contrast_start_y;

    // Selected region in image coordinates (pixels)
    int sel_x1, sel_y1, sel_x2, sel_y2;
    int sel_orig_x1, sel_orig_y1, sel_orig_x2, sel_orig_y2;

    // Zoom state
    gboolean fit_window;
    double zoom_factor; // 1.0 = 100%

    // Stats & Histogram
    guint32 *hist_data;
    int hist_bins;
    guint32 hist_max_count;
    double stats_mean;
    double stats_median;

    // Trace Data
    double *trace_time;
    double *trace_min;
    double *trace_max;
    double *trace_mean;
    double *trace_median;
    double *trace_p01;
    double *trace_p09;

    // Trace Histogram Data (Waterfall)
    uint32_t *trace_hist_data;
    double *trace_hist_min;
    double *trace_hist_max;

    int trace_head;
    int trace_count;
    struct timespec trace_start_time;
    gboolean trace_active;
    double trace_duration;

    // UI Widgets
    GtkWidget *lbl_counter;
    GtkWidget *dropdown_fps;
    GtkWidget *dropdown_cmap;
    GtkWidget *dropdown_scale;

    GtkWidget *spin_min;
    GtkWidget *check_min_auto;
    GtkWidget *spin_max;
    GtkWidget *check_max_auto;

    GtkWidget *btn_fit_window;
    GtkWidget *dropdown_zoom;
    GtkWidget *lbl_zoom;

    // Stats Widgets
    GtkWidget *vbox_stats;
    GtkWidget *btn_stats_update;
    GtkWidget *box_stats;
    GtkWidget *entry_stat_min;
    GtkWidget *entry_stat_max;
    GtkWidget *entry_stat_mean;
    GtkWidget *entry_stat_median;
    GtkWidget *entry_stat_p01;
    GtkWidget *entry_stat_p09;
    GtkWidget *entry_stat_npix;
    GtkWidget *entry_stat_sum;

    GtkWidget *check_histogram;
    GtkWidget *dropdown_hist_scale;
    // Pixel Info
    GtkWidget *lbl_pixel_info_main;
    GtkWidget *lbl_pixel_info_roi;
    GtkWidget *histogram_area;
    gboolean hist_mouse_active;
    double hist_mouse_x;

    // Colorbar Cursor
    double colorbar_cursor_val;
    gboolean colorbar_cursor_active;

    // Trace UI
    GtkWidget *check_trace;
    GtkWidget *spin_trace_dur;
    GtkWidget *trace_area;

    // ROI Expansion
    GtkWidget *btn_expand_roi;
    GtkWidget *frame_stats;
    GtkWidget *paned_images;
    GtkWidget *scrolled_roi;
    GtkWidget *roi_image_area;

    // UI Control
    GtkWidget *vbox_controls;
    GtkWidget *hbox_right;
    GtkWidget *btn_ctrl_overlay;
    GtkWidget *btn_stats_overlay;
    GtkWidget *lbl_fps_est;
    GtkWidget *btn_pause;
    struct timespec last_fps_time;
    uint64_t last_fps_cnt;
} ViewerApp;

// Command line option variables
static double opt_min = 0;
static double opt_max = 0;
static gboolean has_min = FALSE;
static gboolean has_max = FALSE;

// Custom callback to flag if options were set
static gboolean
parse_min_cb (const gchar *option_name,
              const gchar *value,
              gpointer     data,
              GError     **error)
{
    opt_min = g_ascii_strtod (value, NULL);
    has_min = TRUE;
    return TRUE;
}

static gboolean
parse_max_cb (const gchar *option_name,
              const gchar *value,
              gpointer     data,
              GError     **error)
{
    opt_max = g_ascii_strtod (value, NULL);
    has_max = TRUE;
    return TRUE;
}

static GOptionEntry entries[] =
{
  { "min", 'm', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, parse_min_cb, "Minimum value for scaling", "VAL" },
  { "max", 'M', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, parse_max_cb, "Maximum value for scaling", "VAL" },
  { NULL }
};

// Forward decl
static void update_zoom_layout(ViewerApp *app);
static void get_image_screen_geometry(ViewerApp *app, double *offset_x, double *offset_y, double *scale);
static void widget_to_image_coords(ViewerApp *app, double wx, double wy, int *ix, int *iy);
gboolean update_display (gpointer user_data);
static void on_btn_autoscale_clicked (GtkButton *btn, gpointer user_data);

// Helper Functions for Color & Scale

static const char* get_datatype_string(int type) {
    switch(type) {
        case _DATATYPE_UINT8: return "UINT8";
        case _DATATYPE_INT8: return "INT8";
        case _DATATYPE_UINT16: return "UINT16";
        case _DATATYPE_INT16: return "INT16";
        case _DATATYPE_UINT32: return "UINT32";
        case _DATATYPE_INT32: return "INT32";
        case _DATATYPE_UINT64: return "UINT64";
        case _DATATYPE_INT64: return "INT64";
        case _DATATYPE_FLOAT: return "FLOAT";
        case _DATATYPE_DOUBLE: return "DOUBLE";
        case _DATATYPE_COMPLEX_FLOAT: return "CFLOAT";
        case _DATATYPE_COMPLEX_DOUBLE: return "CDOUBLE";
        default: return "UNKNOWN";
    }
}

static double apply_scaling(double t, int type) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    switch (type) {
        case SCALE_LINEAR: return t;
        case SCALE_LOG:
            // Standard Log: log10(1 + 1000*t) / log10(1001)
            return log10(1.0 + 1000.0 * t) / 3.0;
        case SCALE_SQRT: return sqrt(t);
        case SCALE_SQUARE: return t * t;
        case SCALE_ASINH:
            // asinh(10*t) / asinh(10)
            return asinh(10.0 * t) / 2.998;
        default: return t;
    }
}

static void get_colormap_color(double t, int type, double *r, double *g, double *b) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    switch (type) {
        case COLORMAP_GREY:
            *r = t; *g = t; *b = t;
            break;
        case COLORMAP_RED:
            *r = t; *g = 0; *b = 0;
            break;
        case COLORMAP_GREEN:
            *r = 0; *g = t; *b = 0;
            break;
        case COLORMAP_BLUE:
            *r = 0; *g = 0; *b = t;
            break;
        case COLORMAP_HEAT:
            // Simple Heat: Black -> Red -> Yellow -> White
            if (t < 0.33) {
                *r = t / 0.33;
                *g = 0;
                *b = 0;
            } else if (t < 0.66) {
                *r = 1.0;
                *g = (t - 0.33) / 0.33;
                *b = 0;
            } else {
                *r = 1.0;
                *g = 1.0;
                *b = (t - 0.66) / 0.34;
            }
            break;
        case COLORMAP_COOL:
            // Cyan -> Magenta
            *r = t;
            *g = 1.0 - t;
            *b = 1.0;
            break;
        case COLORMAP_RAINBOW:
            // Simple HSV walk
            {
                double h = (1.0 - t) * 240.0; // Blue to Red
                double x = 1.0 - fabs(fmod(h / 60.0, 2) - 1.0);
                if (h < 60) { *r=1; *g=x; *b=0; }
                else if (h < 120) { *r=x; *g=1; *b=0; }
                else if (h < 180) { *r=0; *g=1; *b=x; }
                else if (h < 240) { *r=0; *g=x; *b=1; }
                else if (h < 300) { *r=x; *g=0; *b=1; }
                else { *r=1; *g=0; *b=x; }
            }
            break;
        case COLORMAP_A:
            // DS9 "A": Black-Red-Green-Blue-Yellow-Cyan-Magenta-White approximate
            // Actually DS9 A is distinct. Let's do a Green-Red-Blue cycle
            // For now, let's map: 0-0.25 (Black-Red), 0.25-0.5 (Red-White), 0.5-0.75 (White-Blue), 0.75-1 (Blue-Black) - No that's weird.
            // Let's implement "Real" DS9 A approximation:
            if (t < 0.5) { *r = t*2; *g = 0; *b = 0; }
            else { *r = 1; *g = (t-0.5)*2; *b = (t-0.5)*2; }
            break;
        case COLORMAP_B:
            // DS9 "B": Inverse Heat-ish?
            // Let's do Yellow -> Blue
            *r = 1.0 - t;
            *g = 1.0 - t;
            *b = t;
            break;
        default:
            *r = t; *g = t; *b = t;
            break;
    }
}

static void
update_pixel_info(ViewerApp *app, GtkWidget *label, double x, double y, gboolean roi_context) {
    // Use cached buffer if available (handles pause), otherwise fallback to live stream
    void *data_source = app->raw_buffer ? app->raw_buffer : (app->image ? app->image->array.raw : NULL);
    if (!data_source || !app->image) return;

    int ix = 0, iy = 0;

    if (roi_context) {
        if (!app->selection_active) return;

        int roi_w = app->sel_x2 - app->sel_x1;
        int roi_h = app->sel_y2 - app->sel_y1;
        if (roi_w <= 0 || roi_h <= 0) return;

        int width = gtk_widget_get_width(app->roi_image_area);
        int height = gtk_widget_get_height(app->roi_image_area);

        double scale_x = (double)width / roi_w;
        double scale_y = (double)height / roi_h;
        double scale = (scale_x < scale_y) ? scale_x : scale_y;
        double draw_w = roi_w * scale;
        double draw_h = roi_h * scale;
        double off_x = (width - draw_w) / 2.0;
        double off_y = (height - draw_h) / 2.0;

        double rx = (x - off_x) / scale;
        double ry = (y - off_y) / scale;

        ix = (int)rx + app->sel_x1;
        iy = (int)ry + app->sel_y1;
    } else {
        widget_to_image_coords(app, x, y, &ix, &iy);
    }

    if (ix >= 0 && iy >= 0 && ix < app->image->md->size[0] && iy < app->image->md->size[1]) {
        size_t idx = iy * app->image->md->size[0] + ix;
        double val = 0;
        switch (app->image->md->datatype) {
            case _DATATYPE_FLOAT: val = ((float*)data_source)[idx]; break;
            case _DATATYPE_DOUBLE: val = ((double*)data_source)[idx]; break;
            case _DATATYPE_UINT8: val = ((uint8_t*)data_source)[idx]; break;
            case _DATATYPE_INT16: val = ((int16_t*)data_source)[idx]; break;
            case _DATATYPE_UINT16: val = ((uint16_t*)data_source)[idx]; break;
            case _DATATYPE_INT32: val = ((int32_t*)data_source)[idx]; break;
            case _DATATYPE_UINT32: val = ((uint32_t*)data_source)[idx]; break;
            default: val = 0; break;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "(%d, %d) = %.4g", ix, iy, val);
        gtk_label_set_text(GTK_LABEL(label), buf);
        gtk_widget_set_visible(label, TRUE);

        app->colorbar_cursor_val = val;
        app->colorbar_cursor_active = TRUE;
        if (app->colorbar) gtk_widget_queue_draw(app->colorbar);
    } else {
        gtk_widget_set_visible(label, FALSE);
        if (app->colorbar_cursor_active) {
             app->colorbar_cursor_active = FALSE;
             if (app->colorbar) gtk_widget_queue_draw(app->colorbar);
        }
    }
}

static void
on_motion_main (GtkEventControllerMotion *controller,
                double                    x,
                double                    y,
                gpointer                  user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    update_pixel_info(app, app->lbl_pixel_info_main, x, y, FALSE);
}

static void
on_motion_roi (GtkEventControllerMotion *controller,
               double                    x,
               double                    y,
               gpointer                  user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    update_pixel_info(app, app->lbl_pixel_info_roi, x, y, TRUE);
}

static void
on_leave (GtkEventControllerMotion *controller,
          gpointer                  user_data)
{
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    ViewerApp *app = (ViewerApp *)user_data;

    if (widget == app->selection_area) {
        gtk_widget_set_visible(app->lbl_pixel_info_main, FALSE);
    } else if (widget == app->roi_image_area) {
        gtk_widget_set_visible(app->lbl_pixel_info_roi, FALSE);
    }

    app->colorbar_cursor_active = FALSE;
    if (app->colorbar) gtk_widget_queue_draw(app->colorbar);
}

static void
on_motion_hist (GtkEventControllerMotion *controller,
                double                    x,
                double                    y,
                gpointer                  user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->hist_mouse_active = TRUE;
    app->hist_mouse_x = x;
    gtk_widget_queue_draw(app->histogram_area);
}

static void
on_leave_hist (GtkEventControllerMotion *controller,
               gpointer                  user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->hist_mouse_active = FALSE;
    gtk_widget_queue_draw(app->histogram_area);
}

static void
on_pause_toggled (GtkToggleButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->paused = gtk_toggle_button_get_active(btn);
}

// UI Callbacks
static void
on_hide_controls_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gtk_widget_set_visible(app->vbox_controls, FALSE);
    gtk_widget_set_visible(app->btn_ctrl_overlay, TRUE);
}

static void
on_show_controls_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gtk_widget_set_visible(app->vbox_controls, TRUE);
    gtk_widget_set_visible(app->btn_ctrl_overlay, FALSE);
}

static void
on_hide_right_panel_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gtk_widget_set_visible(app->vbox_stats, FALSE);
    gtk_widget_set_visible(app->btn_stats_overlay, TRUE);
}

static void
on_show_right_panel_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gtk_widget_set_visible(app->vbox_stats, TRUE);
    gtk_widget_set_visible(app->btn_stats_overlay, FALSE);
    app->force_redraw = TRUE;
}

static void
on_fps_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    guint selected = gtk_drop_down_get_selected(dropdown);

    guint interval = 33; // Default ~30fps
    // 1, 2, 5, 10, 25, 50, 100
    if (selected == 0) interval = 1000;
    else if (selected == 1) interval = 500;
    else if (selected == 2) interval = 200;
    else if (selected == 3) interval = 100;
    else if (selected == 4) interval = 40;
    else if (selected == 5) interval = 20;
    else if (selected == 6) interval = 10;

    if (app->timeout_id > 0) {
        g_source_remove(app->timeout_id);
    }
    app->timeout_id = g_timeout_add(interval, update_display, app);
}

static void
on_cmap_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->colormap_type = gtk_drop_down_get_selected(dropdown);
    app->force_redraw = TRUE;
}

static void
on_scale_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->scale_type = gtk_drop_down_get_selected(dropdown);
    app->force_redraw = TRUE;
}

static void
on_histogram_toggled (GtkCheckButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gboolean active = gtk_check_button_get_active(btn);
    gtk_widget_set_visible(app->histogram_area, active && app->selection_active);
    app->force_redraw = TRUE;
}

static void
on_hist_scale_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gtk_widget_queue_draw(app->histogram_area);
}

static void
on_auto_min_toggled (GtkCheckButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gboolean is_auto = gtk_check_button_get_active(btn);
    app->fixed_min = !is_auto;
    gtk_widget_set_sensitive(app->spin_min, !is_auto);
    app->force_redraw = TRUE;
}

static void
on_auto_max_toggled (GtkCheckButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gboolean is_auto = gtk_check_button_get_active(btn);
    app->fixed_max = !is_auto;
    gtk_widget_set_sensitive(app->spin_max, !is_auto);
    app->force_redraw = TRUE;
}

static void update_spin_steps(ViewerApp *app) {
    if (!app->spin_min || !app->spin_max) return;
    double range = fabs(app->max_val - app->min_val);
    double step = range * 0.2;
    if (step < 1e-9) step = 1.0;

    gtk_spin_button_set_increments(GTK_SPIN_BUTTON(app->spin_min), step, step * 5.0);
    gtk_spin_button_set_increments(GTK_SPIN_BUTTON(app->spin_max), step, step * 5.0);
}

static void
on_spin_min_changed (GtkSpinButton *spin, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->min_val = gtk_spin_button_get_value(spin);
    update_spin_steps(app);
    app->force_redraw = TRUE;
}

static void
on_spin_max_changed (GtkSpinButton *spin, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->max_val = gtk_spin_button_get_value(spin);
    update_spin_steps(app);
    app->force_redraw = TRUE;
}

static void
on_btn_autoscale_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    gboolean min_auto = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_min_auto));
    gboolean max_auto = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_max_auto));

    gboolean new_state;
    if (min_auto && max_auto) {
        new_state = FALSE; // Turn OFF auto
    } else {
        new_state = TRUE;  // Turn ON auto
    }

    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->check_min_auto), new_state);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->check_max_auto), new_state);

    app->force_redraw = TRUE;
}

static void
on_btn_reset_colorbar_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    // Reset Colormap windowing only
    app->cmap_min = 0.0;
    app->cmap_max = 1.0;

    app->force_redraw = TRUE;
}

static void
on_btn_reset_selection_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    if (app->image) {
        app->sel_x1 = 0;
        app->sel_y1 = 0;
        app->sel_x2 = app->image->md->size[0];
        app->sel_y2 = app->image->md->size[1];
        app->selection_active = TRUE;
    }
    app->force_redraw = TRUE;

    gtk_widget_queue_draw(app->selection_area);
    if (app->roi_image_area) gtk_widget_queue_draw(app->roi_image_area);
}


static void
on_btn_expand_roi_toggled (GtkCheckButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gboolean active = gtk_check_button_get_active(btn);
    if (active) {
        gtk_widget_set_visible(app->scrolled_roi, TRUE);
    } else {
        gtk_widget_set_visible(app->scrolled_roi, FALSE);
    }
}

static void
on_fit_window_toggled (GtkCheckButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gboolean new_state = gtk_check_button_get_active(btn);

    if (app->fit_window && !new_state) {
        // Switching from Fit to Manual.
        // Calculate the scale that was being used so we can preserve it.
        // We can't use get_image_screen_geometry directly because it relies on app->fit_window flag.

        double img_w = (double)(app->image ? app->image->md->size[0] : 1);
        double img_h = (double)(app->image ? app->image->md->size[1] : 1);
        int widget_w = gtk_widget_get_width(app->image_area);
        int widget_h = gtk_widget_get_height(app->image_area);

        if (img_w > 0 && img_h > 0 && widget_w > 0 && widget_h > 0) {
             double scale_x = (double)widget_w / img_w;
             double scale_y = (double)widget_h / img_h;
             app->zoom_factor = (scale_x < scale_y) ? scale_x : scale_y;
        }
    }

    app->fit_window = new_state;
    update_zoom_layout(app);
}

static void
on_dropdown_zoom_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    guint selected = gtk_drop_down_get_selected(dropdown);

    double zooms[] = {0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    if (selected < 7) {
        app->zoom_factor = zooms[selected];

        if (app->fit_window) {
            g_signal_handlers_block_by_func(app->btn_fit_window, on_fit_window_toggled, app);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(app->btn_fit_window), FALSE);
            app->fit_window = FALSE;
            g_signal_handlers_unblock_by_func(app->btn_fit_window, on_fit_window_toggled, app);
        }

        update_zoom_layout(app);
    }
}

static gboolean
on_scroll (GtkEventControllerScroll *controller,
           double                    dx,
           double                    dy,
           gpointer                  user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    double zoom_step = 1.1;
    if (dy > 0) {
        app->zoom_factor /= zoom_step;
    } else if (dy < 0) {
        app->zoom_factor *= zoom_step;
    }

    if (app->fit_window) {
        double off_x, off_y, scale;
        get_image_screen_geometry(app, &off_x, &off_y, &scale);
        app->zoom_factor = scale;
        if (dy > 0) app->zoom_factor /= zoom_step;
        else if (dy < 0) app->zoom_factor *= zoom_step;

        g_signal_handlers_block_by_func(app->btn_fit_window, on_fit_window_toggled, app);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(app->btn_fit_window), FALSE);
        app->fit_window = FALSE;
        g_signal_handlers_unblock_by_func(app->btn_fit_window, on_fit_window_toggled, app);
    }

    update_zoom_layout(app);
    return TRUE;
}

// Helpers for coordinate conversion
static void
get_image_screen_geometry(ViewerApp *app, double *offset_x, double *offset_y, double *scale) {
    if (!app->image) {
        *offset_x = 0; *offset_y = 0; *scale = 1.0;
        return;
    }

    int widget_w = gtk_widget_get_width(app->image_area);
    int widget_h = gtk_widget_get_height(app->image_area);

    if (widget_w <= 0 || widget_h <= 0) {
        *offset_x = 0; *offset_y = 0; *scale = 1.0;
        return;
    }

    double img_w = (double)app->image->md->size[0];
    double img_h = (double)app->image->md->size[1];

    if (img_w <= 0 || img_h <= 0) {
        *offset_x = 0; *offset_y = 0; *scale = 1.0;
        return;
    }

    double int_off_x, int_off_y;

    if (app->fit_window) {
        double scale_x = (double)widget_w / img_w;
        double scale_y = (double)widget_h / img_h;
        *scale = (scale_x < scale_y) ? scale_x : scale_y;

        double display_w = img_w * (*scale);
        double display_h = img_h * (*scale);

        int_off_x = (widget_w - display_w) / 2.0;
        int_off_y = (widget_h - display_h) / 2.0;
    } else {
        *scale = app->zoom_factor;

        double display_w = img_w * (*scale);
        double display_h = img_h * (*scale);

        if (widget_w > display_w) int_off_x = (widget_w - display_w) / 2.0;
        else int_off_x = 0;

        if (widget_h > display_h) int_off_y = (widget_h - display_h) / 2.0;
        else int_off_y = 0;
    }

    if (app->selection_area) {
        graphene_point_t p_in = {0, 0};
        graphene_point_t p_out;
        if (gtk_widget_compute_point(app->image_area, app->selection_area, &p_in, &p_out)) {
            *offset_x = int_off_x + p_out.x;
            *offset_y = int_off_y + p_out.y;
        } else {
            *offset_x = int_off_x;
            *offset_y = int_off_y;
        }
    } else {
        *offset_x = int_off_x;
        *offset_y = int_off_y;
    }
}

static void
update_zoom_layout(ViewerApp *app) {
    if (!app->image) return;

    double img_w = (double)app->image->md->size[0];
    double img_h = (double)app->image->md->size[1];

    char buf[64];

    if (app->fit_window) {
        gtk_widget_set_size_request(app->image_area, -1, -1);
        gtk_widget_set_halign(app->image_area, GTK_ALIGN_FILL);
        gtk_widget_set_valign(app->image_area, GTK_ALIGN_FILL);

        gtk_widget_set_hexpand(app->image_area, TRUE);
        gtk_widget_set_vexpand(app->image_area, TRUE);

        double off_x, off_y, scale;
        get_image_screen_geometry(app, &off_x, &off_y, &scale);
        snprintf(buf, sizeof(buf), "Zoom: %.1f%%", scale * 100.0);
        gtk_label_set_text(GTK_LABEL(app->lbl_zoom), buf);

        gtk_widget_queue_draw(app->selection_area);
        gtk_widget_queue_draw(app->image_area);
    } else {
        int req_w = (int)(img_w * app->zoom_factor);
        int req_h = (int)(img_h * app->zoom_factor);

        gtk_widget_set_size_request(app->image_area, req_w, req_h);

        gtk_widget_set_halign(app->image_area, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(app->image_area, GTK_ALIGN_CENTER);

        gtk_widget_set_hexpand(app->image_area, FALSE);
        gtk_widget_set_vexpand(app->image_area, FALSE);

        snprintf(buf, sizeof(buf), "Zoom: %.1f%%", app->zoom_factor * 100.0);
        gtk_label_set_text(GTK_LABEL(app->lbl_zoom), buf);

        gtk_widget_queue_draw(app->selection_area);
        gtk_widget_queue_draw(app->image_area);
    }
}

// Callback for resize
static void
on_image_area_resize (GtkWidget *widget, int width, int height, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    if (app->fit_window) {
        update_zoom_layout(app);
    }
}

// Drawing function for colorbar
static void
draw_colorbar_func (GtkDrawingArea *area,
                    cairo_t        *cr,
                    int             width,
                    int             height,
                    gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    int bar_width = 20;
    int bar_x = (width - bar_width) / 2;
    int margin_top = 20;
    int margin_bottom = 20;
    int bar_height = height - margin_top - margin_bottom;

    if (bar_height <= 0) return;

    // Draw Gradient respecting cmap_min/cmap_max
    // We iterate pixels vertically and calculate color

    for (int y = 0; y < bar_height; y++) {
        double t = 1.0 - (double)y / (double)bar_height; // 0 at bottom, 1 at top

        // Apply mapping
        double val = (t - app->cmap_min) / (app->cmap_max - app->cmap_min);
        if (val < 0) val = 0;
        if (val > 1) val = 1;

        val = apply_scaling(val, app->scale_type);

        double r, g, b;
        get_colormap_color(val, app->colormap_type, &r, &g, &b);

        cairo_set_source_rgb(cr, r, g, b);
        cairo_rectangle(cr, bar_x, margin_top + y, bar_width, 1);
        cairo_fill(cr);
    }

    // Draw Border
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    cairo_rectangle (cr, bar_x, margin_top, bar_width, bar_height);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);

    char buf[64];
    cairo_text_extents_t extents;

    int num_ticks = 5;
    for (int i = 0; i < num_ticks; i++) {
        double t = (double)i / (num_ticks - 1);
        double val = app->min_val + t * (app->max_val - app->min_val);
        double y = height - margin_bottom - t * bar_height;

        snprintf(buf, sizeof(buf), "%.3g", val);
        cairo_text_extents(cr, buf, &extents);

        // Draw tick
        cairo_move_to(cr, bar_x + bar_width, y);
        cairo_line_to(cr, bar_x + bar_width + 5, y);
        cairo_stroke(cr);

        // Draw Label
        cairo_move_to(cr, bar_x + bar_width + 8, y + extents.height/2 - 1);
        cairo_show_text(cr, buf);
    }

    // Draw Cursor Line
    if (app->colorbar_cursor_active) {
        double val = app->colorbar_cursor_val;
        double norm = (val - app->min_val) / (app->max_val - app->min_val);

        if (norm >= 0.0 && norm <= 1.0) {
            double y = height - margin_bottom - norm * bar_height;

            cairo_set_source_rgb(cr, 1, 0, 1); // Magenta
            cairo_set_line_width(cr, 2);
            cairo_move_to(cr, bar_x - 5, y);
            cairo_line_to(cr, bar_x + bar_width + 5, y);
            cairo_stroke(cr);
        }
    }
}

// Drawing function for Histogram
static void
draw_histogram_func (GtkDrawingArea *area,
                     cairo_t        *cr,
                     int             width,
                     int             height,
                     gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    if (!app->hist_data || app->hist_max_count == 0) return;

    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2); // Dark gray background
    cairo_paint(cr);

    int margin_left = 40;
    int margin_bottom = 20;
    int plot_w = width - margin_left;
    int plot_h = height - margin_bottom;

    if (plot_w <= 0 || plot_h <= 0) return;

    double bin_width = (double)plot_w / app->hist_bins;
    gboolean log_scale = (gtk_drop_down_get_selected(GTK_DROP_DOWN(app->dropdown_hist_scale)) == 1);

    double max_val = (double)app->hist_max_count;
    if (log_scale) max_val = log10(max_val + 1.0);

    // Calculate CDF
    double total_count = 0;
    for (int i = 0; i < app->hist_bins; i++) total_count += app->hist_data[i];

    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);

    for (int i = 0; i < app->hist_bins; ++i) {
        double val = (double)app->hist_data[i];
        if (log_scale) val = log10(val + 1.0);

        double h = (val / max_val) * plot_h;
        cairo_rectangle(cr, margin_left + i * bin_width, plot_h - h, bin_width, h);
        cairo_fill(cr);
    }

    // CDF (Red) and Inv CDF (Blue)
    if (total_count > 0) {
        double cum = 0;

        cairo_set_line_width(cr, 2);

        // Red Curve
        cairo_set_source_rgb(cr, 1, 0, 0);
        cum = 0;
        for (int i = 0; i < app->hist_bins; ++i) {
            cum += app->hist_data[i];
            double frac = cum / total_count;
            double x = margin_left + (i + 0.5) * bin_width;
            double y = plot_h * (1.0 - frac);
            if (i==0) cairo_move_to(cr, x, y);
            else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);

        // Blue Curve (Inv CDF)
        cairo_set_source_rgb(cr, 0, 0, 1);
        cum = 0;
        for (int i = 0; i < app->hist_bins; ++i) {
            cum += app->hist_data[i];
            double frac = cum / total_count;
            double inv_frac = 1.0 - frac;
            double x = margin_left + (i + 0.5) * bin_width;
            double y = plot_h * (1.0 - inv_frac);
            if (i==0) cairo_move_to(cr, x, y);
            else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
    }

    // Axes / Labels
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, margin_left, 0);
    cairo_line_to(cr, margin_left, plot_h);
    cairo_line_to(cr, width, plot_h);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);

    char buf[64];
    // Y Axis Max
    snprintf(buf, sizeof(buf), "%u", app->hist_max_count);
    cairo_move_to(cr, 2, 10);
    cairo_show_text(cr, buf);

    // X Axis Min
    snprintf(buf, sizeof(buf), "%.2g", app->current_min);
    cairo_move_to(cr, margin_left, height - 5);
    cairo_show_text(cr, buf);

    // X Axis Max
    cairo_text_extents_t extents;
    snprintf(buf, sizeof(buf), "%.2g", app->current_max);
    cairo_text_extents(cr, buf, &extents);
    cairo_move_to(cr, width - extents.width - 5, height - 5);
    cairo_show_text(cr, buf);

    // Mean/Median Lines
    double range = app->current_max - app->current_min;
    if (range > 0) {
        double mean_norm = (app->stats_mean - app->current_min) / range;
        if (mean_norm >= 0 && mean_norm <= 1.0) {
            cairo_set_source_rgb(cr, 0, 1, 1); // Cyan Mean
            double x = margin_left + mean_norm * plot_w;
            cairo_move_to(cr, x, 0);
            cairo_line_to(cr, x, plot_h);
            cairo_stroke(cr);
        }
        double median_norm = (app->stats_median - app->current_min) / range;
        if (median_norm >= 0 && median_norm <= 1.0) {
            cairo_set_source_rgb(cr, 1, 0, 1); // Magenta Median
            double x = margin_left + median_norm * plot_w;
            cairo_move_to(cr, x, 0);
            cairo_line_to(cr, x, plot_h);
            cairo_stroke(cr);
        }
    }

    // Overlay
    if (app->hist_mouse_active && app->hist_mouse_x >= margin_left) {
        int bin = (int)((app->hist_mouse_x - margin_left) / bin_width);
        if (bin >= 0 && bin < app->hist_bins) {
            double bin_val = app->current_min + (double)bin / app->hist_bins * range;
            uint32_t count = app->hist_data[bin];

            // Recompute CDF for this bin
            double cum = 0;
            for (int i = 0; i <= bin; ++i) cum += app->hist_data[i];
            double cdf = (total_count > 0) ? (cum / total_count * 100.0) : 0;
            double inv = 100.0 - cdf;

            // Draw Line
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_set_dash(cr, (double[]){4.0, 4.0}, 1, 0);
            double x = margin_left + (bin + 0.5) * bin_width;
            cairo_move_to(cr, x, 0);
            cairo_line_to(cr, x, plot_h);
            cairo_stroke(cr);
            cairo_set_dash(cr, NULL, 0, 0);

            // Draw Text Box
            snprintf(buf, sizeof(buf), "V: %.3g N: %u", bin_val, count);
            char buf2[64];
            snprintf(buf2, sizeof(buf2), "C: %.1f%% I: %.1f%%", cdf, inv);

            cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
            cairo_rectangle(cr, margin_left + 10, 10, 120, 30);
            cairo_fill(cr);

            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_move_to(cr, margin_left + 15, 22);
            cairo_show_text(cr, buf);

            cairo_set_source_rgb(cr, 1, 0.5, 0.5);
            cairo_move_to(cr, margin_left + 15, 35);
            char sub[32]; snprintf(sub, sizeof(sub), "C:%.1f%%", cdf);
            cairo_show_text(cr, sub);

            cairo_set_source_rgb(cr, 0.5, 0.5, 1);
            cairo_move_to(cr, margin_left + 75, 35);
            snprintf(sub, sizeof(sub), "I:%.1f%%", inv);
            cairo_show_text(cr, sub);
        }
    }
}

static void
update_trace_data(ViewerApp *app, double min, double max, double mean, double median, double p01, double p09,
                  uint32_t *hist, double hist_min, double hist_max) {
    if (!app->trace_active) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double t = (now.tv_sec - app->trace_start_time.tv_sec) +
               (now.tv_nsec - app->trace_start_time.tv_nsec) / 1e9;

    int idx = app->trace_head;
    app->trace_time[idx] = t;
    app->trace_min[idx] = min;
    app->trace_max[idx] = max;
    app->trace_mean[idx] = mean;
    app->trace_median[idx] = median;
    app->trace_p01[idx] = p01;
    app->trace_p09[idx] = p09;

    if (hist) {
        memcpy(app->trace_hist_data + idx * TRACE_HIST_BINS, hist, TRACE_HIST_BINS * sizeof(uint32_t));
        app->trace_hist_min[idx] = hist_min;
        app->trace_hist_max[idx] = hist_max;
    } else {
        memset(app->trace_hist_data + idx * TRACE_HIST_BINS, 0, TRACE_HIST_BINS * sizeof(uint32_t));
        app->trace_hist_min[idx] = 0;
        app->trace_hist_max[idx] = 1;
    }

    app->trace_head = (app->trace_head + 1) % TRACE_MAX_SAMPLES;
    if (app->trace_count < TRACE_MAX_SAMPLES) app->trace_count++;

    if (app->trace_area && gtk_widget_get_visible(app->trace_area)) {
        gtk_widget_queue_draw(app->trace_area);
    }
}

static void
draw_trace_func (GtkDrawingArea *area,
                 cairo_t        *cr,
                 int             width,
                 int             height,
                 gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    if (app->trace_count < 2) return;

    // Background
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    // Determine Time Range
    int head = app->trace_head;
    int count = app->trace_count;
    int tail = (head - count + TRACE_MAX_SAMPLES) % TRACE_MAX_SAMPLES;

    double t_end = app->trace_time[(head - 1 + TRACE_MAX_SAMPLES) % TRACE_MAX_SAMPLES];
    double t_start_req = t_end - app->trace_duration;

    // Determine Value Range (Y axis)
    double min_y = 1e30;
    double max_y = -1e30;

    // Iterating to find range and start index
    int start_idx = tail;
    int visible_count = 0;

    for (int i = 0; i < count; ++i) {
        int idx = (tail + i) % TRACE_MAX_SAMPLES;
        if (app->trace_time[idx] >= t_start_req) {
            if (visible_count == 0) start_idx = idx;
            visible_count++;

            if (app->trace_min[idx] < min_y) min_y = app->trace_min[idx];
            if (app->trace_max[idx] > max_y) max_y = app->trace_max[idx];
        }
    }

    if (visible_count < 2) return;

    // Padding
    double pad = (max_y - min_y) * 0.1;
    if (pad == 0) pad = 1.0;
    min_y -= pad;
    max_y += pad;

    // Draw Heatmap
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    uint32_t *pixels = (uint32_t*)cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf) / 4;

    // Clear surface
    memset(pixels, 0, height * stride * 4);

    double time_scale = width / app->trace_duration;
    double t_start_disp = t_end - app->trace_duration;

    // Iterate visible samples
    for (int i = 0; i < visible_count; ++i) {
        int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
        double t = app->trace_time[idx];
        int x = (int)((t - t_start_disp) * time_scale);

        if (x < 0 || x >= width) continue;

        double h_min = app->trace_hist_min[idx];
        double h_max = app->trace_hist_max[idx];
        double h_range = h_max - h_min;
        if (h_range <= 0) h_range = 1.0;

        uint32_t *hist = app->trace_hist_data + idx * TRACE_HIST_BINS;

        // Find max count in this column for normalization (optional, local contrast)
        // Or global? Local makes more sense for waterfall visibility.
        uint32_t col_max = 0;
        for (int b=0; b<TRACE_HIST_BINS; b++) if (hist[b] > col_max) col_max = hist[b];
        if (col_max == 0) col_max = 1;

        // Draw column
        for (int y = 0; y < height; ++y) {
            // Map Y to Value
            double val = max_y - (double)y / height * (max_y - min_y);

            // Map Value to Bin
            int bin = (int)((val - h_min) / h_range * (TRACE_HIST_BINS - 1));

            if (bin >= 0 && bin < TRACE_HIST_BINS) {
                uint32_t c = hist[bin];
                if (c > 0) {
                    double brightness = log10(c + 1) / log10(col_max + 1);
                    uint8_t v = (uint8_t)(brightness * 255.0);
                    // Grey scale: R=G=B=v
                    pixels[y * stride + x] = (255 << 24) | (v << 16) | (v << 8) | v;
                }
            }
        }

        // Fill gaps if x steps > 1? Simple nearest/points for now.
        // If trace is slow, x might jump. We can just draw points.
        // Or draw rectangles if x range is calculated.
        // Assuming high FPS, 1px width is fine.
    }

    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(surf);

    // Draw Lines
    #define MAP_X(t) ((t - (t_end - app->trace_duration)) / app->trace_duration * width)
    #define MAP_Y(v) (height - (v - min_y) / (max_y - min_y) * height)

    // Max - Red
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_set_line_width(cr, 1);
    for (int i = 0; i < visible_count; ++i) {
        int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
        double x = MAP_X(app->trace_time[idx]);
        double y = MAP_Y(app->trace_max[idx]);
        if (i==0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // Min - Blue
    cairo_set_source_rgb(cr, 0, 0, 1);
    for (int i = 0; i < visible_count; ++i) {
        int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
        double x = MAP_X(app->trace_time[idx]);
        double y = MAP_Y(app->trace_min[idx]);
        if (i==0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // Mean - Green
    cairo_set_source_rgb(cr, 0, 1, 0);
    for (int i = 0; i < visible_count; ++i) {
        int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
        double x = MAP_X(app->trace_time[idx]);
        double y = MAP_Y(app->trace_mean[idx]);
        if (i==0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // Median - Yellow
    cairo_set_source_rgb(cr, 1, 1, 0);
    for (int i = 0; i < visible_count; ++i) {
        int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
        double x = MAP_X(app->trace_time[idx]);
        double y = MAP_Y(app->trace_median[idx]);
        if (i==0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // P01 - Cyan
    cairo_set_source_rgb(cr, 0, 1, 1);
    for (int i = 0; i < visible_count; ++i) {
        int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
        double x = MAP_X(app->trace_time[idx]);
        double y = MAP_Y(app->trace_p01[idx]);
        if (i==0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // P09 - Magenta
    cairo_set_source_rgb(cr, 1, 0, 1);
    for (int i = 0; i < visible_count; ++i) {
        int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
        double x = MAP_X(app->trace_time[idx]);
        double y = MAP_Y(app->trace_p09[idx]);
        if (i==0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
}

static void
on_trace_toggled (GtkCheckButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->trace_active = gtk_check_button_get_active(btn);
    gtk_widget_set_visible(app->trace_area, app->trace_active);

    if (app->trace_active && app->trace_count == 0) {
        clock_gettime(CLOCK_MONOTONIC, &app->trace_start_time);
    }
}

static void
on_trace_dur_changed (GtkSpinButton *spin, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->trace_duration = gtk_spin_button_get_value(spin);
    gtk_widget_queue_draw(app->trace_area);
}

// Drawing function for ROI Expansion Area
static void
draw_roi_area_func (GtkDrawingArea *area,
                    cairo_t        *cr,
                    int             width,
                    int             height,
                    gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    // Clear background to black
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);

    if (!app->image || !app->display_buffer || !app->selection_active) {
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 20);
        cairo_move_to(cr, 20, 40);
        cairo_show_text(cr, "No Selection");
        return;
    }

    int roi_w = app->sel_x2 - app->sel_x1;
    int roi_h = app->sel_y2 - app->sel_y1;

    if (roi_w <= 0 || roi_h <= 0) return;

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, app->img_width);
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        app->display_buffer,
        CAIRO_FORMAT_RGB24,
        app->img_width,
        app->img_height,
        stride
    );

    // We want to draw the sub-rectangle (sel_x1, sel_y1, roi_w, roi_h)
    // stretched to fit (0, 0, width, height)

    double scale_x = (double)width / roi_w;
    double scale_y = (double)height / roi_h;
    // Typically expand to fill, but respect aspect ratio?
    // "Expand... to the same display size as the main full image" usually implies filling the widget.
    // Let's preserve aspect ratio to avoid distortion, centering it.
    double scale = (scale_x < scale_y) ? scale_x : scale_y;

    double draw_w = roi_w * scale;
    double draw_h = roi_h * scale;
    double off_x = (width - draw_w) / 2.0;
    double off_y = (height - draw_h) / 2.0;

    // Clip to widget
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_clip(cr);

    cairo_translate(cr, off_x, off_y);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, -app->sel_x1, -app->sel_y1);

    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);

    // Draw red outline around the ROI content in the expanded view
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_set_line_width(cr, 2.0 / scale); // Keep line width constant in screen pixels
    cairo_rectangle(cr, app->sel_x1, app->sel_y1, roi_w, roi_h);
    cairo_stroke(cr);

    cairo_surface_destroy(surface);
}

// Drawing function for Image Area (Nearest Neighbor)
static void
draw_image_area_func (GtkDrawingArea *area,
                      cairo_t        *cr,
                      int             width,
                      int             height,
                      gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    if (!app->image || !app->display_buffer) return;

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, app->img_width);
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        app->display_buffer,
        CAIRO_FORMAT_RGB24,
        app->img_width,
        app->img_height,
        stride
    );

    double off_x, off_y, scale;
    get_image_screen_geometry(app, &off_x, &off_y, &scale);

    double int_off_x = 0;
    double int_off_y = 0;

    if (app->fit_window) {
        double scale_x = (double)width / app->img_width;
        double scale_y = (double)height / app->img_height;
        scale = (scale_x < scale_y) ? scale_x : scale_y;

        double display_w = app->img_width * scale;
        double display_h = app->img_height * scale;

        int_off_x = (width - display_w) / 2.0;
        int_off_y = (height - display_h) / 2.0;
    } else {
        scale = app->zoom_factor;
        double display_w = app->img_width * scale;
        double display_h = app->img_height * scale;

        if (width > display_w) int_off_x = (width - display_w) / 2.0;
        if (height > display_h) int_off_y = (height - display_h) / 2.0;
    }

    cairo_translate(cr, int_off_x, int_off_y);
    cairo_scale(cr, scale, scale);

    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);

    cairo_surface_destroy(surface);
}

static void
widget_to_image_coords(ViewerApp *app, double wx, double wy, int *ix, int *iy) {
    double offset_x, offset_y, scale;
    get_image_screen_geometry(app, &offset_x, &offset_y, &scale);

    double lx = (wx - offset_x) / scale;
    double ly = (wy - offset_y) / scale;

    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    if (lx >= app->image->md->size[0]) lx = app->image->md->size[0] - 1;
    if (ly >= app->image->md->size[1]) ly = app->image->md->size[1] - 1;

    *ix = (int)lx;
    *iy = (int)ly;
}

// Drawing function for selection overlay
static void
draw_selection_func (GtkDrawingArea *area,
                     cairo_t        *cr,
                     int             width,
                     int             height,
                     gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    if (!app->is_dragging && !app->selection_active && !app->is_moving_selection) return;

    double x1, y1, x2, y2;
    double offset_x, offset_y, scale;
    get_image_screen_geometry(app, &offset_x, &offset_y, &scale);

    if (app->is_moving_selection) {
        x1 = app->sel_x1 * scale + offset_x;
        y1 = app->sel_y1 * scale + offset_y;
        x2 = app->sel_x2 * scale + offset_x;
        y2 = app->sel_y2 * scale + offset_y;
        x2 += scale;
        y2 += scale;
    } else if (app->is_dragging) {
        x1 = app->start_x;
        y1 = app->start_y;
        x2 = app->curr_x;
        y2 = app->curr_y;
    } else {
        x1 = app->sel_x1 * scale + offset_x;
        y1 = app->sel_y1 * scale + offset_y;
        x2 = app->sel_x2 * scale + offset_x;
        y2 = app->sel_y2 * scale + offset_y;
        x2 += scale;
        y2 += scale;
    }

    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_rectangle(cr, x1, y1, x2 - x1, y2 - y1);
    cairo_stroke(cr);
}

// Gesture callbacks (Left Click: ROI)
static void
drag_begin (GtkGestureDrag *gesture,
            double          x,
            double          y,
            gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    if (button == GDK_BUTTON_SECONDARY) { // Right Click
        app->is_adjusting_contrast = TRUE;
        app->contrast_start_x = x;
        app->contrast_start_y = y;
        app->contrast_start_cmap_min = app->cmap_min;
        app->contrast_start_cmap_max = app->cmap_max;
        return;
    }

    if (button == GDK_BUTTON_PRIMARY) { // Left Click
        GtkEventController *controller = GTK_EVENT_CONTROLLER(gesture);
        GdkEvent *event = gtk_event_controller_get_current_event(controller);
        GdkModifierType modifiers = gdk_event_get_modifier_state(event);

        if (!(modifiers & GDK_SHIFT_MASK)) return;
        if (!app->image) return;

        int ix, iy;
        widget_to_image_coords(app, x, y, &ix, &iy);

        gboolean is_full_frame = (app->sel_x1 <= 0 && app->sel_y1 <= 0 &&
                                  app->sel_x2 >= app->image->md->size[0] &&
                                  app->sel_y2 >= app->image->md->size[1]);

        if (app->selection_active && !is_full_frame &&
            ix >= app->sel_x1 && ix <= app->sel_x2 &&
            iy >= app->sel_y1 && iy <= app->sel_y2) {

            app->is_moving_selection = TRUE;
            app->is_dragging = FALSE;
            app->sel_orig_x1 = app->sel_x1;
            app->sel_orig_y1 = app->sel_y1;
            app->sel_orig_x2 = app->sel_x2;
            app->sel_orig_y2 = app->sel_y2;
        } else {
            app->is_moving_selection = FALSE;
            app->is_dragging = TRUE;
            app->selection_active = FALSE;
            gtk_widget_set_visible(app->box_stats, FALSE);
        }

        app->start_x = x;
        app->start_y = y;
        app->curr_x = x;
        app->curr_y = y;

        app->force_redraw = TRUE;
        gtk_widget_queue_draw(app->selection_area);
    }
}

static void
drag_update (GtkGestureDrag *gesture,
             double          offset_x,
             double          offset_y,
             gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    if (app->is_adjusting_contrast) {
        int width = gtk_widget_get_width(app->selection_area);
        int height = gtk_widget_get_height(app->selection_area);
        if (width <= 0) width = 1;
        if (height <= 0) height = 1;

        double start_center = (app->contrast_start_cmap_max + app->contrast_start_cmap_min) / 2.0;
        double start_width = (app->contrast_start_cmap_max - app->contrast_start_cmap_min);
        // Avoid zero width
        if (fabs(start_width) < 1e-6) start_width = 0.01;

        // Scale factors logic (approximation of DS9 feel)
        double range = start_width;

        // Horizontal: Shift Center
        double shift = (offset_x / (double)width) * range; // simple linear shift
        double new_center = start_center - shift; // Invert? Try standard

        // Vertical: Scale Width (Contrast)
        // Drag Up -> Smaller Width (Higher Contrast)
        // Drag Down -> Larger Width (Lower Contrast)
        double scale_factor = exp( offset_y / (double)height * 4.0 );
        double new_width = start_width * scale_factor;

        double new_cmin = new_center - new_width / 2.0;
        double new_cmax = new_center + new_width / 2.0;

        // Optional: Clamp? DS9 allows going way out.
        // But for display logic 0..1 is the range.
        // Let's not clamp too strictly, but maybe ensure sane range.

        app->cmap_min = new_cmin;
        app->cmap_max = new_cmax;

        app->force_redraw = TRUE;
        return;
    }

    if (app->is_moving_selection) {
        double off_x, off_y, scale;
        get_image_screen_geometry(app, &off_x, &off_y, &scale);

        int idx = (int)(offset_x / scale);
        int idy = (int)(offset_y / scale);

        int w = app->sel_orig_x2 - app->sel_orig_x1;
        int h = app->sel_orig_y2 - app->sel_orig_y1;
        int img_w = app->image->md->size[0];
        int img_h = app->image->md->size[1];

        app->sel_x1 = app->sel_orig_x1 + idx;
        app->sel_y1 = app->sel_orig_y1 + idy;

        if (app->sel_x1 < 0) app->sel_x1 = 0;
        if (app->sel_y1 < 0) app->sel_y1 = 0;
        if (app->sel_x1 + w >= img_w) app->sel_x1 = img_w - 1 - w;
        if (app->sel_y1 + h >= img_h) app->sel_y1 = img_h - 1 - h;

        app->sel_x2 = app->sel_x1 + w;
        app->sel_y2 = app->sel_y1 + h;

        app->force_redraw = TRUE;
    } else if (app->is_dragging) {
        app->curr_x = app->start_x + offset_x;
        app->curr_y = app->start_y + offset_y;
    }
    gtk_widget_queue_draw(app->selection_area);
    if (app->roi_image_area && gtk_widget_get_visible(app->scrolled_roi)) {
        gtk_widget_queue_draw(app->roi_image_area);
    }
}

static void
drag_end (GtkGestureDrag *gesture,
          double          offset_x,
          double          offset_y,
          gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    if (app->is_adjusting_contrast) {
        app->is_adjusting_contrast = FALSE;
        return;
    }

    if (app->is_moving_selection) {
        app->is_moving_selection = FALSE;
        app->selection_active = TRUE;
    } else if (app->is_dragging) {
        app->is_dragging = FALSE;
        app->curr_x = app->start_x + offset_x;
        app->curr_y = app->start_y + offset_y;

        if (fabs(offset_x) < 2 && fabs(offset_y) < 2) {
            app->selection_active = FALSE;
            gtk_widget_set_visible(app->box_stats, FALSE);
        } else {
            app->selection_active = TRUE;

            int ix1, iy1, ix2, iy2;
            widget_to_image_coords(app, app->start_x, app->start_y, &ix1, &iy1);
            widget_to_image_coords(app, app->curr_x, app->curr_y, &ix2, &iy2);

            app->sel_x1 = (ix1 < ix2) ? ix1 : ix2;
            app->sel_x2 = (ix1 < ix2) ? ix2 : ix1;
            app->sel_y1 = (iy1 < iy2) ? iy1 : iy2;
            app->sel_y2 = (iy1 < iy2) ? iy2 : iy1;

            gtk_widget_set_visible(app->box_stats, TRUE);
            if (gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_histogram))) {
                gtk_widget_set_visible(app->histogram_area, TRUE);
            }
        }
    }

    app->force_redraw = TRUE;
    gtk_widget_queue_draw(app->selection_area);
    if (app->roi_image_area && gtk_widget_get_visible(app->scrolled_roi)) {
        gtk_widget_queue_draw(app->roi_image_area);
    }
}

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static void
calculate_roi_stats(ViewerApp *app, void *raw_data, int width, int height, uint8_t datatype) {
    if (!app->selection_active) return;

    int x1 = app->sel_x1;
    int x2 = app->sel_x2 + 1;
    int y1 = app->sel_y1;
    int y2 = app->sel_y2 + 1;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > width) x2 = width;
    if (y2 > height) y2 = height;

    int roi_w = x2 - x1;
    int roi_h = y2 - y1;

    if (roi_w <= 0 || roi_h <= 0) return;

    size_t count = roi_w * roi_h;
    double *values = (double *)malloc(count * sizeof(double));
    if (!values) return;

    size_t idx = 0;
    double sum = 0;
    double min_v = 1e30;
    double max_v = -1e30;

    // First pass for min/max/sum/values
    for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
            int i = y * width + x;
            double val = 0;
            switch (datatype) {
                case _DATATYPE_FLOAT: val = ((float*)raw_data)[i]; break;
                case _DATATYPE_DOUBLE: val = ((double*)raw_data)[i]; break;
                case _DATATYPE_UINT8: val = ((uint8_t*)raw_data)[i]; break;
                case _DATATYPE_INT16: val = ((int16_t*)raw_data)[i]; break;
                case _DATATYPE_UINT16: val = ((uint16_t*)raw_data)[i]; break;
                case _DATATYPE_INT32: val = ((int32_t*)raw_data)[i]; break;
                case _DATATYPE_UINT32: val = ((uint32_t*)raw_data)[i]; break;
                default: val = 0; break;
            }
            values[idx++] = val;
            sum += val;
            if (val < min_v) min_v = val;
            if (val > max_v) max_v = val;
        }
    }

    // Calculate Histogram if enabled OR if trace is active (for heatmap)
    gboolean show_hist = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_histogram));
    gboolean trace_active = app->trace_active;

    // Ensure hist data allocation if needed
    if ((show_hist || trace_active) && !app->hist_data) {
        app->hist_bins = 256; // Fixed for now
        app->hist_data = (guint32*)calloc(app->hist_bins, sizeof(guint32));
    }

    if (show_hist || trace_active) {
        memset(app->hist_data, 0, app->hist_bins * sizeof(guint32));
        app->hist_max_count = 0;

        // Use Global Display Range for Histogram
        double disp_min = app->current_min;
        double disp_max = app->current_max;
        double range = disp_max - disp_min;

        if (range <= 0) range = 1.0;

        for (size_t i = 0; i < count; ++i) {
            int bin = (int)((values[i] - disp_min) / range * (app->hist_bins - 1));
            // Clamp to histogram bounds
            if (bin < 0) bin = 0;
            if (bin >= app->hist_bins) bin = app->hist_bins - 1;

            app->hist_data[bin]++;
            if (app->hist_data[bin] > app->hist_max_count) app->hist_max_count = app->hist_data[bin];
        }
        if (show_hist) gtk_widget_queue_draw(app->histogram_area);
    }

    double mean = sum / count;

    qsort(values, count, sizeof(double), compare_doubles);

    double median = values[count / 2];
    double p01 = values[(size_t)(count * 0.1)];
    double p09 = values[(size_t)(count * 0.9)];

    app->stats_mean = mean;
    app->stats_median = median;

    free(values);

    char buf[64];
    snprintf(buf, sizeof(buf), "%.4g", min_v);
    gtk_editable_set_text(GTK_EDITABLE(app->entry_stat_min), buf);

    snprintf(buf, sizeof(buf), "%.4g", max_v);
    gtk_editable_set_text(GTK_EDITABLE(app->entry_stat_max), buf);

    snprintf(buf, sizeof(buf), "%.4g", mean);
    gtk_editable_set_text(GTK_EDITABLE(app->entry_stat_mean), buf);

    snprintf(buf, sizeof(buf), "%.4g", median);
    gtk_editable_set_text(GTK_EDITABLE(app->entry_stat_median), buf);

    snprintf(buf, sizeof(buf), "%.4g", p01);
    gtk_editable_set_text(GTK_EDITABLE(app->entry_stat_p01), buf);

    snprintf(buf, sizeof(buf), "%.4g", p09);
    gtk_editable_set_text(GTK_EDITABLE(app->entry_stat_p09), buf);

    update_trace_data(app, min_v, max_v, mean, median, p01, p09,
                      app->hist_data, app->current_min, app->current_max);

    // New Stats
    snprintf(buf, sizeof(buf), "%zu", count);
    gtk_editable_set_text(GTK_EDITABLE(app->entry_stat_npix), buf);

    snprintf(buf, sizeof(buf), "%.4g", sum);
    gtk_editable_set_text(GTK_EDITABLE(app->entry_stat_sum), buf);

    if (app->frame_stats) {
        snprintf(buf, sizeof(buf), "ROI Stats [ %d x %d at %.1f, %.1f ]",
                 roi_w, roi_h, x1 + roi_w/2.0, y1 + roi_h/2.0);
        gtk_frame_set_label(GTK_FRAME(app->frame_stats), buf);
    }
}

static void
draw_image (ViewerApp *app)
{
    if (!app->image || !app->image->array.raw) return;

    int width = app->image->md->size[0];
    int height = app->image->md->size[1];
    uint8_t datatype = app->image->md->datatype;
    size_t element_size = ImageStreamIO_typesize(datatype);
    size_t frame_size = width * height * element_size;

    // Manage Raw Buffer
    if (!app->raw_buffer || app->raw_buffer_size < frame_size) {
        if (app->raw_buffer) free(app->raw_buffer);
        app->raw_buffer = malloc(frame_size);
        app->raw_buffer_size = frame_size;
    }

    // If not paused, copy fresh data. If paused, keep existing data.
    if (!app->paused) {
        void *src_ptr = NULL;
        if (app->image->md->imagetype & CIRCULAR_BUFFER) {
            if (app->image->md->naxis == 3) {
                uint64_t slice_index = app->image->md->cnt1 % app->image->md->size[2];
                src_ptr = (char*)app->image->array.raw + (slice_index * frame_size);
            } else {
                 src_ptr = app->image->array.raw;
            }
        } else {
            src_ptr = app->image->array.raw;
        }

        if (src_ptr) {
            memcpy(app->raw_buffer, src_ptr, frame_size);
        }
    }

    void *raw_data = app->raw_buffer;
    if (!raw_data) return;

    app->img_width = width;
    app->img_height = height;

    // Calculate Stats if selection active (and we are running, or forced update)
    // When paused, we might still want to update stats if ROI moves, so we allow it.
    if (app->selection_active && gtk_check_button_get_active(GTK_CHECK_BUTTON(app->btn_stats_update))) {
        calculate_roi_stats(app, raw_data, width, height, datatype);
    }

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, width);
    size_t required_size = stride * height;

    if (!app->display_buffer || app->display_buffer_size < required_size) {
        if (app->display_buffer) free(app->display_buffer);
        app->display_buffer = malloc(required_size);
        if (!app->display_buffer) return;
        app->display_buffer_size = required_size;
    }

    guchar *pixels = app->display_buffer;

    double min_val = 1e30;
    double max_val = -1e30;

    gboolean need_scan = (!app->fixed_min || !app->fixed_max);

    if (need_scan) {
        int x_start = 0, y_start = 0;
        int x_end = width, y_end = height;

        if (app->selection_active) {
            x_start = app->sel_x1;
            x_end = app->sel_x2 + 1;
            y_start = app->sel_y1;
            y_end = app->sel_y2 + 1;
            if (x_start < 0) x_start = 0;
            if (y_start < 0) y_start = 0;
            if (x_end > width) x_end = width;
            if (y_end > height) y_end = height;
        }

        for (int y = y_start; y < y_end; y++) {
            for (int x = x_start; x < x_end; x++) {
                int i = y * width + x;
                double val = 0;
                switch (datatype) {
                    case _DATATYPE_FLOAT: val = ((float*)raw_data)[i]; break;
                    case _DATATYPE_DOUBLE: val = ((double*)raw_data)[i]; break;
                    case _DATATYPE_UINT8: val = ((uint8_t*)raw_data)[i]; break;
                    case _DATATYPE_INT16: val = ((int16_t*)raw_data)[i]; break;
                    case _DATATYPE_UINT16: val = ((uint16_t*)raw_data)[i]; break;
                    case _DATATYPE_INT32: val = ((int32_t*)raw_data)[i]; break;
                    case _DATATYPE_UINT32: val = ((uint32_t*)raw_data)[i]; break;
                    default: val = 0; break;
                }

                if (val < min_val) min_val = val;
                if (val > max_val) max_val = val;
            }
        }

        if (min_val > max_val) {
            min_val = 0;
            max_val = 1;
        }
    }

    if (app->fixed_min) min_val = app->min_val;
    if (app->fixed_max) max_val = app->max_val;

    if (max_val == min_val) max_val = min_val + 1.0;

    app->current_min = min_val;
    app->current_max = max_val;

    if (app->colorbar) {
        gtk_widget_queue_draw(app->colorbar);
    }

    if (!app->fixed_min && app->spin_min) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_min), min_val);
    }
    if (!app->fixed_max && app->spin_max) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_max), max_val);
    }

    update_spin_steps(app);

    // Effective min/max for colormap
    double eff_min = min_val + app->cmap_min * (max_val - min_val);
    double eff_max = min_val + app->cmap_max * (max_val - min_val);

    if (fabs(eff_max - eff_min) < 1e-9) eff_max = eff_min + 1.0;

    // Populate display buffer
    for (int y = 0; y < height; y++) {
        uint32_t *row = (uint32_t*)(pixels + y * stride);
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
             double val = 0;
            if (datatype == _DATATYPE_FLOAT) {
                val = ((float*)raw_data)[idx];
            } else if (datatype == _DATATYPE_DOUBLE) {
                val = ((double*)raw_data)[idx];
            } else if (datatype == _DATATYPE_UINT8) {
                val = ((uint8_t*)raw_data)[idx];
            } else if (datatype == _DATATYPE_INT16) {
                val = ((int16_t*)raw_data)[idx];
            } else if (datatype == _DATATYPE_UINT16) {
                val = ((uint16_t*)raw_data)[idx];
            } else if (datatype == _DATATYPE_INT32) {
                val = ((int32_t*)raw_data)[idx];
            } else if (datatype == _DATATYPE_UINT32) {
                val = ((uint32_t*)raw_data)[idx];
            }

            double norm = (val - eff_min) / (eff_max - eff_min);
            if (norm < 0) norm = 0;
            if (norm > 1) norm = 1;

            norm = apply_scaling(norm, app->scale_type);

            double r, g, b;
            get_colormap_color(norm, app->colormap_type, &r, &g, &b);

            uint8_t br = (uint8_t)(r * 255.0);
            uint8_t bg = (uint8_t)(g * 255.0);
            uint8_t bb = (uint8_t)(b * 255.0);

            row[x] = (255 << 24) | (br << 16) | (bg << 8) | bb;
        }
    }

    gtk_widget_queue_draw(app->image_area);
    if (app->roi_image_area && gtk_widget_get_visible(app->scrolled_roi)) {
        gtk_widget_queue_draw(app->roi_image_area);
    }
}

gboolean
update_display (gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    if (!app->image) {
        app->image = (IMAGE*) malloc(sizeof(IMAGE));
        if (ImageStreamIO_openIm(app->image, app->image_name) != IMAGESTREAMIO_SUCCESS) {
             free(app->image);
             app->image = NULL;
             return G_SOURCE_CONTINUE;
        }
        printf("Connected to stream: %s\n", app->image_name);
        app->fit_window = TRUE;
        app->cmap_min = 0.0;
        app->cmap_max = 1.0;
        if (app->btn_fit_window) gtk_check_button_set_active(GTK_CHECK_BUTTON(app->btn_fit_window), TRUE);
        app->force_redraw = TRUE;

        // Init ROI to full frame
        app->sel_x1 = 0;
        app->sel_y1 = 0;
        app->sel_x2 = app->image->md->size[0];
        app->sel_y2 = app->image->md->size[1];
        app->selection_active = TRUE;
        gtk_widget_set_visible(app->box_stats, TRUE);

        // Set Window Title
        GtkWindow *win = GTK_WINDOW(gtk_widget_get_root(app->image_area));
        if (win) {
            char title[256];
            snprintf(title, sizeof(title), "MilkShmimView: %s [%s %dx%d]",
                     app->image_name,
                     get_datatype_string(app->image->md->datatype),
                     (int)app->image->md->size[0], (int)app->image->md->size[1]);
            gtk_window_set_title(win, title);
        }
    }

    // FPS Estimation
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double dt = (now.tv_sec - app->last_fps_time.tv_sec) + (now.tv_nsec - app->last_fps_time.tv_nsec) / 1e9;
    if (dt >= 1.0) {
        double fps = (double)(app->image->md->cnt0 - app->last_fps_cnt) / dt;
        if (fps < 0) fps = 0;
        char buf_fps[64];
        snprintf(buf_fps, sizeof(buf_fps), "%.1f Hz", fps);
        gtk_label_set_text(GTK_LABEL(app->lbl_fps_est), buf_fps);
        app->last_fps_time = now;
        app->last_fps_cnt = app->image->md->cnt0;
    }

    static uint64_t last_cnt0 = 0;

    gboolean new_frame = (!app->paused && app->image->md->cnt0 != last_cnt0);

    if (new_frame || app->force_redraw) {
        if (new_frame && app->image->md->write) {
             return G_SOURCE_CONTINUE;
        }

        if (new_frame) last_cnt0 = app->image->md->cnt0;
        app->force_redraw = FALSE;
        draw_image(app);

        // Update counter label (only when image updates)
        char buf[64];
        snprintf(buf, sizeof(buf), "Counter: %lu", last_cnt0);
        gtk_label_set_text(GTK_LABEL(app->lbl_counter), buf);
    }

    return G_SOURCE_CONTINUE;
}

static void
activate (GtkApplication *app,
          gpointer        user_data)
{
    ViewerApp *viewer = (ViewerApp *)user_data;
    GtkWidget *window;
    GtkWidget *hbox;
    GtkWidget *vbox_controls;
    GtkWidget *hbox_right;
    GtkWidget *scrolled_window;
    GtkWidget *overlay;
    GtkWidget *label;
    GtkWidget *btn_autoscale;
    GtkWidget *btn_reset;
    GtkWidget *btn_reset_colorbar;
    GtkGesture *drag_controller;
    GtkEventController *scroll_controller;
    GtkWidget *frame_stats;

    window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (window), "ImageStreamIO Viewer");
    gtk_window_set_default_size (GTK_WINDOW (window), 1000, 700);

    GtkWidget *paned_root = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned_root), 200);
    gtk_window_set_child(GTK_WINDOW(window), paned_root);

    // Sidebar Controls (Left)
    viewer->vbox_controls = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request (viewer->vbox_controls, 160, -1);
    gtk_widget_set_hexpand(viewer->vbox_controls, FALSE);
    gtk_widget_set_margin_start (viewer->vbox_controls, 10);
    gtk_widget_set_margin_end (viewer->vbox_controls, 10);
    gtk_widget_set_margin_top (viewer->vbox_controls, 10);
    gtk_widget_set_margin_bottom (viewer->vbox_controls, 10);

    gtk_paned_set_start_child(GTK_PANED(paned_root), viewer->vbox_controls);
    gtk_paned_set_resize_start_child(GTK_PANED(paned_root), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned_root), FALSE);

    // Hide Button
    GtkWidget *btn_hide = gtk_button_new_with_label("Hide Panel");
    g_signal_connect(btn_hide, "clicked", G_CALLBACK(on_hide_controls_clicked), viewer);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), btn_hide);

    // Counter
    viewer->lbl_counter = gtk_label_new("Counter: 0");
    gtk_widget_set_halign(viewer->lbl_counter, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), viewer->lbl_counter);

    GtkWidget *row;

    // FPS Control
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);
    label = gtk_label_new("FPS");
    gtk_box_append(GTK_BOX(row), label);
    const char *fps_opts[] = {"1 Hz", "2 Hz", "5 Hz", "10 Hz", "25 Hz", "50 Hz", "100 Hz", NULL};
    viewer->dropdown_fps = gtk_drop_down_new_from_strings(fps_opts);
    gtk_widget_set_hexpand(viewer->dropdown_fps, TRUE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_fps), 4); // 25Hz default
    g_signal_connect(viewer->dropdown_fps, "notify::selected", G_CALLBACK(on_fps_changed), viewer);
    gtk_box_append(GTK_BOX(row), viewer->dropdown_fps);

    viewer->lbl_fps_est = gtk_label_new("0.0 Hz");
    gtk_box_append(GTK_BOX(row), viewer->lbl_fps_est);

    // Pause Button
    viewer->btn_pause = gtk_toggle_button_new_with_label("Pause");
    g_signal_connect(viewer->btn_pause, "toggled", G_CALLBACK(on_pause_toggled), viewer);
    gtk_box_append(GTK_BOX(row), viewer->btn_pause);

    // Cmap & Scale Row
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);

    label = gtk_label_new("Cmap");
    gtk_box_append(GTK_BOX(row), label);
    const char *cmap_opts[] = {"Grey", "Red", "Green", "Blue", "Heat", "Cool", "Rainbow", "A", "B", NULL};
    viewer->dropdown_cmap = gtk_drop_down_new_from_strings(cmap_opts);
    gtk_widget_set_hexpand(viewer->dropdown_cmap, TRUE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_cmap), COLORMAP_GREY);
    g_signal_connect(viewer->dropdown_cmap, "notify::selected", G_CALLBACK(on_cmap_changed), viewer);
    gtk_box_append(GTK_BOX(row), viewer->dropdown_cmap);

    label = gtk_label_new("Scale");
    gtk_box_append(GTK_BOX(row), label);
    const char *scale_opts[] = {"Linear", "Log", "Sqrt", "Square", "Asinh", NULL};
    viewer->dropdown_scale = gtk_drop_down_new_from_strings(scale_opts);
    gtk_widget_set_hexpand(viewer->dropdown_scale, TRUE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_scale), SCALE_LINEAR);
    g_signal_connect(viewer->dropdown_scale, "notify::selected", G_CALLBACK(on_scale_changed), viewer);
    gtk_box_append(GTK_BOX(row), viewer->dropdown_scale);

    // Zoom Controls
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);

    // Zoom Dropdown
    const char *zoom_levels[] = {"1/8x", "1/4x", "1/2x", "1x", "2x", "4x", "8x", NULL};
    viewer->dropdown_zoom = gtk_drop_down_new_from_strings (zoom_levels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_zoom), 3);
    g_signal_connect (viewer->dropdown_zoom, "notify::selected", G_CALLBACK (on_dropdown_zoom_changed), viewer);
    gtk_box_append (GTK_BOX (row), viewer->dropdown_zoom);

    // Fit Toggle
    viewer->btn_fit_window = gtk_check_button_new_with_label ("fit");
    gtk_check_button_set_active (GTK_CHECK_BUTTON (viewer->btn_fit_window), TRUE);
    g_signal_connect (viewer->btn_fit_window, "toggled", G_CALLBACK (on_fit_window_toggled), viewer);
    gtk_box_append (GTK_BOX (row), viewer->btn_fit_window);

    // Zoom Label
    viewer->lbl_zoom = gtk_label_new ("100%");
    gtk_box_append (GTK_BOX (row), viewer->lbl_zoom);

    // Selection Controls
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);

    label = gtk_label_new ("ROI");
    gtk_widget_set_size_request(label, 60, -1);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (row), label);

    viewer->btn_expand_roi = gtk_check_button_new_with_label("show");
    g_signal_connect(viewer->btn_expand_roi, "toggled", G_CALLBACK(on_btn_expand_roi_toggled), viewer);
    gtk_box_append(GTK_BOX(row), viewer->btn_expand_roi);

    btn_reset = gtk_button_new_with_label ("reset");
    g_signal_connect (btn_reset, "clicked", G_CALLBACK (on_btn_reset_selection_clicked), viewer);
    gtk_box_append (GTK_BOX (row), btn_reset);

    // Auto Scale Row
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);

    btn_autoscale = gtk_button_new_with_label ("Auto Scale");
    g_signal_connect (btn_autoscale, "clicked", G_CALLBACK (on_btn_autoscale_clicked), viewer);
    gtk_box_append (GTK_BOX (row), btn_autoscale);

    viewer->check_min_auto = gtk_check_button_new_with_label ("Min");
    g_signal_connect (viewer->check_min_auto, "toggled", G_CALLBACK (on_auto_min_toggled), viewer);
    gtk_box_append (GTK_BOX (row), viewer->check_min_auto);

    viewer->check_max_auto = gtk_check_button_new_with_label ("Max");
    g_signal_connect (viewer->check_max_auto, "toggled", G_CALLBACK (on_auto_max_toggled), viewer);
    gtk_box_append (GTK_BOX (row), viewer->check_max_auto);

    // Manual Levels Row
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);

    viewer->spin_min = gtk_spin_button_new_with_range (-1e20, 1e20, 1.0);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (viewer->spin_min), 2);
    gtk_widget_set_hexpand(viewer->spin_min, TRUE);
    g_signal_connect (viewer->spin_min, "value-changed", G_CALLBACK (on_spin_min_changed), viewer);
    gtk_box_append (GTK_BOX (row), viewer->spin_min);

    viewer->spin_max = gtk_spin_button_new_with_range (-1e20, 1e20, 1.0);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (viewer->spin_max), 2);
    gtk_widget_set_hexpand(viewer->spin_max, TRUE);
    g_signal_connect (viewer->spin_max, "value-changed", G_CALLBACK (on_spin_max_changed), viewer);
    gtk_box_append (GTK_BOX (row), viewer->spin_max);

    // Middle Paned (Images vs Right Panel)
    GtkWidget *paned_mid = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_end_child(GTK_PANED(paned_root), paned_mid);
    gtk_paned_set_resize_end_child(GTK_PANED(paned_root), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned_root), FALSE);

    // Images Paned (Main Image vs ROI Image)
    viewer->paned_images = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(paned_mid), viewer->paned_images);
    gtk_paned_set_resize_start_child(GTK_PANED(paned_mid), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned_mid), FALSE);

    gtk_paned_set_position(GTK_PANED(viewer->paned_images), 400);

    // Main Image
    scrolled_window = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (scrolled_window, TRUE);
    gtk_widget_set_hexpand (scrolled_window, TRUE);

    gtk_paned_set_start_child(GTK_PANED(viewer->paned_images), scrolled_window);
    gtk_paned_set_resize_start_child(GTK_PANED(viewer->paned_images), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(viewer->paned_images), TRUE);

    scroll_controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect (scroll_controller, "scroll", G_CALLBACK (on_scroll), viewer);
    gtk_widget_add_controller (scrolled_window, scroll_controller);

    overlay = gtk_overlay_new();
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), overlay);

    // ROI Image (Hidden by default)
    viewer->scrolled_roi = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(viewer->scrolled_roi, TRUE);
    gtk_widget_set_hexpand(viewer->scrolled_roi, TRUE);
    gtk_widget_set_visible(viewer->scrolled_roi, FALSE);

    gtk_paned_set_end_child(GTK_PANED(viewer->paned_images), viewer->scrolled_roi);
    gtk_paned_set_resize_end_child(GTK_PANED(viewer->paned_images), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(viewer->paned_images), TRUE);

    GtkWidget *overlay_roi = gtk_overlay_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(viewer->scrolled_roi), overlay_roi);

    viewer->roi_image_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(viewer->roi_image_area, TRUE);
    gtk_widget_set_vexpand(viewer->roi_image_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(viewer->roi_image_area), draw_roi_area_func, viewer, NULL);
    gtk_overlay_set_child(GTK_OVERLAY(overlay_roi), viewer->roi_image_area);

    viewer->lbl_pixel_info_roi = gtk_label_new("");
    gtk_widget_set_halign(viewer->lbl_pixel_info_roi, GTK_ALIGN_END);
    gtk_widget_set_valign(viewer->lbl_pixel_info_roi, GTK_ALIGN_END);
    gtk_widget_set_margin_end(viewer->lbl_pixel_info_roi, 5);
    gtk_widget_set_margin_bottom(viewer->lbl_pixel_info_roi, 5);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay_roi), viewer->lbl_pixel_info_roi);

    GtkEventController *roi_motion = gtk_event_controller_motion_new();
    g_signal_connect(roi_motion, "motion", G_CALLBACK(on_motion_roi), viewer);
    g_signal_connect(roi_motion, "leave", G_CALLBACK(on_leave), viewer);
    gtk_widget_add_controller(viewer->roi_image_area, roi_motion);

    // Using DrawingArea instead of Picture for manual nearest-neighbor drawing
    viewer->image_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(viewer->image_area), draw_image_area_func, viewer, NULL);
    g_signal_connect(viewer->image_area, "resize", G_CALLBACK(on_image_area_resize), viewer);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), viewer->image_area);

    // Selection Overlay
    viewer->selection_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(viewer->selection_area, TRUE);
    gtk_widget_set_vexpand(viewer->selection_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(viewer->selection_area), draw_selection_func, viewer, NULL);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), viewer->selection_area);

    viewer->lbl_pixel_info_main = gtk_label_new("");
    gtk_widget_set_halign(viewer->lbl_pixel_info_main, GTK_ALIGN_END);
    gtk_widget_set_valign(viewer->lbl_pixel_info_main, GTK_ALIGN_END);
    gtk_widget_set_margin_end(viewer->lbl_pixel_info_main, 5);
    gtk_widget_set_margin_bottom(viewer->lbl_pixel_info_main, 5);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), viewer->lbl_pixel_info_main);

    GtkEventController *main_motion = gtk_event_controller_motion_new();
    g_signal_connect(main_motion, "motion", G_CALLBACK(on_motion_main), viewer);
    g_signal_connect(main_motion, "leave", G_CALLBACK(on_leave), viewer);
    gtk_widget_add_controller(viewer->selection_area, main_motion);

    // Overlay Buttons Container
    GtkWidget *box_overlay_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(box_overlay_btns, GTK_ALIGN_START);
    gtk_widget_set_valign(box_overlay_btns, GTK_ALIGN_START);
    gtk_widget_set_margin_start(box_overlay_btns, 5);
    gtk_widget_set_margin_top(box_overlay_btns, 5);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), box_overlay_btns);

    // Ctrl Overlay Button (Hidden by default, shown when panel hidden)
    viewer->btn_ctrl_overlay = gtk_button_new_with_label("ctrl");
    gtk_widget_set_opacity(viewer->btn_ctrl_overlay, 0.7);
    g_signal_connect(viewer->btn_ctrl_overlay, "clicked", G_CALLBACK(on_show_controls_clicked), viewer);
    gtk_widget_set_visible(viewer->btn_ctrl_overlay, FALSE);
    gtk_box_append(GTK_BOX(box_overlay_btns), viewer->btn_ctrl_overlay);

    // Stats Overlay Button (Hidden by default, shown when panel hidden)
    viewer->btn_stats_overlay = gtk_button_new_with_label("stats");
    gtk_widget_set_opacity(viewer->btn_stats_overlay, 0.7);
    g_signal_connect(viewer->btn_stats_overlay, "clicked", G_CALLBACK(on_show_right_panel_clicked), viewer);
    gtk_widget_set_visible(viewer->btn_stats_overlay, FALSE);
    gtk_box_append(GTK_BOX(box_overlay_btns), viewer->btn_stats_overlay);

    drag_controller = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag_controller), 0);
    g_signal_connect(drag_controller, "drag-begin", G_CALLBACK(drag_begin), viewer);
    g_signal_connect(drag_controller, "drag-update", G_CALLBACK(drag_update), viewer);
    g_signal_connect(drag_controller, "drag-end", G_CALLBACK(drag_end), viewer);
    gtk_widget_add_controller(viewer->selection_area, GTK_EVENT_CONTROLLER(drag_controller));


    // Right Sidebar (Colorbar + Stats)
    hbox_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    viewer->hbox_right = hbox_right;
    gtk_widget_set_margin_start(hbox_right, 10);
    gtk_widget_set_margin_end(hbox_right, 10);
    gtk_widget_set_margin_top(hbox_right, 10);
    gtk_widget_set_margin_bottom(hbox_right, 10);

    gtk_paned_set_end_child(GTK_PANED(paned_mid), hbox_right);
    gtk_paned_set_resize_end_child(GTK_PANED(paned_mid), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned_mid), FALSE);

    // Colorbar Column
    GtkWidget *vbox_cbar_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(hbox_right), vbox_cbar_col);

    // Colorbar
    viewer->colorbar = gtk_drawing_area_new ();
    gtk_widget_set_size_request (viewer->colorbar, 60, -1);
    gtk_widget_set_vexpand(viewer->colorbar, TRUE);
    gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (viewer->colorbar), draw_colorbar_func, viewer, NULL);
    gtk_box_append (GTK_BOX (vbox_cbar_col), viewer->colorbar);

    // Reset Colorbar Small Button
    btn_reset_colorbar = gtk_button_new_with_label("R");
    g_signal_connect(btn_reset_colorbar, "clicked", G_CALLBACK(on_btn_reset_colorbar_clicked), viewer);
    gtk_box_append(GTK_BOX(vbox_cbar_col), btn_reset_colorbar);

    // Stats Panel Container (Right)
    viewer->vbox_stats = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_visible(viewer->vbox_stats, TRUE); // Start visible
    gtk_box_append(GTK_BOX(hbox_right), viewer->vbox_stats);

    // Stats Header (Update + Hide)
    GtkWidget *hbox_stats_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_stats), hbox_stats_header);

    // Update Toggle
    viewer->btn_stats_update = gtk_check_button_new_with_label("Update");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(viewer->btn_stats_update), TRUE);
    gtk_widget_set_hexpand(viewer->btn_stats_update, TRUE);
    gtk_box_append(GTK_BOX(hbox_stats_header), viewer->btn_stats_update);

    // Hide Stats Button (Top of Stats Panel)
    GtkWidget *btn_hide_stats = gtk_button_new_with_label("Hide");
    g_signal_connect(btn_hide_stats, "clicked", G_CALLBACK(on_hide_right_panel_clicked), viewer);
    gtk_box_append(GTK_BOX(hbox_stats_header), btn_hide_stats);

    // Stats Box
    frame_stats = gtk_frame_new("ROI Stats");
    viewer->frame_stats = frame_stats;
    viewer->box_stats = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(viewer->box_stats, 5);
    gtk_widget_set_margin_end(viewer->box_stats, 5);
    gtk_widget_set_margin_top(viewer->box_stats, 5);
    gtk_widget_set_margin_bottom(viewer->box_stats, 5);
    gtk_frame_set_child(GTK_FRAME(frame_stats), viewer->box_stats);

    gtk_box_append(GTK_BOX(viewer->vbox_stats), frame_stats);

    GtkWidget *stat_row;
    GtkWidget *lbl;

    // Min / Max
    stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->box_stats), stat_row);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Min"));
    viewer->entry_stat_min = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_min), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_min, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_min, 60, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_min);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Max"));
    viewer->entry_stat_max = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_max), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_max, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_max, 60, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_max);

    // Mean / Median
    stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->box_stats), stat_row);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Avg"));
    viewer->entry_stat_mean = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_mean), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_mean, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_mean, 60, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_mean);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Med"));
    viewer->entry_stat_median = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_median), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_median, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_median, 60, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_median);

    // P01 / P09
    stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->box_stats), stat_row);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("P01"));
    viewer->entry_stat_p01 = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_p01), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_p01, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_p01, 60, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_p01);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("P09"));
    viewer->entry_stat_p09 = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_p09), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_p09, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_p09, 60, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_p09);

    // Npix / Sum
    stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->box_stats), stat_row);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Num"));
    viewer->entry_stat_npix = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_npix), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_npix, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_npix, 60, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_npix);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Sum"));
    viewer->entry_stat_sum = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_sum), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_sum, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_sum, 60, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_sum);

    // Hist Controls (Hist / Log)
    stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->box_stats), stat_row);

    viewer->check_histogram = gtk_check_button_new_with_label("hist");
    g_signal_connect(viewer->check_histogram, "toggled", G_CALLBACK(on_histogram_toggled), viewer);
    gtk_box_append(GTK_BOX(stat_row), viewer->check_histogram);

    const char *hist_scales[] = {"lin", "log", NULL};
    viewer->dropdown_hist_scale = gtk_drop_down_new_from_strings(hist_scales);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_hist_scale), 0);
    g_signal_connect(viewer->dropdown_hist_scale, "notify::selected", G_CALLBACK(on_hist_scale_changed), viewer);
    gtk_box_append(GTK_BOX(stat_row), viewer->dropdown_hist_scale);

    // Histogram
    viewer->histogram_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(viewer->histogram_area, 150, 200);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(viewer->histogram_area), draw_histogram_func, viewer, NULL);
    gtk_widget_set_visible(viewer->histogram_area, FALSE);
    gtk_box_append(GTK_BOX(viewer->box_stats), viewer->histogram_area);

    GtkEventController *hist_motion = gtk_event_controller_motion_new();
    g_signal_connect(hist_motion, "motion", G_CALLBACK(on_motion_hist), viewer);
    g_signal_connect(hist_motion, "leave", G_CALLBACK(on_leave_hist), viewer);
    gtk_widget_add_controller(viewer->histogram_area, hist_motion);

    // Trace Controls
    stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->box_stats), stat_row);

    viewer->check_trace = gtk_check_button_new_with_label("trace");
    g_signal_connect(viewer->check_trace, "toggled", G_CALLBACK(on_trace_toggled), viewer);
    gtk_box_append(GTK_BOX(stat_row), viewer->check_trace);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Dur:"));

    viewer->spin_trace_dur = gtk_spin_button_new_with_range(1.0, 3600.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(viewer->spin_trace_dur), viewer->trace_duration);
    g_signal_connect(viewer->spin_trace_dur, "value-changed", G_CALLBACK(on_trace_dur_changed), viewer);
    gtk_box_append(GTK_BOX(stat_row), viewer->spin_trace_dur);

    // Trace Area
    viewer->trace_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(viewer->trace_area, 150, 300);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(viewer->trace_area), draw_trace_func, viewer, NULL);
    gtk_widget_set_visible(viewer->trace_area, FALSE); // Hidden by default
    gtk_box_append(GTK_BOX(viewer->box_stats), viewer->trace_area);

    gtk_widget_set_visible(viewer->box_stats, FALSE);


    // Initialize UI State based on CLI args
    gtk_check_button_set_active (GTK_CHECK_BUTTON (viewer->check_min_auto), !viewer->fixed_min);
    gtk_check_button_set_active (GTK_CHECK_BUTTON (viewer->check_max_auto), !viewer->fixed_max);

    gtk_widget_set_sensitive (viewer->spin_min, viewer->fixed_min);
    gtk_widget_set_sensitive (viewer->spin_max, viewer->fixed_max);

    if (viewer->fixed_min) gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->spin_min), viewer->min_val);
    if (viewer->fixed_max) gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->spin_max), viewer->max_val);

    viewer->fit_window = TRUE;
    viewer->zoom_factor = 1.0;

    // Set initial colormap range
    viewer->cmap_min = 0.0;
    viewer->cmap_max = 1.0;

    // Default 30ms = 33Hz
    viewer->timeout_id = g_timeout_add (30, update_display, viewer);

    update_spin_steps(viewer);

    gtk_window_present (GTK_WINDOW (window));
}

int
main (int    argc,
      char **argv)
{
    GtkApplication *app;
    int status;
    ViewerApp viewer = {0};
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new ("<stream_name> - ImageStreamIO Viewer");
    g_option_context_add_main_entries (context, entries, NULL);
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_print ("option parsing failed: %s\n", error->message);
        exit (1);
    }
    g_option_context_free (context);

    if (argc < 2) {
        printf("Usage: %s [options] <stream_name>\n", argv[0]);
        return 1;
    }

    viewer.image_name = argv[1];
    viewer.min_val = opt_min;
    viewer.max_val = opt_max;
    viewer.fixed_min = has_min;
    viewer.fixed_max = has_max;

    // Allocate Trace Memory
    viewer.trace_time = (double*)calloc(TRACE_MAX_SAMPLES, sizeof(double));
    viewer.trace_min = (double*)calloc(TRACE_MAX_SAMPLES, sizeof(double));
    viewer.trace_max = (double*)calloc(TRACE_MAX_SAMPLES, sizeof(double));
    viewer.trace_mean = (double*)calloc(TRACE_MAX_SAMPLES, sizeof(double));
    viewer.trace_median = (double*)calloc(TRACE_MAX_SAMPLES, sizeof(double));
    viewer.trace_p01 = (double*)calloc(TRACE_MAX_SAMPLES, sizeof(double));
    viewer.trace_p09 = (double*)calloc(TRACE_MAX_SAMPLES, sizeof(double));

    viewer.trace_hist_data = (uint32_t*)calloc(TRACE_MAX_SAMPLES * TRACE_HIST_BINS, sizeof(uint32_t));
    viewer.trace_hist_min = (double*)calloc(TRACE_MAX_SAMPLES, sizeof(double));
    viewer.trace_hist_max = (double*)calloc(TRACE_MAX_SAMPLES, sizeof(double));

    viewer.trace_duration = 60.0; // Default 60s

    app = gtk_application_new ("org.milk.shmimview", G_APPLICATION_NON_UNIQUE);
    g_signal_connect (app, "activate", G_CALLBACK (activate), &viewer);
    status = g_application_run (G_APPLICATION (app), 0, NULL);
    g_object_unref (app);

    if (viewer.image) {
        ImageStreamIO_closeIm(viewer.image);
        free(viewer.image);
    }
    if (viewer.display_buffer) free(viewer.display_buffer);
    if (viewer.raw_buffer) free(viewer.raw_buffer);
    if (viewer.hist_data) free(viewer.hist_data);

    if (viewer.trace_time) free(viewer.trace_time);
    if (viewer.trace_min) free(viewer.trace_min);
    if (viewer.trace_max) free(viewer.trace_max);
    if (viewer.trace_mean) free(viewer.trace_mean);
    if (viewer.trace_median) free(viewer.trace_median);
    if (viewer.trace_p01) free(viewer.trace_p01);
    if (viewer.trace_p09) free(viewer.trace_p09);

    if (viewer.trace_hist_data) free(viewer.trace_hist_data);
    if (viewer.trace_hist_min) free(viewer.trace_hist_min);
    if (viewer.trace_hist_max) free(viewer.trace_hist_max);

    return status;
}
