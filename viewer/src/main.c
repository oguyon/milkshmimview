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

enum {
    AUTO_MANUAL = 0,
    AUTO_DATA,
    AUTO_P01,
    AUTO_P02,
    AUTO_P05,
    AUTO_P10,
    AUTO_COUNT
};

enum {
    AUTO_MAX_MANUAL = 0,
    AUTO_MAX_DATA,
    AUTO_MAX_P99,
    AUTO_MAX_P98,
    AUTO_MAX_P95,
    AUTO_MAX_P90,
    AUTO_MAX_COUNT
};

// Application state
typedef struct {
    IMAGE *image;
    GtkWidget *image_area; // Replaces picture
    GtkWidget *scrolled_main; // Main Image Scrolled Window
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

    // Trace Curve Visibility
    gboolean show_trace_min;
    gboolean show_trace_max;
    gboolean show_trace_mean;
    gboolean show_trace_median;
    gboolean show_trace_p01;
    gboolean show_trace_p09;

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

    // Pan State (Right Click Drag)
    gboolean is_panning;
    double pan_start_x;
    double pan_start_y;
    double pan_start_hadj;
    double pan_start_vadj;

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
    GtkWidget *dropdown_min_mode;
    GtkWidget *spin_max;
    GtkWidget *dropdown_max_mode;

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

    GtkWidget *check_trace_min;
    GtkWidget *check_trace_max;
    GtkWidget *check_trace_mean;
    GtkWidget *check_trace_median;
    GtkWidget *check_trace_p01;
    GtkWidget *check_trace_p09;

    GtkWidget *check_histogram;
    GtkWidget *dropdown_hist_scale;
    // Pixel Info
    GtkWidget *lbl_pixel_info_main;
    GtkWidget *lbl_pixel_info_roi;
    GtkWidget *histogram_area;

    // Unified Highlight State (Histogram & Colorbar)
    gboolean highlight_active;
    double highlight_val;
    double highlight_mouse_x_hist; // Only for histogram overlay drawing
    gboolean hist_mouse_active;    // True if mouse is specifically over histogram

    // Colorbar Cursor
    double colorbar_cursor_val;
    gboolean colorbar_cursor_active;
    gboolean colorbar_mouse_active; // Specific for drawing the bar UI
    double colorbar_mouse_y;

    // Auto Scale State
    int last_min_mode;
    int last_max_mode;
    gboolean autoscale_source_roi;
    GtkWidget *btn_autoscale;
    GtkWidget *btn_autoscale_source;

    // Trace UI
    GtkWidget *check_trace;
    GtkWidget *lbl_trace_dur;
    GtkWidget *trace_area;
    struct timespec program_start_time;

    // Colorbar Thresholds
    GtkWidget *check_thresholds;
    GtkWidget *spin_thresh_min;
    GtkWidget *spin_thresh_max;
    gboolean thresholds_enabled;
    double thresh_min_val;
    double thresh_max_val;

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

    // Track mouse on main image for live updates
    gboolean mouse_over_main;
    double last_mouse_x_main;
    double last_mouse_y_main;

    // Orientation
    int rot_angle; // 0, 90, 180, 270 (CCW steps: 0,1,2,3)
    gboolean flip_x;
    gboolean flip_y;
    GtkWidget *lbl_rotation;

    // Auto Scale Gain
    double auto_gain;
    GtkWidget *dropdown_gain;
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
static void get_image_screen_geometry(ViewerApp *app, int widget_w, int widget_h, double *center_x, double *center_y, double *scale);
static void widget_to_image_coords(ViewerApp *app, double wx, double wy, int *ix, int *iy);
gboolean update_display (gpointer user_data);
static void on_btn_autoscale_toggled (GtkToggleButton *btn, gpointer user_data);

// Helpers for Orientation
static void update_rotation_label(ViewerApp *app) {
    char buf[16];
    int angle = (app->rot_angle * 90) % 360;
    snprintf(buf, sizeof(buf), "%d deg", angle);
    gtk_label_set_text(GTK_LABEL(app->lbl_rotation), buf);
}

static void
on_rotate_cw_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->rot_angle = (app->rot_angle + 3) % 4; // -90 deg
    update_rotation_label(app);
    app->force_redraw = TRUE;
}

static void
on_rotate_ccw_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->rot_angle = (app->rot_angle + 1) % 4; // +90 deg
    update_rotation_label(app);
    app->force_redraw = TRUE;
}

static void
on_flip_x_toggled (GtkToggleButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->flip_x = gtk_toggle_button_get_active(btn);
    app->force_redraw = TRUE;
}

static void
on_flip_y_toggled (GtkToggleButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->flip_y = gtk_toggle_button_get_active(btn);
    app->force_redraw = TRUE;
}

static void
on_gain_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    guint selected = gtk_drop_down_get_selected(dropdown);
    double gains[] = {1.00, 0.50, 0.20, 0.10, 0.05, 0.02, 0.01};
    if (selected < 7) {
        app->auto_gain = gains[selected];
    }
}

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
            // Black -> Cyan -> Magenta
            if (t < 0.5) {
                // Black to Cyan
                *r = 0;
                *g = t * 2.0;
                *b = t * 2.0;
            } else {
                // Cyan to Magenta
                double x = (t - 0.5) * 2.0;
                *r = x;
                *g = 1.0 - x;
                *b = 1.0;
            }
            break;
        case COLORMAP_RAINBOW:
            // Black -> Blue -> Cyan -> Green -> Yellow -> Red
            // 5 segments
            if (t == 0) { *r=0; *g=0; *b=0; }
            else {
                double h = (1.0 - t) * 240.0; // Blue to Red
                double x = 1.0 - fabs(fmod(h / 60.0, 2) - 1.0);
                // Darken low values
                double scale = 1.0;
                if (t < 0.1) scale = t * 10.0;

                double tr, tg, tb;
                if (h < 60) { tr=1; tg=x; tb=0; }
                else if (h < 120) { tr=x; tg=1; tb=0; }
                else if (h < 180) { tr=0; tg=1; tb=x; }
                else if (h < 240) { tr=0; tg=x; tb=1; }
                else if (h < 300) { tr=x; tg=0; tb=1; }
                else { tr=1; tg=0; tb=x; }

                *r = tr * scale;
                *g = tg * scale;
                *b = tb * scale;
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
            // DS9 "B": Black -> Yellow -> Blue
            if (t < 0.5) {
                // Black -> Yellow
                *r = t * 2.0;
                *g = t * 2.0;
                *b = 0;
            } else {
                // Yellow -> Blue
                double x = (t - 0.5) * 2.0;
                *r = 1.0 - x;
                *g = 1.0 - x;
                *b = x;
            }
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
        snprintf(buf, sizeof(buf), "X: %d Y: %d\nVal: %.4g", ix, iy, val);
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
    app->mouse_over_main = TRUE;
    app->last_mouse_x_main = x;
    app->last_mouse_y_main = y;
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
        app->mouse_over_main = FALSE;
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
    app->highlight_active = TRUE;
    app->hist_mouse_active = TRUE;
    app->highlight_mouse_x_hist = x;

    // Calculate value from X
    int width = gtk_widget_get_width(app->histogram_area);
    int margin_left = 40;
    int plot_w = width - margin_left;
    if (plot_w > 0) {
        double range = app->current_max - app->current_min;
        app->highlight_val = app->current_min + ((x - margin_left) / (double)plot_w) * range;
    }

    gtk_widget_queue_draw(app->histogram_area);
    if (app->trace_area) gtk_widget_queue_draw(app->trace_area);
    app->force_redraw = TRUE; // Trigger image redraw to apply tint
}

static void
on_leave_hist (GtkEventControllerMotion *controller,
               gpointer                  user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->highlight_active = FALSE;
    app->hist_mouse_active = FALSE;
    gtk_widget_queue_draw(app->histogram_area);
    if (app->trace_area) gtk_widget_queue_draw(app->trace_area);
    app->force_redraw = TRUE;
}

static void
on_motion_colorbar (GtkEventControllerMotion *controller,
                    double                    x,
                    double                    y,
                    gpointer                  user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->colorbar_mouse_active = TRUE;
    app->colorbar_mouse_y = y;

    // Set unified highlight
    int height = gtk_widget_get_height(app->colorbar);
    int margin_top = 20;
    int margin_bottom = 20;
    int bar_height = height - margin_top - margin_bottom;

    if (bar_height > 0) {
        double t = 1.0 - (y - margin_top) / (double)bar_height; // 0 at bottom, 1 at top
        app->highlight_val = app->min_val + t * (app->max_val - app->min_val);
        app->highlight_active = TRUE;
    }

    gtk_widget_queue_draw(app->colorbar);
    gtk_widget_queue_draw(app->histogram_area); // Update histogram tinting too
    if (app->trace_area) gtk_widget_queue_draw(app->trace_area);
    app->force_redraw = TRUE;
}

static void
on_leave_colorbar (GtkEventControllerMotion *controller,
                   gpointer                  user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->colorbar_mouse_active = FALSE;
    app->highlight_active = FALSE;
    gtk_widget_queue_draw(app->colorbar);
    gtk_widget_queue_draw(app->histogram_area);
    if (app->trace_area) gtk_widget_queue_draw(app->trace_area);
    app->force_redraw = TRUE;
}

static void
on_pause_toggled (GtkToggleButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->paused = gtk_toggle_button_get_active(btn);
}

static void
on_trace_curve_toggled (GtkCheckButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->show_trace_min = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_trace_min));
    app->show_trace_max = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_trace_max));
    app->show_trace_mean = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_trace_mean));
    app->show_trace_median = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_trace_median));
    app->show_trace_p01 = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_trace_p01));
    app->show_trace_p09 = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_trace_p09));

    if (app->trace_area) gtk_widget_queue_draw(app->trace_area);
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
on_min_mode_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    int mode = gtk_drop_down_get_selected(dropdown);
    app->fixed_min = (mode == AUTO_MANUAL);
    gtk_widget_set_sensitive(app->spin_min, app->fixed_min);

    // Update Autoscale Toggle State
    if (app->btn_autoscale) {
        g_signal_handlers_block_by_func(app->btn_autoscale, on_btn_autoscale_toggled, app);

        gboolean is_auto = !app->fixed_min && !app->fixed_max;
        gboolean is_manual = app->fixed_min && app->fixed_max;

        // If transitioning from Manual to Auto (partially or fully), set Toggle ON
        if (!app->fixed_min) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->btn_autoscale), TRUE);
            gtk_button_set_label(GTK_BUTTON(app->btn_autoscale), "Auto");
        }

        // If user manually selects Manual for both, set Toggle OFF
        if (is_manual) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->btn_autoscale), FALSE);
            gtk_button_set_label(GTK_BUTTON(app->btn_autoscale), "Manual");
        }

        g_signal_handlers_unblock_by_func(app->btn_autoscale, on_btn_autoscale_toggled, app);
    }

    app->force_redraw = TRUE;
}

static void
on_max_mode_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    int mode = gtk_drop_down_get_selected(dropdown);
    app->fixed_max = (mode == AUTO_MAX_MANUAL);
    gtk_widget_set_sensitive(app->spin_max, app->fixed_max);

    // Update Autoscale Toggle State
    if (app->btn_autoscale) {
        g_signal_handlers_block_by_func(app->btn_autoscale, on_btn_autoscale_toggled, app);

        gboolean is_manual = app->fixed_min && app->fixed_max;

        // If transitioning from Manual to Auto (partially or fully), set Toggle ON
        if (!app->fixed_max) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->btn_autoscale), TRUE);
            gtk_button_set_label(GTK_BUTTON(app->btn_autoscale), "Auto");
        }

        // If user manually selects Manual for both, set Toggle OFF
        if (is_manual) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->btn_autoscale), FALSE);
            gtk_button_set_label(GTK_BUTTON(app->btn_autoscale), "Manual");
        }

        g_signal_handlers_unblock_by_func(app->btn_autoscale, on_btn_autoscale_toggled, app);
    }

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

static void update_thresh_spin_steps(ViewerApp *app) {
    if (!app->spin_thresh_min || !app->spin_thresh_max) return;
    double range = fabs(app->thresh_max_val - app->thresh_min_val);
    double step = range * 0.1;
    if (step < 1e-9) step = 0.1;

    gtk_spin_button_set_increments(GTK_SPIN_BUTTON(app->spin_thresh_min), step, step * 10.0);
    gtk_spin_button_set_increments(GTK_SPIN_BUTTON(app->spin_thresh_max), step, step * 10.0);
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
on_btn_autoscale_toggled (GtkToggleButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    gboolean active = gtk_toggle_button_get_active(btn);

    gtk_button_set_label(GTK_BUTTON(btn), active ? "Auto" : "Manual");

    if (active) {
        // Switch to Auto
        // Restore last modes if valid (not manual), else default to DATA
        int target_min = app->last_min_mode;
        int target_max = app->last_max_mode;

        if (target_min == AUTO_MANUAL) target_min = AUTO_DATA;
        if (target_max == AUTO_MAX_MANUAL) target_max = AUTO_MAX_DATA;

        // Prevent signal recursion if needed, though dropdowns trigger redraw which is fine.
        // We want to update dropdowns visually.
        g_signal_handlers_block_by_func(app->dropdown_min_mode, on_min_mode_changed, app);
        g_signal_handlers_block_by_func(app->dropdown_max_mode, on_max_mode_changed, app);

        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->dropdown_min_mode), target_min);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->dropdown_max_mode), target_max);

        app->fixed_min = FALSE;
        app->fixed_max = FALSE;
        gtk_widget_set_sensitive(app->spin_min, FALSE);
        gtk_widget_set_sensitive(app->spin_max, FALSE);

        g_signal_handlers_unblock_by_func(app->dropdown_min_mode, on_min_mode_changed, app);
        g_signal_handlers_unblock_by_func(app->dropdown_max_mode, on_max_mode_changed, app);

    } else {
        // Switch to Manual
        // Save current modes
        int current_min = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->dropdown_min_mode));
        int current_max = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->dropdown_max_mode));

        if (current_min != AUTO_MANUAL) app->last_min_mode = current_min;
        if (current_max != AUTO_MAX_MANUAL) app->last_max_mode = current_max;

        g_signal_handlers_block_by_func(app->dropdown_min_mode, on_min_mode_changed, app);
        g_signal_handlers_block_by_func(app->dropdown_max_mode, on_max_mode_changed, app);

        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->dropdown_min_mode), AUTO_MANUAL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->dropdown_max_mode), AUTO_MAX_MANUAL);

        app->fixed_min = TRUE;
        app->fixed_max = TRUE;
        gtk_widget_set_sensitive(app->spin_min, TRUE);
        gtk_widget_set_sensitive(app->spin_max, TRUE);

        g_signal_handlers_unblock_by_func(app->dropdown_min_mode, on_min_mode_changed, app);
        g_signal_handlers_unblock_by_func(app->dropdown_max_mode, on_max_mode_changed, app);
    }

    app->force_redraw = TRUE;
}

static void
on_btn_autoscale_source_toggled (GtkToggleButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->autoscale_source_roi = gtk_toggle_button_get_active(btn);

    gtk_button_set_label(GTK_BUTTON(btn), app->autoscale_source_roi ? "ROI" : "Full");

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

    gtk_widget_set_visible(app->scrolled_roi, active);

    if (active) {
        int w = gtk_widget_get_width(app->paned_images);
        if (w > 0) {
            int pos = gtk_paned_get_position(GTK_PANED(app->paned_images));
            // If main image taking almost all space (pos near w) or no space (pos near 0)
            // Default split to give ROI some space.
            // pos refers to the size of the start child (Main Image).
            if (pos < 50 || pos > w - 50) {
                // Give half space
                gtk_paned_set_position(GTK_PANED(app->paned_images), w / 2);
            }
        }
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
        int w = gtk_widget_get_width(app->image_area);
        int h = gtk_widget_get_height(app->image_area);
        get_image_screen_geometry(app, w, h, &off_x, &off_y, &scale);
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
get_image_screen_geometry(ViewerApp *app, int widget_w, int widget_h, double *center_x, double *center_y, double *scale) {
    if (!app->image) {
        *center_x = 0; *center_y = 0; *scale = 1.0;
        return;
    }

    if (widget_w <= 0 || widget_h <= 0) {
        *center_x = 0; *center_y = 0; *scale = 1.0;
        return;
    }

    double img_w = (double)app->image->md->size[0];
    double img_h = (double)app->image->md->size[1];

    if (img_w <= 0 || img_h <= 0) {
        *center_x = 0; *center_y = 0; *scale = 1.0;
        return;
    }

    // Effective dimensions after rotation
    double eff_w = (app->rot_angle % 2 == 0) ? img_w : img_h;
    double eff_h = (app->rot_angle % 2 == 0) ? img_h : img_w;

    if (app->fit_window) {
        double scale_x = (double)widget_w / eff_w;
        double scale_y = (double)widget_h / eff_h;
        *scale = (scale_x < scale_y) ? scale_x : scale_y;
    } else {
        *scale = app->zoom_factor;
    }

    *center_x = widget_w / 2.0;
    *center_y = widget_h / 2.0;
}

static void
update_zoom_layout(ViewerApp *app) {
    if (!app->image) return;

    double img_w = (double)app->image->md->size[0];
    double img_h = (double)app->image->md->size[1];

    // Effective dimensions
    double eff_w = (app->rot_angle % 2 == 0) ? img_w : img_h;
    double eff_h = (app->rot_angle % 2 == 0) ? img_h : img_w;

    char buf[64];

    if (app->fit_window) {
        gtk_widget_set_size_request(app->image_area, -1, -1);
        gtk_widget_set_halign(app->image_area, GTK_ALIGN_FILL);
        gtk_widget_set_valign(app->image_area, GTK_ALIGN_FILL);

        gtk_widget_set_hexpand(app->image_area, TRUE);
        gtk_widget_set_vexpand(app->image_area, TRUE);

        int w = gtk_widget_get_width(app->image_area);
        int h = gtk_widget_get_height(app->image_area);
        double cx, cy, scale;
        get_image_screen_geometry(app, w, h, &cx, &cy, &scale);
        snprintf(buf, sizeof(buf), "Zoom: %.1f%%", scale * 100.0);
        gtk_label_set_text(GTK_LABEL(app->lbl_zoom), buf);

        gtk_widget_queue_draw(app->selection_area);
        gtk_widget_queue_draw(app->image_area);
    } else {
        int req_w = (int)(eff_w * app->zoom_factor);
        int req_h = (int)(eff_h * app->zoom_factor);

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

// Helper to draw color indicator
static void
draw_color_indicator (GtkDrawingArea *area,
                      cairo_t        *cr,
                      int             width,
                      int             height,
                      gpointer        user_data)
{
    GdkRGBA *color = (GdkRGBA *)user_data;
    cairo_set_source_rgb(cr, color->red, color->green, color->blue);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
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
    int bar_x = 5;
    int margin_top = 20;
    int margin_bottom = 20;
    int bar_height = height - margin_top - margin_bottom;

    if (bar_height <= 0) return;

    // Draw Gradient or Red/Blue Override
    if (app->colorbar_mouse_active) {
        // Red above mouse, Blue below
        // mouse_y is relative to widget. Bar starts at margin_top, ends at height - margin_bottom.

        double split_y = app->colorbar_mouse_y;

        // Clamp split to bar area
        if (split_y < margin_top) split_y = margin_top;
        if (split_y > height - margin_bottom) split_y = height - margin_bottom;

        // Red Top
        cairo_set_source_rgb(cr, 1, 0, 0); // Red
        cairo_rectangle(cr, bar_x, margin_top, bar_width, split_y - margin_top);
        cairo_fill(cr);

        // Blue Bottom
        cairo_set_source_rgb(cr, 0, 0, 1); // Blue
        cairo_rectangle(cr, bar_x, split_y, bar_width, (height - margin_bottom) - split_y);
        cairo_fill(cr);

    } else {
        // Normal Gradient
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

    // Draw Cursor Line (from image hover)
    if (app->colorbar_cursor_active && !app->colorbar_mouse_active) {
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

    // Draw Highlight Line from Histogram Hover
    if (app->highlight_active && !app->colorbar_mouse_active) {
        double val = app->highlight_val;
        double norm = (val - app->min_val) / (app->max_val - app->min_val);

        if (norm >= 0.0 && norm <= 1.0) {
            double y = height - margin_bottom - norm * bar_height;

            cairo_set_source_rgb(cr, 1, 1, 0); // Yellow
            cairo_set_line_width(cr, 2);
            cairo_move_to(cr, bar_x - 5, y);
            cairo_line_to(cr, bar_x + bar_width + 5, y);
            cairo_stroke(cr);
        }
    }

    // Draw Thresholds
    if (app->thresholds_enabled) {
        // Max Threshold (Red)
        if (app->thresh_max_val < app->max_val && app->thresh_max_val > app->min_val) {
            double norm = (app->thresh_max_val - app->min_val) / (app->max_val - app->min_val);
            double y = height - margin_bottom - norm * bar_height;
            if (y < margin_top) y = margin_top;

            cairo_set_source_rgba(cr, 1, 0, 0, 0.5);
            cairo_rectangle(cr, bar_x, margin_top, bar_width, y - margin_top);
            cairo_fill(cr);
        }

        // Min Threshold (Blue)
        if (app->thresh_min_val > app->min_val && app->thresh_min_val < app->max_val) {
            double norm = (app->thresh_min_val - app->min_val) / (app->max_val - app->min_val);
            double y = height - margin_bottom - norm * bar_height;
            if (y > height - margin_bottom) y = height - margin_bottom;

            cairo_set_source_rgba(cr, 0, 0, 1, 0.5);
            cairo_rectangle(cr, bar_x, y, bar_width, (height - margin_bottom) - y);
            cairo_fill(cr);
        }
    }

    // Draw Mouse Interaction (Yellow Line + Text)
    if (app->colorbar_mouse_active) {
        double split_y = app->colorbar_mouse_y;
        if (split_y < margin_top) split_y = margin_top;
        if (split_y > height - margin_bottom) split_y = height - margin_bottom;

        // Yellow Line
        cairo_set_source_rgb(cr, 1, 1, 0); // Yellow
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, bar_x - 5, split_y);
        cairo_line_to(cr, bar_x + bar_width + 5, split_y);
        cairo_stroke(cr);

        // Calculate Value
        double t = 1.0 - (split_y - margin_top) / (double)bar_height; // 0 at bottom, 1 at top
        double val = app->min_val + t * (app->max_val - app->min_val);

        // Text Box
        char buf[64];
        snprintf(buf, sizeof(buf), "%.4g", val);
        cairo_text_extents(cr, buf, &extents);

        double text_w = extents.width + 10;
        double text_h = extents.height + 6;
        double text_x = bar_x - text_w - 5; // Left of bar
        if (text_x < 0) text_x = bar_x + bar_width + 5; // Right if no space
        double text_y = split_y - text_h / 2;

        cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
        cairo_rectangle(cr, text_x, text_y, text_w, text_h);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, text_x + 5, text_y + text_h - 4);
        cairo_show_text(cr, buf);
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

    // Draw Bars
    double range = app->current_max - app->current_min;

    for (int i = 0; i < app->hist_bins; ++i) {
        double val = (double)app->hist_data[i];
        if (log_scale) val = log10(val + 1.0);

        double h = (val / max_val) * plot_h;
        cairo_rectangle(cr, margin_left + i * bin_width, plot_h - h, bin_width, h);

        // Color based on cursor position if active
        if (app->highlight_active) {
            double bin_center_val = app->current_min + (i + 0.5) / app->hist_bins * range;
            if (bin_center_val < app->highlight_val) {
                cairo_set_source_rgb(cr, 0.2, 0.2, 0.8); // Blueish
            } else {
                cairo_set_source_rgb(cr, 0.8, 0.2, 0.2); // Reddish
            }
        } else {
            cairo_set_source_rgb(cr, 0.8, 0.8, 0.8); // Grey
        }

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

    // Stats Lines (Min, Max, Mean, Median, P10, P90)
    // Using simple colors: Min(Dark Blue), Max(Dark Red), Mean(Green), Median(Yellow), P10(Cyan), P90(Magenta)
    if (range > 0) {
        double val, norm, x;
        int trace_idx = (app->trace_head - 1 + TRACE_MAX_SAMPLES) % TRACE_MAX_SAMPLES;
        if (app->trace_count > 0) {
            // Min (Dark Blue)
            if (app->show_trace_min) {
                val = app->trace_min[trace_idx];
                norm = (val - app->current_min) / range;
                if (norm >= 0 && norm <= 1.0) {
                    cairo_set_source_rgb(cr, 0, 0, 0.5);
                    x = margin_left + norm * plot_w;
                    cairo_move_to(cr, x, 0); cairo_line_to(cr, x, plot_h); cairo_stroke(cr);
                }
            }
            // Max (Dark Red)
            if (app->show_trace_max) {
                val = app->trace_max[trace_idx];
                norm = (val - app->current_min) / range;
                if (norm >= 0 && norm <= 1.0) {
                    cairo_set_source_rgb(cr, 0.5, 0, 0);
                    x = margin_left + norm * plot_w;
                    cairo_move_to(cr, x, 0); cairo_line_to(cr, x, plot_h); cairo_stroke(cr);
                }
            }
            // P10 (Cyan)
            if (app->show_trace_p01) {
                val = app->trace_p01[trace_idx];
                norm = (val - app->current_min) / range;
                if (norm >= 0 && norm <= 1.0) {
                    cairo_set_source_rgb(cr, 0, 1, 1);
                    x = margin_left + norm * plot_w;
                    cairo_move_to(cr, x, 0); cairo_line_to(cr, x, plot_h); cairo_stroke(cr);
                }
            }
            // P90 (Magenta)
            if (app->show_trace_p09) {
                val = app->trace_p09[trace_idx];
                norm = (val - app->current_min) / range;
                if (norm >= 0 && norm <= 1.0) {
                    cairo_set_source_rgb(cr, 1, 0, 1);
                    x = margin_left + norm * plot_w;
                    cairo_move_to(cr, x, 0); cairo_line_to(cr, x, plot_h); cairo_stroke(cr);
                }
            }
            // Mean (Green)
            if (app->show_trace_mean) {
                val = app->stats_mean;
                norm = (val - app->current_min) / range;
                if (norm >= 0 && norm <= 1.0) {
                    cairo_set_source_rgb(cr, 0, 1, 0);
                    x = margin_left + norm * plot_w;
                    cairo_move_to(cr, x, 0); cairo_line_to(cr, x, plot_h); cairo_stroke(cr);
                }
            }
            // Median (Yellow)
            if (app->show_trace_median) {
                val = app->stats_median;
                norm = (val - app->current_min) / range;
                if (norm >= 0 && norm <= 1.0) {
                    cairo_set_source_rgb(cr, 1, 1, 0);
                    x = margin_left + norm * plot_w;
                    cairo_move_to(cr, x, 0); cairo_line_to(cr, x, plot_h); cairo_stroke(cr);
                }
            }
        }
    }

    // Overlay (Only if mouse is actually ON histogram)
    if (app->highlight_active && app->hist_mouse_active && app->highlight_mouse_x_hist >= margin_left) {
        // hist_mouse_active check ensures we only draw the text box when mouse is over histogram,
        // not when it's over colorbar

        int bin = (int)((app->highlight_mouse_x_hist - margin_left) / bin_width);
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

            // Draw Text Box at Top of Vertical Line
            snprintf(buf, sizeof(buf), "%.4g", bin_val);
            cairo_text_extents(cr, buf, &extents);

            double tx = x - extents.width/2;
            if (tx < margin_left + 5) tx = margin_left + 5;
            if (tx + extents.width > width - 5) tx = width - extents.width - 5;

            double ty = 20;

            cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
            cairo_rectangle(cr, tx - 2, ty - extents.height, extents.width + 4, extents.height + 4);
            cairo_fill(cr);

            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_move_to(cr, tx, ty);
            cairo_show_text(cr, buf);
        }
    }

    // Draw Stats Vertical Lines (matching trace colors)
    // Min (Dark Blue), Max (Dark Red), P10 (Cyan), P90 (Magenta)
    // Mean (Green) and Median (Yellow) are already drawn in SEARCH block? No, I need to check.
    // SEARCH block contained Mean/Median lines. I need to make sure I didn't delete them.
    // Wait, the SEARCH block above *only* replaced the overlay part at the end of function.
    // The Mean/Median drawing was *before* the overlay logic in previous code.
    // I should add the extra lines.

    // Actually, I can't easily insert *before* the overlay if I'm replacing the overlay block.
    // I will just add them here, they will be drawn on top of curves but below overlay if I put them before overlay code.
    // But this Replace block is only for the overlay code.
    // To implement "Match vertical lines... to ROI stats", I should probably add them before the overlay.
    // Let me revise the Replace block to include the area before overlay or I'll just append them
    // but then they might cover the overlay? No, overlay is drawn last.
    // The SEARCH block is the overlay logic.
    // I will add the lines *before* the overlay logic in a separate Replace if needed,
    // or rewrite the whole function.
    // Re-reading `draw_histogram_func` in `read_file` output:
    // Mean/Median are drawn around lines 1320. Overlay is around 1330.

    // Let's do a targeted replace for the Mean/Median section to add Min/Max/P10/P90.
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

    int margin_bottom = 20;
    int plot_height = height - margin_bottom;
    if (plot_height <= 0) return;

    // Determine Time Range
    int head = app->trace_head;
    int count = app->trace_count;
    int tail = (head - count + TRACE_MAX_SAMPLES) % TRACE_MAX_SAMPLES;

    double t_end = app->trace_time[(head - 1 + TRACE_MAX_SAMPLES) % TRACE_MAX_SAMPLES];
    double t_start_req = t_end - app->trace_duration;

    // Determine Value Range (Y axis) - Match Histogram Display Range
    double min_y = app->current_min;
    double max_y = app->current_max;

    // Iterating to find start index
    int start_idx = tail;
    int visible_count = 0;

    for (int i = 0; i < count; ++i) {
        int idx = (tail + i) % TRACE_MAX_SAMPLES;
        if (app->trace_time[idx] >= t_start_req) {
            if (visible_count == 0) start_idx = idx;
            visible_count++;
        }
    }

    if (visible_count < 2) return;

    // Draw Heatmap
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, plot_height);
    uint32_t *pixels = (uint32_t*)cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf) / 4;

    // Clear surface
    memset(pixels, 0, plot_height * stride * 4);

    double time_scale = width / app->trace_duration;
    double t_start_disp = t_end - app->trace_duration;

    // Iterate visible samples
    for (int i = 0; i < visible_count; ++i) {
        int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
        double t = app->trace_time[idx];
        int x0 = (int)((t - t_start_disp) * time_scale);

        // Calculate width to next sample
        int x1 = width;
        if (i < visible_count - 1) {
            int next_idx = (idx + 1) % TRACE_MAX_SAMPLES;
            double t_next = app->trace_time[next_idx];
            x1 = (int)((t_next - t_start_disp) * time_scale);
        }

        int w = x1 - x0;
        if (w < 1) w = 1; // Ensure at least 1 pixel
        if (x0 >= width) continue;
        if (x0 + w > width) w = width - x0;

        double h_min = app->trace_hist_min[idx];
        double h_max = app->trace_hist_max[idx];
        double h_range = h_max - h_min;
        if (h_range <= 0) h_range = 1.0;

        uint32_t *hist = app->trace_hist_data + idx * TRACE_HIST_BINS;

        uint32_t col_max = 0;
        for (int b=0; b<TRACE_HIST_BINS; b++) if (hist[b] > col_max) col_max = hist[b];
        if (col_max == 0) col_max = 1;

        // Draw column(s)
        for (int dx = 0; dx < w; dx++) {
            int x = x0 + dx;
            if (x < 0 || x >= width) continue;

            for (int y = 0; y < plot_height; ++y) {
                // Map Y to Value
                double val = max_y - (double)y / plot_height * (max_y - min_y);

                // Map Value to Bin
                int bin = (int)((val - h_min) / h_range * (TRACE_HIST_BINS - 1));

                if (bin >= 0 && bin < TRACE_HIST_BINS) {
                    uint32_t c = hist[bin];
                    if (c > 0) {
                        double brightness = log10(c + 1) / log10(col_max + 1);

                        uint8_t br, bg, bb;
                        if (app->highlight_active) {
                            if (val < app->highlight_val) {
                                // Blueish
                                br = (uint8_t)(brightness * 50);
                                bg = (uint8_t)(brightness * 50);
                                bb = (uint8_t)(brightness * 255);
                            } else {
                                // Reddish
                                br = (uint8_t)(brightness * 255);
                                bg = (uint8_t)(brightness * 50);
                                bb = (uint8_t)(brightness * 50);
                            }
                        } else {
                            // Grey scale
                            uint8_t v = (uint8_t)(brightness * 255.0);
                            br = v; bg = v; bb = v;
                        }

                        // Apply Thresholds Override
                        if (app->thresholds_enabled) {
                            if (val > app->thresh_max_val) {
                                br = 255; bg = 0; bb = 0; // Bright Red
                            } else if (val < app->thresh_min_val) {
                                br = 0; bg = 0; bb = 255; // Bright Blue
                            }
                        }

                        pixels[y * stride + x] = (255 << 24) | (br << 16) | (bg << 8) | bb;
                    }
                }
            }
        }
    }

    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(surf);

    // Draw X-Axis Time Labels
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_font_size(cr, 10);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

    double label_step = 10.0;
    if (app->trace_duration > 60) label_step = 30.0;
    if (app->trace_duration > 300) label_step = 60.0;
    if (app->trace_duration > 1800) label_step = 300.0;

    // Labels are relative to now (t_end), moving backwards
    // 0, -10, -20 ... down to -duration

    for (double t_rel = 0; t_rel >= -app->trace_duration; t_rel -= label_step) {
        double t_abs = t_end + t_rel;
        double x = (t_abs - t_start_disp) * time_scale;

        if (x >= 0 && x <= width) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0fs", t_rel);
            cairo_text_extents_t extents;
            cairo_text_extents(cr, buf, &extents);

            cairo_move_to(cr, x - extents.width/2, height - 5);
            cairo_show_text(cr, buf);

            cairo_move_to(cr, x, plot_height);
            cairo_line_to(cr, x, plot_height + 5);
            cairo_stroke(cr);
        }
    }

    // Draw Lines
    #define MAP_X(t) ((t - (t_end - app->trace_duration)) / app->trace_duration * width)
    #define MAP_Y(v) (plot_height - (v - min_y) / (max_y - min_y) * plot_height)

    cairo_set_line_width(cr, 1);

    // Highlight Line (Yellow Dashed Horizontal)
    if (app->highlight_active) {
        double y = MAP_Y(app->highlight_val);
        if (y >= 0 && y <= plot_height) {
            cairo_set_source_rgb(cr, 1, 1, 0);
            cairo_set_line_width(cr, 2);
            cairo_set_dash(cr, (double[]){4.0, 4.0}, 1, 0);
            cairo_move_to(cr, 0, y);
            cairo_line_to(cr, width, y);
            cairo_stroke(cr);
            cairo_set_dash(cr, NULL, 0, 0);
            cairo_set_line_width(cr, 1);
        }
    }

    // Max - Dark Red
    if (app->show_trace_max) {
        cairo_set_source_rgb(cr, 0.5, 0, 0);
        for (int i = 0; i < visible_count; ++i) {
            int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
            double x = MAP_X(app->trace_time[idx]);
            double y = MAP_Y(app->trace_max[idx]);
            if (i==0) cairo_move_to(cr, x, y);
            else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
    }

    // Min - Dark Blue
    if (app->show_trace_min) {
        cairo_set_source_rgb(cr, 0, 0, 0.5);
        for (int i = 0; i < visible_count; ++i) {
            int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
            double x = MAP_X(app->trace_time[idx]);
            double y = MAP_Y(app->trace_min[idx]);
            if (i==0) cairo_move_to(cr, x, y);
            else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
    }

    // Mean - Green
    if (app->show_trace_mean) {
        cairo_set_source_rgb(cr, 0, 1, 0);
        for (int i = 0; i < visible_count; ++i) {
            int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
            double x = MAP_X(app->trace_time[idx]);
            double y = MAP_Y(app->trace_mean[idx]);
            if (i==0) cairo_move_to(cr, x, y);
            else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
    }

    // Median - Yellow
    if (app->show_trace_median) {
        cairo_set_source_rgb(cr, 1, 1, 0);
        for (int i = 0; i < visible_count; ++i) {
            int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
            double x = MAP_X(app->trace_time[idx]);
            double y = MAP_Y(app->trace_median[idx]);
            if (i==0) cairo_move_to(cr, x, y);
            else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
    }

    // p10 - Cyan
    if (app->show_trace_p01) {
        cairo_set_source_rgb(cr, 0, 1, 1);
        for (int i = 0; i < visible_count; ++i) {
            int idx = (start_idx + i) % TRACE_MAX_SAMPLES;
            double x = MAP_X(app->trace_time[idx]);
            double y = MAP_Y(app->trace_p01[idx]);
            if (i==0) cairo_move_to(cr, x, y);
            else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
    }

    // p90 - Magenta
    if (app->show_trace_p09) {
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
on_trace_dur_increase (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->trace_duration *= 1.2;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1fs", app->trace_duration);
    gtk_label_set_text(GTK_LABEL(app->lbl_trace_dur), buf);
    gtk_widget_queue_draw(app->trace_area);
}

static void
on_trace_dur_decrease (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->trace_duration /= 1.2;
    if (app->trace_duration < 1.0) app->trace_duration = 1.0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1fs", app->trace_duration);
    gtk_label_set_text(GTK_LABEL(app->lbl_trace_dur), buf);
    gtk_widget_queue_draw(app->trace_area);
}

static void
on_threshold_toggled (GtkCheckButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->thresholds_enabled = gtk_check_button_get_active(btn);
    app->force_redraw = TRUE;
}

static void
on_thresh_min_changed (GtkSpinButton *spin, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->thresh_min_val = gtk_spin_button_get_value(spin);
    update_thresh_spin_steps(app);
    app->force_redraw = TRUE;
}

static void
on_thresh_max_changed (GtkSpinButton *spin, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->thresh_max_val = gtk_spin_button_get_value(spin);
    update_thresh_spin_steps(app);
    app->force_redraw = TRUE;
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

    // We want to draw the sub-rectangle (sel_x1, sel_y1, roi_w, roi_h)
    // stretched to fit (0, 0, width, height)

    // Create local buffer for ROI to allow tinting without affecting main image
    // Stride must be 4-byte aligned, simplest is width * 4 for RGB24
    int roi_stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, roi_w);
    guchar *roi_buffer = malloc(roi_stride * roi_h);
    if (!roi_buffer) return;

    int main_stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, app->img_width);

    // Copy pixels
    for (int y = 0; y < roi_h; y++) {
        uint32_t *src_row = (uint32_t*)(app->display_buffer + (app->sel_y1 + y) * main_stride);
        uint32_t *dst_row = (uint32_t*)(roi_buffer + y * roi_stride);
        memcpy(dst_row, src_row + app->sel_x1, roi_w * 4);
    }

    // Apply Tint if needed
    if (app->highlight_active) {
        // Get raw data source
        void *data_source = app->raw_buffer ? app->raw_buffer : (app->image ? app->image->array.raw : NULL);
        if (data_source) {
            int main_width = app->img_width;

            for (int y = 0; y < roi_h; y++) {
                uint32_t *dst_row = (uint32_t*)(roi_buffer + y * roi_stride);
                int raw_y = app->sel_y1 + y;

                for (int x = 0; x < roi_w; x++) {
                    int raw_x = app->sel_x1 + x;
                    size_t idx = raw_y * main_width + raw_x;

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

                    uint32_t p = dst_row[x];
                    uint8_t br = (p >> 16) & 0xFF;
                    uint8_t bg = (p >> 8) & 0xFF;
                    uint8_t bb = p & 0xFF;

                    if (val < app->highlight_val) {
                        // Mix Blue
                        br = (uint8_t)(br * 0.7);
                        bg = (uint8_t)(bg * 0.7);
                        bb = (uint8_t)(bb * 0.7 + 255.0 * 0.3);
                    } else {
                        // Mix Red
                        br = (uint8_t)(br * 0.7 + 255.0 * 0.3);
                        bg = (uint8_t)(bg * 0.7);
                        bb = (uint8_t)(bb * 0.7);
                    }

                    if (app->thresholds_enabled) {
                        if (val > app->thresh_max_val) {
                            br = 255; bg = 0; bb = 0;
                        } else if (val < app->thresh_min_val) {
                            br = 0; bg = 0; bb = 255;
                        }
                    }

                    dst_row[x] = (255 << 24) | (br << 16) | (bg << 8) | bb;
                }
            }
        }
    }

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        roi_buffer,
        CAIRO_FORMAT_RGB24,
        roi_w,
        roi_h,
        roi_stride
    );

    // Clip to widget
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_clip(cr);

    // Calculate dimensions considering rotation
    double eff_w = (app->rot_angle % 2 == 0) ? roi_w : roi_h;
    double eff_h = (app->rot_angle % 2 == 0) ? roi_h : roi_w;

    double scale_x = (double)width / eff_w;
    double scale_y = (double)height / eff_h;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;

    // Transform to match Main Display orientation
    // 1. Center in Widget
    cairo_translate(cr, width / 2.0, height / 2.0);

    // 2. Scale
    cairo_scale(cr, scale, scale);

    // 3. Rotate
    cairo_rotate(cr, app->rot_angle * (M_PI / 2.0));

    // 4. Flip
    // Flip Y: TRUE -> 1 (Top-Down), FALSE -> -1 (Bottom-Up/Cartesian)
    cairo_scale(cr, app->flip_x ? -1.0 : 1.0, app->flip_y ? 1.0 : -1.0);

    // 5. Center Object (Move top-left of object to origin)
    cairo_translate(cr, -roi_w / 2.0, -roi_h / 2.0);

    // Draw
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);

    // Draw red outline around the ROI content in the expanded view
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_set_line_width(cr, 2.0 / scale); // Keep line width constant in screen pixels
    cairo_rectangle(cr, 0, 0, roi_w, roi_h);
    cairo_stroke(cr);

    cairo_surface_destroy(surface);
    free(roi_buffer);
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

    double cx, cy, scale;
    get_image_screen_geometry(app, width, height, &cx, &cy, &scale);

    cairo_save(cr);

    cairo_translate(cr, cx, cy);
    cairo_scale(cr, scale, scale);
    cairo_rotate(cr, app->rot_angle * (M_PI / 2.0));
    cairo_scale(cr, app->flip_x ? -1.0 : 1.0, app->flip_y ? 1.0 : -1.0);
    cairo_translate(cr, -app->img_width / 2.0, -app->img_height / 2.0);

    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);

    cairo_restore(cr);
    cairo_surface_destroy(surface);
}

static void
widget_to_image_coords(ViewerApp *app, double wx, double wy, int *ix, int *iy) {
    double cx, cy, scale;
    // Use selection_area dimensions because events originate from the selection_area overlay
    int w = gtk_widget_get_width(app->selection_area);
    int h = gtk_widget_get_height(app->selection_area);
    get_image_screen_geometry(app, w, h, &cx, &cy, &scale);

    // Inverse Transform
    double x = wx - cx;
    double y = wy - cy;

    // Inv Scale
    x /= scale;
    y /= scale;

    // Inv Rotate (rotate by -angle)
    double angle = -app->rot_angle * (M_PI / 2.0);
    double rx = x * cos(angle) - y * sin(angle);
    double ry = x * sin(angle) + y * cos(angle);
    x = rx; y = ry;

    // Inv Flip
    if (app->flip_x) x = -x;
    if (!app->flip_y) y = -y;

    // Inv Translate (center)
    x += app->img_width / 2.0;
    y += app->img_height / 2.0;

    // Clamp
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= app->image->md->size[0]) x = app->image->md->size[0] - 1;
    if (y >= app->image->md->size[1]) y = app->image->md->size[1] - 1;

    *ix = (int)x;
    *iy = (int)y;
}

// Drawing function for selection overlay and Orientation
static void
draw_selection_func (GtkDrawingArea *area,
                     cairo_t        *cr,
                     int             width,
                     int             height,
                     gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

    double cx, cy, scale;
    get_image_screen_geometry(app, width, height, &cx, &cy, &scale);

    // Draw Orientation Overlay
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, scale, scale);
    cairo_rotate(cr, app->rot_angle * (M_PI / 2.0));
    cairo_scale(cr, app->flip_x ? -1.0 : 1.0, app->flip_y ? 1.0 : -1.0);
    cairo_translate(cr, -app->img_width / 2.0, -app->img_height / 2.0);

    // Draw Origin Dot (0,0) - Yellow
    cairo_set_source_rgb(cr, 1, 1, 0);
    cairo_arc(cr, 0, 0, 5.0 / scale, 0, 2*M_PI);
    cairo_fill(cr);

    // Draw X Vector (Red)
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_set_line_width(cr, 2.0 / scale);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, 30.0 / scale, 0);
    cairo_stroke(cr);
    // Arrowhead X
    cairo_move_to(cr, 30.0 / scale, 0);
    cairo_line_to(cr, 25.0 / scale, -3.0 / scale);
    cairo_line_to(cr, 25.0 / scale, 3.0 / scale);
    cairo_close_path(cr);
    cairo_fill(cr);

    // Draw Y Vector (Green)
    cairo_set_source_rgb(cr, 0, 1, 0);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, 0, 30.0 / scale);
    cairo_stroke(cr);
    // Arrowhead Y
    cairo_move_to(cr, 0, 30.0 / scale);
    cairo_line_to(cr, -3.0 / scale, 25.0 / scale);
    cairo_line_to(cr, 3.0 / scale, 25.0 / scale);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_restore(cr);

    // Draw Selection
    if (app->is_dragging) {
        cairo_set_source_rgb(cr, 1, 0, 0);
        cairo_set_line_width(cr, 2);
        cairo_rectangle(cr, app->start_x, app->start_y, app->curr_x - app->start_x, app->curr_y - app->start_y);
        cairo_stroke(cr);
    } else if (app->selection_active || app->is_moving_selection) {
        cairo_save(cr);
        cairo_translate(cr, cx, cy);
        cairo_scale(cr, scale, scale);
        cairo_rotate(cr, app->rot_angle * (M_PI / 2.0));
        cairo_scale(cr, app->flip_x ? -1.0 : 1.0, app->flip_y ? 1.0 : -1.0);
        cairo_translate(cr, -app->img_width / 2.0, -app->img_height / 2.0);

        cairo_set_source_rgb(cr, 1, 0, 0);
        cairo_set_line_width(cr, 2.0 / scale);
        cairo_rectangle(cr, app->sel_x1, app->sel_y1, app->sel_x2 - app->sel_x1, app->sel_y2 - app->sel_y1);
        cairo_stroke(cr);

        cairo_restore(cr);
    }

    // Draw Frame Counter if Controls Hidden
    if (app->vbox_controls && !gtk_widget_get_visible(app->vbox_controls) && app->image) {
        char buf[64];
        snprintf(buf, sizeof(buf), "cnt: %lu", (unsigned long)app->image->md->cnt0);

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 14);

        cairo_text_extents_t extents;
        cairo_text_extents(cr, buf, &extents);

        double text_w = extents.width + 10;
        double text_h = extents.height + 6;
        double x = width - text_w - 5;
        double y = 5;

        // Background
        cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
        cairo_rectangle(cr, x, y, text_w, text_h);
        cairo_fill(cr);

        // Text
        cairo_set_source_rgb(cr, 1, 1, 0); // Yellow
        cairo_move_to(cr, x + 5, y + text_h - 6);
        cairo_show_text(cr, buf);
    }
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

        // If Shift is NOT pressed, check for Pan
        if (!(modifiers & GDK_SHIFT_MASK)) {
            // Pan
            GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(app->scrolled_main));
            GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(app->scrolled_main));

            // Only pan if zoomed (scrollbars present/active)
            if (gtk_adjustment_get_upper(hadj) > gtk_adjustment_get_page_size(hadj) ||
                gtk_adjustment_get_upper(vadj) > gtk_adjustment_get_page_size(vadj)) {

                app->is_panning = TRUE;
                app->pan_start_x = x;
                app->pan_start_y = y;
                app->pan_start_hadj = gtk_adjustment_get_value(hadj);
                app->pan_start_vadj = gtk_adjustment_get_value(vadj);
            }
            return;
        }

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

    if (app->is_panning) {
        GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(app->scrolled_main));
        GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(app->scrolled_main));

        gtk_adjustment_set_value(hadj, app->pan_start_hadj - offset_x);
        gtk_adjustment_set_value(vadj, app->pan_start_vadj - offset_y);
        return;
    }

    if (app->is_moving_selection) {
        double off_x, off_y, scale;
        int width = gtk_widget_get_width(app->selection_area);
        int height = gtk_widget_get_height(app->selection_area);
        get_image_screen_geometry(app, width, height, &off_x, &off_y, &scale);

        // Transform offset vector to image space (inverse rotate & flip)
        double dx = offset_x / scale;
        double dy = offset_y / scale;

        // Inv Rotate
        double angle = -app->rot_angle * (M_PI / 2.0);
        double rdx = dx * cos(angle) - dy * sin(angle);
        double rdy = dx * sin(angle) + dy * cos(angle);
        dx = rdx; dy = rdy;

        // Inv Flip
        if (app->flip_x) dx = -dx;
        if (!app->flip_y) dy = -dy;

        int idx = (int)dx;
        int idy = (int)dy;

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

    if (app->is_panning) {
        app->is_panning = FALSE;
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

// Refactored Helper for calculating limits from a buffer
static void
calculate_limits_from_buffer(void *data, size_t count, int datatype,
                             int mode_min, int mode_max,
                             double *out_min, double *out_max) {

    if (mode_min == AUTO_MANUAL && mode_max == AUTO_MAX_MANUAL) return;

    double g_min = 1e30, g_max = -1e30;

    // Type-specific scan
    #define SCAN_MINMAX(type) \
        { \
            type *ptr = (type*)data; \
            for(size_t i=0; i<count; ++i) { \
                double v = (double)ptr[i]; \
                if(v < g_min) g_min = v; \
                if(v > g_max) g_max = v; \
            } \
        }

    switch(datatype) {
        case _DATATYPE_FLOAT: SCAN_MINMAX(float); break;
        case _DATATYPE_DOUBLE: SCAN_MINMAX(double); break;
        case _DATATYPE_UINT8: SCAN_MINMAX(uint8_t); break;
        case _DATATYPE_INT16: SCAN_MINMAX(int16_t); break;
        case _DATATYPE_UINT16: SCAN_MINMAX(uint16_t); break;
        case _DATATYPE_INT32: SCAN_MINMAX(int32_t); break;
        case _DATATYPE_UINT32: SCAN_MINMAX(uint32_t); break;
        default: return;
    }

    if (g_min > g_max) { g_min = 0; g_max = 1; }

    // Set targets based on Min
    if (mode_min == AUTO_DATA) *out_min = g_min;

    // Set targets based on Max
    if (mode_max == AUTO_MAX_DATA) *out_max = g_max;

    // If percentiles needed
    gboolean need_hist = (mode_min > AUTO_DATA) || (mode_max > AUTO_MAX_DATA);
    if (need_hist) {
        // Build temporary histogram
        #define HIST_BINS 4096
        static uint32_t hist[HIST_BINS]; // static to avoid stack overflow, safe if single threaded drawing
        memset(hist, 0, sizeof(hist));

        double range = g_max - g_min;
        if (range <= 0) range = 1.0;

        #define FILL_HIST(type) \
            { \
                type *ptr = (type*)data; \
                for(size_t i=0; i<count; ++i) { \
                    int bin = (int)(((double)ptr[i] - g_min) / range * (HIST_BINS - 1)); \
                    if(bin < 0) bin = 0; if(bin >= HIST_BINS) bin = HIST_BINS-1; \
                    hist[bin]++; \
                } \
            }

        switch(datatype) {
            case _DATATYPE_FLOAT: FILL_HIST(float); break;
            case _DATATYPE_DOUBLE: FILL_HIST(double); break;
            case _DATATYPE_UINT8: FILL_HIST(uint8_t); break;
            case _DATATYPE_INT16: FILL_HIST(int16_t); break;
            case _DATATYPE_UINT16: FILL_HIST(uint16_t); break;
            case _DATATYPE_INT32: FILL_HIST(int32_t); break;
            case _DATATYPE_UINT32: FILL_HIST(uint32_t); break;
        }

        // Find percentiles from CDF
        double target_cdf = 0;
        if (mode_min == AUTO_P01) target_cdf = 0.01;
        else if (mode_min == AUTO_P02) target_cdf = 0.02;
        else if (mode_min == AUTO_P05) target_cdf = 0.05;
        else if (mode_min == AUTO_P10) target_cdf = 0.10;

        if (target_cdf > 0) {
            double threshold = count * target_cdf;
            double cum = 0;
            for (int i=0; i<HIST_BINS; ++i) {
                cum += hist[i];
                if (cum >= threshold) {
                    *out_min = g_min + ((double)i / (HIST_BINS-1)) * range;
                    break;
                }
            }
        }

        target_cdf = 0;
        if (mode_max == AUTO_MAX_P99) target_cdf = 0.99;
        else if (mode_max == AUTO_MAX_P98) target_cdf = 0.98;
        else if (mode_max == AUTO_MAX_P95) target_cdf = 0.95;
        else if (mode_max == AUTO_MAX_P90) target_cdf = 0.90;

        if (target_cdf > 0) {
            double threshold = count * target_cdf;
            double cum = 0;
            for (int i=0; i<HIST_BINS; ++i) {
                cum += hist[i];
                if (cum >= threshold) {
                    *out_max = g_min + ((double)i / (HIST_BINS-1)) * range;
                    break;
                }
            }
        }
    }
}

static void
calculate_autoscale_limits(ViewerApp *app, double *new_min, double *new_max, int width, int height, uint8_t datatype, void *raw_data) {
    int mode_min = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->dropdown_min_mode));
    int mode_max = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->dropdown_max_mode));

    if (mode_min == AUTO_MANUAL && mode_max == AUTO_MAX_MANUAL) return;

    if (app->autoscale_source_roi && app->selection_active) {
        // Extract ROI data
        int x1 = app->sel_x1; int x2 = app->sel_x2 + 1;
        int y1 = app->sel_y1; int y2 = app->sel_y2 + 1;
        if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
        if (x2 > width) x2 = width; if (y2 > height) y2 = height;

        int roi_w = x2 - x1; int roi_h = y2 - y1;

        if (roi_w > 0 && roi_h > 0) {
            size_t roi_count = roi_w * roi_h;
            size_t type_size = ImageStreamIO_typesize(datatype);

            // Allocate temp buffer for ROI contiguous block
            // (Optimize: We could iterate directly in calculate_limits_from_buffer if we passed strides/ROI,
            // but copy is simpler for now and likely fast enough for typical ROIs)
            void *roi_buf = malloc(roi_count * type_size);
            if (roi_buf) {
                for(int y=0; y<roi_h; ++y) {
                    void *src = (char*)raw_data + ((y1 + y) * width + x1) * type_size;
                    void *dst = (char*)roi_buf + (y * roi_w) * type_size;
                    memcpy(dst, src, roi_w * type_size);
                }

                calculate_limits_from_buffer(roi_buf, roi_count, datatype, mode_min, mode_max, new_min, new_max);
                free(roi_buf);
                return;
            }
        }
    }

    // Fallback to Full Frame
    calculate_limits_from_buffer(raw_data, (size_t)width * height, datatype, mode_min, mode_max, new_min, new_max);

    // Apply Gain (Smoothing) if in Auto Mode
    // formula: val = gain * new + (1-gain) * old
    // Gain 1.0 = Instant, Gain 0.01 = Slow
    if (mode_min != AUTO_MANUAL) {
        *new_min = app->auto_gain * (*new_min) + (1.0 - app->auto_gain) * app->min_val;
    }
    if (mode_max != AUTO_MAX_MANUAL) {
        *new_max = app->auto_gain * (*new_max) + (1.0 - app->auto_gain) * app->max_val;
    }
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

    double min_val = app->min_val;
    double max_val = app->max_val;

    // Calculate Autoscale if needed
    if (!app->fixed_min || !app->fixed_max) {
        calculate_autoscale_limits(app, &min_val, &max_val, width, height, datatype, raw_data);
    }

    if (app->fixed_min) min_val = app->min_val;
    else app->min_val = min_val; // Update internal state for UI consistency?

    if (app->fixed_max) max_val = app->max_val;
    else app->max_val = max_val;

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

            if (app->thresholds_enabled) {
                if (val > app->thresh_max_val) {
                    br = 255; bg = 0; bb = 0; // Bright Red
                } else if (val < app->thresh_min_val) {
                    br = 0; bg = 0; bb = 255; // Bright Blue
                }
            }

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

        // Update live pixel info if mouse is hovering
        if (app->mouse_over_main) {
            update_pixel_info(app, app->lbl_pixel_info_main, app->last_mouse_x_main, app->last_mouse_y_main, FALSE);
        }

        // Update counter label (only when image updates)
        char buf[64];
        snprintf(buf, sizeof(buf), "Counter: %lu", last_cnt0);
        gtk_label_set_text(GTK_LABEL(app->lbl_counter), buf);

        // Force redraw of selection overlay to update frame counter if control panel is hidden
        if (app->selection_area) gtk_widget_queue_draw(app->selection_area);
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

    // Orientation Controls
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);

    GtkWidget *btn_rot_cw = gtk_button_new_with_label("-90");
    g_signal_connect(btn_rot_cw, "clicked", G_CALLBACK(on_rotate_cw_clicked), viewer);
    gtk_box_append(GTK_BOX(row), btn_rot_cw);

    GtkWidget *btn_rot_ccw = gtk_button_new_with_label("+90");
    g_signal_connect(btn_rot_ccw, "clicked", G_CALLBACK(on_rotate_ccw_clicked), viewer);
    gtk_box_append(GTK_BOX(row), btn_rot_ccw);

    viewer->lbl_rotation = gtk_label_new("0 deg");
    gtk_box_append(GTK_BOX(row), viewer->lbl_rotation);

    GtkWidget *btn_flip_x = gtk_toggle_button_new_with_label("FlipX");
    gtk_widget_add_css_class(btn_flip_x, "auto-scale-red"); // Reuse red style
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn_flip_x), viewer->flip_x);
    g_signal_connect(btn_flip_x, "toggled", G_CALLBACK(on_flip_x_toggled), viewer);
    gtk_box_append(GTK_BOX(row), btn_flip_x);

    GtkWidget *btn_flip_y = gtk_toggle_button_new_with_label("FlipY");
    gtk_widget_add_css_class(btn_flip_y, "auto-scale-red");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn_flip_y), viewer->flip_y);
    g_signal_connect(btn_flip_y, "toggled", G_CALLBACK(on_flip_y_toggled), viewer);
    gtk_box_append(GTK_BOX(row), btn_flip_y);

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

    // Auto Scale Button
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);

    btn_autoscale = gtk_toggle_button_new_with_label ("Auto");
    viewer->btn_autoscale = btn_autoscale;
    g_signal_connect (btn_autoscale, "toggled", G_CALLBACK (on_btn_autoscale_toggled), viewer);
    gtk_widget_set_hexpand(btn_autoscale, TRUE);
    gtk_box_append (GTK_BOX (row), btn_autoscale);

    // Add CSS for Auto Scale button (Red when active)
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".auto-scale-red:checked { background: #aa0000; color: white; border-color: #550000; }");

    gtk_widget_add_css_class(btn_autoscale, "auto-scale-red");

    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Auto Scale Gain Dropdown
    const char *gain_opts[] = {"1.00", "0.50", "0.20", "0.10", "0.05", "0.02", "0.01", NULL};
    viewer->dropdown_gain = gtk_drop_down_new_from_strings(gain_opts);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_gain), 0); // Default 1.00
    g_signal_connect(viewer->dropdown_gain, "notify::selected", G_CALLBACK(on_gain_changed), viewer);
    gtk_box_append(GTK_BOX(row), viewer->dropdown_gain);

    // Auto Scale Source Toggle
    GtkWidget *btn_as_source = gtk_toggle_button_new_with_label("Full");
    viewer->btn_autoscale_source = btn_as_source;
    g_signal_connect(btn_as_source, "toggled", G_CALLBACK(on_btn_autoscale_source_toggled), viewer);
    gtk_box_append(GTK_BOX(row), btn_as_source);

    // Min Control
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);
    gtk_box_append(GTK_BOX(row), gtk_label_new("Min"));

    viewer->spin_min = gtk_spin_button_new_with_range (-1e20, 1e20, 1.0);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (viewer->spin_min), 2);
    gtk_widget_set_hexpand(viewer->spin_min, TRUE);
    g_signal_connect (viewer->spin_min, "value-changed", G_CALLBACK (on_spin_min_changed), viewer);
    gtk_box_append (GTK_BOX (row), viewer->spin_min);

    const char *min_modes[] = {"Manual", "Min Val", "1%", "2%", "5%", "10%", NULL};
    viewer->dropdown_min_mode = gtk_drop_down_new_from_strings(min_modes);
    g_signal_connect(viewer->dropdown_min_mode, "notify::selected", G_CALLBACK(on_min_mode_changed), viewer);
    gtk_box_append(GTK_BOX(row), viewer->dropdown_min_mode);

    // Max Control
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);
    gtk_box_append(GTK_BOX(row), gtk_label_new("Max"));

    viewer->spin_max = gtk_spin_button_new_with_range (-1e20, 1e20, 1.0);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (viewer->spin_max), 2);
    gtk_widget_set_hexpand(viewer->spin_max, TRUE);
    g_signal_connect (viewer->spin_max, "value-changed", G_CALLBACK (on_spin_max_changed), viewer);
    gtk_box_append (GTK_BOX (row), viewer->spin_max);

    const char *max_modes[] = {"Manual", "Max Val", "99%", "98%", "95%", "90%", NULL};
    viewer->dropdown_max_mode = gtk_drop_down_new_from_strings(max_modes);
    g_signal_connect(viewer->dropdown_max_mode, "notify::selected", G_CALLBACK(on_max_mode_changed), viewer);
    gtk_box_append(GTK_BOX(row), viewer->dropdown_max_mode);

    // Separator
    gtk_box_append(GTK_BOX(viewer->vbox_controls), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Thresholds Control
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);

    viewer->check_thresholds = gtk_check_button_new_with_label("Thresholds");
    g_signal_connect(viewer->check_thresholds, "toggled", G_CALLBACK(on_threshold_toggled), viewer);
    gtk_box_append(GTK_BOX(row), viewer->check_thresholds);

    // Thresh Min
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);
    gtk_box_append(GTK_BOX(row), gtk_label_new("T.Min"));

    viewer->spin_thresh_min = gtk_spin_button_new_with_range(-1e20, 1e20, 1.0);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(viewer->spin_thresh_min), 2);
    gtk_widget_set_hexpand(viewer->spin_thresh_min, TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(viewer->spin_thresh_min), 0.0);
    g_signal_connect(viewer->spin_thresh_min, "value-changed", G_CALLBACK(on_thresh_min_changed), viewer);
    gtk_box_append(GTK_BOX(row), viewer->spin_thresh_min);

    // Thresh Max
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->vbox_controls), row);
    gtk_box_append(GTK_BOX(row), gtk_label_new("T.Max"));

    viewer->spin_thresh_max = gtk_spin_button_new_with_range(-1e20, 1e20, 1.0);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(viewer->spin_thresh_max), 2);
    gtk_widget_set_hexpand(viewer->spin_thresh_max, TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(viewer->spin_thresh_max), 1.0);
    g_signal_connect(viewer->spin_thresh_max, "value-changed", G_CALLBACK(on_thresh_max_changed), viewer);
    gtk_box_append(GTK_BOX(row), viewer->spin_thresh_max);

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
    viewer->scrolled_main = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (viewer->scrolled_main, TRUE);
    gtk_widget_set_hexpand (viewer->scrolled_main, TRUE);

    gtk_paned_set_start_child(GTK_PANED(viewer->paned_images), viewer->scrolled_main);
    gtk_paned_set_resize_start_child(GTK_PANED(viewer->paned_images), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(viewer->paned_images), TRUE);

    scroll_controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect (scroll_controller, "scroll", G_CALLBACK (on_scroll), viewer);
    gtk_widget_add_controller (viewer->scrolled_main, scroll_controller);

    overlay = gtk_overlay_new();
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (viewer->scrolled_main), overlay);

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
    gtk_widget_set_halign(viewer->lbl_pixel_info_main, GTK_ALIGN_START);
    gtk_widget_set_valign(viewer->lbl_pixel_info_main, GTK_ALIGN_END);
    gtk_widget_set_margin_start(viewer->lbl_pixel_info_main, 5);
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
    gtk_widget_set_size_request (viewer->colorbar, 100, -1);
    gtk_widget_set_vexpand(viewer->colorbar, TRUE);
    gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (viewer->colorbar), draw_colorbar_func, viewer, NULL);

    GtkEventController *cbar_motion = gtk_event_controller_motion_new();
    g_signal_connect(cbar_motion, "motion", G_CALLBACK(on_motion_colorbar), viewer);
    g_signal_connect(cbar_motion, "leave", G_CALLBACK(on_leave_colorbar), viewer);
    gtk_widget_add_controller(viewer->colorbar, cbar_motion);

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

    // Helper to create rows with toggle and color
    // We can't easily pass the color to a generic helper without static globals or malloc,
    // so we'll do it inline or simple blocks.

    static GdkRGBA col_min = {0, 0, 0.5, 1}; // Dark Blue
    static GdkRGBA col_max = {0.5, 0, 0, 1}; // Dark Red
    static GdkRGBA col_mean = {0, 1, 0, 1};  // Green
    static GdkRGBA col_med = {1, 1, 0, 1};   // Yellow
    static GdkRGBA col_p01 = {0, 1, 1, 1};   // Cyan (Light Blue)
    static GdkRGBA col_p09 = {1, 0, 1, 1};   // Magenta (Light Red)

    // Row 1: Min / Max
    stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->box_stats), stat_row);

    // Min
    GtkWidget *da_min = gtk_drawing_area_new();
    gtk_widget_set_size_request(da_min, 15, 15);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da_min), draw_color_indicator, &col_min, NULL);
    gtk_box_append(GTK_BOX(stat_row), da_min);

    viewer->check_trace_min = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(viewer->check_trace_min), TRUE);
    viewer->show_trace_min = TRUE;
    g_signal_connect(viewer->check_trace_min, "toggled", G_CALLBACK(on_trace_curve_toggled), viewer);
    gtk_box_append(GTK_BOX(stat_row), viewer->check_trace_min);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Min"));
    viewer->entry_stat_min = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_min), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_min, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_min, 50, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_min);

    // Max
    GtkWidget *da_max = gtk_drawing_area_new();
    gtk_widget_set_size_request(da_max, 15, 15);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da_max), draw_color_indicator, &col_max, NULL);
    gtk_box_append(GTK_BOX(stat_row), da_max);

    viewer->check_trace_max = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(viewer->check_trace_max), TRUE);
    viewer->show_trace_max = TRUE;
    g_signal_connect(viewer->check_trace_max, "toggled", G_CALLBACK(on_trace_curve_toggled), viewer);
    gtk_box_append(GTK_BOX(stat_row), viewer->check_trace_max);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Max"));
    viewer->entry_stat_max = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_max), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_max, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_max, 50, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_max);

    // Row 2: Mean / Median
    stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->box_stats), stat_row);

    // Mean
    GtkWidget *da_mean = gtk_drawing_area_new();
    gtk_widget_set_size_request(da_mean, 15, 15);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da_mean), draw_color_indicator, &col_mean, NULL);
    gtk_box_append(GTK_BOX(stat_row), da_mean);

    viewer->check_trace_mean = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(viewer->check_trace_mean), TRUE);
    viewer->show_trace_mean = TRUE;
    g_signal_connect(viewer->check_trace_mean, "toggled", G_CALLBACK(on_trace_curve_toggled), viewer);
    gtk_box_append(GTK_BOX(stat_row), viewer->check_trace_mean);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Avg"));
    viewer->entry_stat_mean = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_mean), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_mean, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_mean, 50, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_mean);

    // Median
    GtkWidget *da_med = gtk_drawing_area_new();
    gtk_widget_set_size_request(da_med, 15, 15);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da_med), draw_color_indicator, &col_med, NULL);
    gtk_box_append(GTK_BOX(stat_row), da_med);

    viewer->check_trace_median = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(viewer->check_trace_median), TRUE);
    viewer->show_trace_median = TRUE;
    g_signal_connect(viewer->check_trace_median, "toggled", G_CALLBACK(on_trace_curve_toggled), viewer);
    gtk_box_append(GTK_BOX(stat_row), viewer->check_trace_median);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("Med"));
    viewer->entry_stat_median = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_median), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_median, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_median, 50, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_median);

    // Row 3: p10% / p90%
    stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(viewer->box_stats), stat_row);

    // p10
    GtkWidget *da_p01 = gtk_drawing_area_new();
    gtk_widget_set_size_request(da_p01, 15, 15);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da_p01), draw_color_indicator, &col_p01, NULL);
    gtk_box_append(GTK_BOX(stat_row), da_p01);

    viewer->check_trace_p01 = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(viewer->check_trace_p01), TRUE);
    viewer->show_trace_p01 = TRUE;
    g_signal_connect(viewer->check_trace_p01, "toggled", G_CALLBACK(on_trace_curve_toggled), viewer);
    gtk_box_append(GTK_BOX(stat_row), viewer->check_trace_p01);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("p10%"));
    viewer->entry_stat_p01 = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_p01), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_p01, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_p01, 50, -1);
    gtk_box_append(GTK_BOX(stat_row), viewer->entry_stat_p01);

    // p90
    GtkWidget *da_p09 = gtk_drawing_area_new();
    gtk_widget_set_size_request(da_p09, 15, 15);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da_p09), draw_color_indicator, &col_p09, NULL);
    gtk_box_append(GTK_BOX(stat_row), da_p09);

    viewer->check_trace_p09 = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(viewer->check_trace_p09), TRUE);
    viewer->show_trace_p09 = TRUE;
    g_signal_connect(viewer->check_trace_p09, "toggled", G_CALLBACK(on_trace_curve_toggled), viewer);
    gtk_box_append(GTK_BOX(stat_row), viewer->check_trace_p09);

    gtk_box_append(GTK_BOX(stat_row), gtk_label_new("p90%"));
    viewer->entry_stat_p09 = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(viewer->entry_stat_p09), FALSE);
    gtk_widget_set_can_focus(viewer->entry_stat_p09, FALSE);
    gtk_widget_set_size_request(viewer->entry_stat_p09, 50, -1);
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

    GtkWidget *btn_dec = gtk_button_new_with_label("-");
    gtk_widget_set_size_request(btn_dec, 20, -1);
    g_signal_connect(btn_dec, "clicked", G_CALLBACK(on_trace_dur_decrease), viewer);
    gtk_box_append(GTK_BOX(stat_row), btn_dec);

    viewer->lbl_trace_dur = gtk_label_new("60.0s");
    gtk_box_append(GTK_BOX(stat_row), viewer->lbl_trace_dur);

    GtkWidget *btn_inc = gtk_button_new_with_label("+");
    gtk_widget_set_size_request(btn_inc, 20, -1);
    g_signal_connect(btn_inc, "clicked", G_CALLBACK(on_trace_dur_increase), viewer);
    gtk_box_append(GTK_BOX(stat_row), btn_inc);

    // Trace Area
    viewer->trace_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(viewer->trace_area, 150, 300);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(viewer->trace_area), draw_trace_func, viewer, NULL);
    gtk_widget_set_visible(viewer->trace_area, FALSE); // Hidden by default
    gtk_box_append(GTK_BOX(viewer->box_stats), viewer->trace_area);

    gtk_widget_set_visible(viewer->box_stats, FALSE);


    // Initialize UI State based on CLI args
    // Set Dropdowns for Auto/Manual
    if (viewer->fixed_min) gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_min_mode), AUTO_MANUAL);
    else gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_min_mode), AUTO_DATA);

    if (viewer->fixed_max) gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_max_mode), AUTO_MAX_MANUAL);
    else gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_max_mode), AUTO_MAX_DATA);

    gtk_widget_set_sensitive (viewer->spin_min, viewer->fixed_min);
    gtk_widget_set_sensitive (viewer->spin_max, viewer->fixed_max);

    if (viewer->fixed_min) gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->spin_min), viewer->min_val);
    if (viewer->fixed_max) gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->spin_max), viewer->max_val);

    viewer->fit_window = TRUE;
    viewer->zoom_factor = 1.0;
    viewer->flip_y = FALSE; // Default Cartesian (0,0 bottom left) with new logic

    // Set initial colormap range
    viewer->cmap_min = 0.0;
    viewer->cmap_max = 1.0;

    // Set Default Thresholds
    viewer->thresh_min_val = 0.0;
    viewer->thresh_max_val = 1.0;

    // Default 30ms = 33Hz
    viewer->timeout_id = g_timeout_add (30, update_display, viewer);

    update_spin_steps(viewer);
    update_thresh_spin_steps(viewer);

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

    viewer.trace_duration = 10.0; // Default 10s
    viewer.auto_gain = 1.0;
    clock_gettime(CLOCK_MONOTONIC, &viewer.program_start_time);

    viewer.last_min_mode = AUTO_DATA;
    viewer.last_max_mode = AUTO_MAX_DATA;

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
