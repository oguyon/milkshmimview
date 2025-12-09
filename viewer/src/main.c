#include <gtk/gtk.h>
#include <ImageStreamIO/ImageStreamIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
    int img_width;
    int img_height;

    // Scaling state
    double min_val;
    double max_val;
    gboolean fixed_min;
    gboolean fixed_max;

    // Actual scaling used for display (for colorbar)
    double current_min;
    double current_max;

    // Control flags
    gboolean force_redraw;

    // Selection state (Left Click)
    gboolean selection_active;
    gboolean is_dragging;
    gboolean is_moving_selection;
    double start_x, start_y; // Widget coords
    double curr_x, curr_y;   // Widget coords

    // Contrast Adjustment state (Right Click)
    gboolean is_adjusting_contrast;
    double contrast_start_min;
    double contrast_start_max;
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

    // UI Widgets
    GtkWidget *lbl_counter;
    GtkWidget *dropdown_fps;

    GtkWidget *spin_min;
    GtkWidget *check_min_auto;
    GtkWidget *spin_max;
    GtkWidget *check_max_auto;

    GtkWidget *btn_fit_window;
    GtkWidget *dropdown_zoom;
    GtkWidget *lbl_zoom;

    // Stats Widgets
    GtkWidget *box_stats;
    GtkWidget *lbl_stat_min;
    GtkWidget *lbl_stat_max;
    GtkWidget *lbl_stat_mean;
    GtkWidget *lbl_stat_median;
    GtkWidget *lbl_stat_p01;
    GtkWidget *lbl_stat_p09;

    GtkWidget *check_histogram;
    GtkWidget *histogram_area;
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
static gboolean update_display (gpointer user_data);

// UI Callbacks
static void
on_fps_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    guint selected = gtk_drop_down_get_selected(dropdown);

    guint interval = 33; // Default ~30fps
    if (selected == 0) interval = 100; // 10 Hz
    else if (selected == 1) interval = 40; // 25 Hz
    else if (selected == 2) interval = 20; // 50 Hz
    else if (selected == 3) interval = 10; // 100 Hz

    if (app->timeout_id > 0) {
        g_source_remove(app->timeout_id);
    }
    app->timeout_id = g_timeout_add(interval, update_display, app);
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

static void
on_spin_min_changed (GtkSpinButton *spin, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->min_val = gtk_spin_button_get_value(spin);
    app->force_redraw = TRUE;
}

static void
on_spin_max_changed (GtkSpinButton *spin, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->max_val = gtk_spin_button_get_value(spin);
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
    // Force Auto On
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->check_min_auto), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->check_max_auto), TRUE);
    app->force_redraw = TRUE;
}

static void
on_btn_reset_selection_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->selection_active = FALSE;
    app->force_redraw = TRUE;
    gtk_widget_set_visible(app->box_stats, FALSE);
    gtk_widget_queue_draw(app->selection_area);
}

static void
on_fit_window_toggled (GtkCheckButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->fit_window = gtk_check_button_get_active(btn);

    if (!app->fit_window) {
        double off_x, off_y, scale;
        get_image_screen_geometry(app, &off_x, &off_y, &scale);
        if (scale > 0) app->zoom_factor = scale;
        else app->zoom_factor = 1.0;
    }

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

    cairo_pattern_t *pat = cairo_pattern_create_linear (0, margin_top + bar_height, 0, margin_top);
    cairo_pattern_add_color_stop_rgb (pat, 0, 0, 0, 0); // Black at bottom
    cairo_pattern_add_color_stop_rgb (pat, 1, 1, 1, 1); // White at top

    cairo_rectangle (cr, bar_x, margin_top, bar_width, bar_height);
    cairo_set_source (cr, pat);
    cairo_fill (cr);
    cairo_pattern_destroy (pat);

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    cairo_rectangle (cr, bar_x, margin_top, bar_width, bar_height);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);

    char buf[64];
    cairo_text_extents_t extents;

    snprintf(buf, sizeof(buf), "%.2g", app->current_max);
    cairo_text_extents(cr, buf, &extents);
    cairo_move_to(cr, (width - extents.width)/2, margin_top - 5);
    cairo_show_text(cr, buf);

    snprintf(buf, sizeof(buf), "%.2g", app->current_min);
    cairo_text_extents(cr, buf, &extents);
    cairo_move_to(cr, (width - extents.width)/2, height - margin_bottom + extents.height + 5);
    cairo_show_text(cr, buf);
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

    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8); // Light gray bars

    double bin_width = (double)width / app->hist_bins;

    for (int i = 0; i < app->hist_bins; ++i) {
        double h = ((double)app->hist_data[i] / app->hist_max_count) * height;
        cairo_rectangle(cr, i * bin_width, height - h, bin_width, h);
        cairo_fill(cr);
    }
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

        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_min_auto)) ||
            gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_max_auto))) {

            gtk_check_button_set_active(GTK_CHECK_BUTTON(app->check_min_auto), FALSE);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(app->check_max_auto), FALSE);
        }

        app->contrast_start_min = app->min_val;
        app->contrast_start_max = app->max_val;

        return;
    }

    if (button == GDK_BUTTON_PRIMARY) { // Left Click
        int ix, iy;
        widget_to_image_coords(app, x, y, &ix, &iy);

        if (app->selection_active &&
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

        double start_center = (app->contrast_start_max + app->contrast_start_min) / 2.0;
        double start_width = (app->contrast_start_max - app->contrast_start_min);
        if (start_width == 0) start_width = 1.0;

        double range = start_width;
        double shift = (offset_x / (double)width) * range;
        double new_center = start_center + shift;

        double scale_factor = exp( -offset_y / (double)height * 4.0 );
        double new_width = start_width / scale_factor;

        double new_min = new_center - new_width / 2.0;
        double new_max = new_center + new_width / 2.0;

        gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_min), new_min);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_max), new_max);

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

    // Calculate Histogram if enabled
    gboolean show_hist = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_histogram));
    if (show_hist) {
        if (!app->hist_data) {
            app->hist_bins = 256;
            app->hist_data = (guint32*)calloc(app->hist_bins, sizeof(guint32));
        } else {
            memset(app->hist_data, 0, app->hist_bins * sizeof(guint32));
        }

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
        gtk_widget_queue_draw(app->histogram_area);
    }

    double mean = sum / count;

    qsort(values, count, sizeof(double), compare_doubles);

    double median = values[count / 2];
    double p01 = values[(size_t)(count * 0.1)];
    double p09 = values[(size_t)(count * 0.9)];

    free(values);

    char buf[64];
    snprintf(buf, sizeof(buf), "Min: %.4g", min_v);
    gtk_label_set_text(GTK_LABEL(app->lbl_stat_min), buf);

    snprintf(buf, sizeof(buf), "Max: %.4g", max_v);
    gtk_label_set_text(GTK_LABEL(app->lbl_stat_max), buf);

    snprintf(buf, sizeof(buf), "Mean: %.4g", mean);
    gtk_label_set_text(GTK_LABEL(app->lbl_stat_mean), buf);

    snprintf(buf, sizeof(buf), "Median: %.4g", median);
    gtk_label_set_text(GTK_LABEL(app->lbl_stat_median), buf);

    snprintf(buf, sizeof(buf), "P 0.1: %.4g", p01);
    gtk_label_set_text(GTK_LABEL(app->lbl_stat_p01), buf);

    snprintf(buf, sizeof(buf), "P 0.9: %.4g", p09);
    gtk_label_set_text(GTK_LABEL(app->lbl_stat_p09), buf);
}

static void
draw_image (ViewerApp *app)
{
    if (!app->image || !app->image->array.raw) return;

    void *raw_data = NULL;

    if (app->image->md->imagetype & CIRCULAR_BUFFER) {
        if (app->image->md->naxis == 3) {
            uint64_t slice_index = app->image->md->cnt1 % app->image->md->size[2];
            size_t element_size = ImageStreamIO_typesize(app->image->md->datatype);
            size_t frame_size = app->image->md->size[0] * app->image->md->size[1] * element_size;
            raw_data = (char*)app->image->array.raw + (slice_index * frame_size);
        } else {
             raw_data = app->image->array.raw;
        }
    } else {
        raw_data = app->image->array.raw;
    }

    if (!raw_data) return;

    int width = app->image->md->size[0];
    int height = app->image->md->size[1];
    uint8_t datatype = app->image->md->datatype;
    app->img_width = width;
    app->img_height = height;

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, width);
    size_t required_size = stride * height;

    if (!app->display_buffer || app->display_buffer_size < required_size) {
        if (app->display_buffer) free(app->display_buffer);
        app->display_buffer = malloc(required_size);
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

    // Calculate Stats if selection active - NOW using updated current_min/max
    if (app->selection_active) {
        calculate_roi_stats(app, raw_data, width, height, datatype);
    }

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

            double norm = (val - min_val) / (max_val - min_val);
            if (norm < 0) norm = 0;
            if (norm > 1) norm = 1;
            uint8_t pixel_val = (uint8_t)(norm * 255.0);

            row[x] = (255 << 24) | (pixel_val << 16) | (pixel_val << 8) | pixel_val;
        }
    }

    gtk_widget_queue_draw(app->image_area);
}

static gboolean
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
        if (app->btn_fit_window) gtk_check_button_set_active(GTK_CHECK_BUTTON(app->btn_fit_window), TRUE);
    }

    // Update counter label
    char buf[64];
    snprintf(buf, sizeof(buf), "Counter: %lu", app->image->md->cnt0);
    gtk_label_set_text(GTK_LABEL(app->lbl_counter), buf);

    static uint64_t last_cnt0 = 0;

    if (app->image->md->cnt0 != last_cnt0 || app->force_redraw) {
        if (app->image->md->write) {
             return G_SOURCE_CONTINUE;
        }

        last_cnt0 = app->image->md->cnt0;
        app->force_redraw = FALSE;
        draw_image(app);
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

    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_window_set_child (GTK_WINDOW (window), hbox);

    // Sidebar Controls (Left)
    vbox_controls = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request (vbox_controls, 200, -1);
    gtk_widget_set_margin_start (vbox_controls, 10);
    gtk_widget_set_margin_end (vbox_controls, 10);
    gtk_widget_set_margin_top (vbox_controls, 10);
    gtk_widget_set_margin_bottom (vbox_controls, 10);
    gtk_box_append (GTK_BOX (hbox), vbox_controls);

    // Counter
    viewer->lbl_counter = gtk_label_new("Counter: 0");
    gtk_widget_set_halign(viewer->lbl_counter, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox_controls), viewer->lbl_counter);

    // FPS Control
    label = gtk_label_new("FPS");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox_controls), label);
    const char *fps_opts[] = {"10 Hz", "25 Hz", "50 Hz", "100 Hz", NULL};
    viewer->dropdown_fps = gtk_drop_down_new_from_strings(fps_opts);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_fps), 1); // 25Hz default
    g_signal_connect(viewer->dropdown_fps, "notify::selected", G_CALLBACK(on_fps_changed), viewer);
    gtk_box_append(GTK_BOX(vbox_controls), viewer->dropdown_fps);

    // Zoom Controls
    label = gtk_label_new ("Zoom");
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (vbox_controls), label);

    viewer->btn_fit_window = gtk_check_button_new_with_label ("Fit to Window");
    gtk_check_button_set_active (GTK_CHECK_BUTTON (viewer->btn_fit_window), TRUE);
    g_signal_connect (viewer->btn_fit_window, "toggled", G_CALLBACK (on_fit_window_toggled), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), viewer->btn_fit_window);

    const char *zoom_levels[] = {"1/8x", "1/4x", "1/2x", "1x", "2x", "4x", "8x", NULL};
    viewer->dropdown_zoom = gtk_drop_down_new_from_strings (zoom_levels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_zoom), 3);
    g_signal_connect (viewer->dropdown_zoom, "notify::selected", G_CALLBACK (on_dropdown_zoom_changed), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), viewer->dropdown_zoom);

    viewer->lbl_zoom = gtk_label_new ("Zoom: 100%");
    gtk_box_append (GTK_BOX (vbox_controls), viewer->lbl_zoom);

    // Min Controls
    label = gtk_label_new ("Min Value");
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (vbox_controls), label);

    viewer->check_min_auto = gtk_check_button_new_with_label ("Auto Min");
    g_signal_connect (viewer->check_min_auto, "toggled", G_CALLBACK (on_auto_min_toggled), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), viewer->check_min_auto);

    viewer->spin_min = gtk_spin_button_new_with_range (-1e20, 1e20, 1.0);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (viewer->spin_min), 2);
    g_signal_connect (viewer->spin_min, "value-changed", G_CALLBACK (on_spin_min_changed), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), viewer->spin_min);

    // Max Controls
    label = gtk_label_new ("Max Value");
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (vbox_controls), label);

    viewer->check_max_auto = gtk_check_button_new_with_label ("Auto Max");
    g_signal_connect (viewer->check_max_auto, "toggled", G_CALLBACK (on_auto_max_toggled), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), viewer->check_max_auto);

    viewer->spin_max = gtk_spin_button_new_with_range (-1e20, 1e20, 1.0);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (viewer->spin_max), 2);
    g_signal_connect (viewer->spin_max, "value-changed", G_CALLBACK (on_spin_max_changed), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), viewer->spin_max);

    // Buttons
    btn_autoscale = gtk_button_new_with_label ("Auto Scale");
    g_signal_connect (btn_autoscale, "clicked", G_CALLBACK (on_btn_autoscale_clicked), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), btn_autoscale);

    btn_reset_colorbar = gtk_button_new_with_label ("Reset Colorbar");
    g_signal_connect (btn_reset_colorbar, "clicked", G_CALLBACK (on_btn_reset_colorbar_clicked), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), btn_reset_colorbar);

    btn_reset = gtk_button_new_with_label ("Reset Selection");
    g_signal_connect (btn_reset, "clicked", G_CALLBACK (on_btn_reset_selection_clicked), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), btn_reset);

    viewer->check_histogram = gtk_check_button_new_with_label("Show Histogram");
    g_signal_connect(viewer->check_histogram, "toggled", G_CALLBACK(on_histogram_toggled), viewer);
    gtk_box_append(GTK_BOX(vbox_controls), viewer->check_histogram);


    // Image Display Area with Overlay (Center)
    scrolled_window = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (scrolled_window, TRUE);
    gtk_widget_set_hexpand (scrolled_window, TRUE);
    gtk_box_append (GTK_BOX (hbox), scrolled_window);

    scroll_controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect (scroll_controller, "scroll", G_CALLBACK (on_scroll), viewer);
    gtk_widget_add_controller (scrolled_window, scroll_controller);

    overlay = gtk_overlay_new();
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), overlay);

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

    drag_controller = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag_controller), 0);
    g_signal_connect(drag_controller, "drag-begin", G_CALLBACK(drag_begin), viewer);
    g_signal_connect(drag_controller, "drag-update", G_CALLBACK(drag_update), viewer);
    g_signal_connect(drag_controller, "drag-end", G_CALLBACK(drag_end), viewer);
    gtk_widget_add_controller(viewer->selection_area, GTK_EVENT_CONTROLLER(drag_controller));


    // Right Sidebar (Colorbar + Stats)
    hbox_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(hbox_right, 10);
    gtk_widget_set_margin_end(hbox_right, 10);
    gtk_widget_set_margin_top(hbox_right, 10);
    gtk_widget_set_margin_bottom(hbox_right, 10);
    gtk_box_append(GTK_BOX(hbox), hbox_right);

    // Colorbar
    viewer->colorbar = gtk_drawing_area_new ();
    gtk_widget_set_size_request (viewer->colorbar, 60, -1);
    gtk_widget_set_vexpand(viewer->colorbar, TRUE);
    gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (viewer->colorbar), draw_colorbar_func, viewer, NULL);
    gtk_box_append (GTK_BOX (hbox_right), viewer->colorbar);

    // Stats Box
    frame_stats = gtk_frame_new("ROI Stats");
    viewer->box_stats = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(viewer->box_stats, 5);
    gtk_widget_set_margin_end(viewer->box_stats, 5);
    gtk_widget_set_margin_top(viewer->box_stats, 5);
    gtk_widget_set_margin_bottom(viewer->box_stats, 5);
    gtk_frame_set_child(GTK_FRAME(frame_stats), viewer->box_stats);
    // Add stats frame to right of colorbar
    gtk_box_append(GTK_BOX(hbox_right), frame_stats);

    viewer->lbl_stat_min = gtk_label_new("Min: -");
    gtk_widget_set_halign(viewer->lbl_stat_min, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(viewer->box_stats), viewer->lbl_stat_min);

    viewer->lbl_stat_max = gtk_label_new("Max: -");
    gtk_widget_set_halign(viewer->lbl_stat_max, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(viewer->box_stats), viewer->lbl_stat_max);

    viewer->lbl_stat_mean = gtk_label_new("Mean: -");
    gtk_widget_set_halign(viewer->lbl_stat_mean, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(viewer->box_stats), viewer->lbl_stat_mean);

    viewer->lbl_stat_median = gtk_label_new("Median: -");
    gtk_widget_set_halign(viewer->lbl_stat_median, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(viewer->box_stats), viewer->lbl_stat_median);

    viewer->lbl_stat_p01 = gtk_label_new("P 0.1: -");
    gtk_widget_set_halign(viewer->lbl_stat_p01, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(viewer->box_stats), viewer->lbl_stat_p01);

    viewer->lbl_stat_p09 = gtk_label_new("P 0.9: -");
    gtk_widget_set_halign(viewer->lbl_stat_p09, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(viewer->box_stats), viewer->lbl_stat_p09);

    // Histogram
    viewer->histogram_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(viewer->histogram_area, 150, 100);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(viewer->histogram_area), draw_histogram_func, viewer, NULL);
    gtk_widget_set_visible(viewer->histogram_area, FALSE);
    gtk_box_append(GTK_BOX(viewer->box_stats), viewer->histogram_area);

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

    // Default 30ms = 33Hz
    viewer->timeout_id = g_timeout_add (30, update_display, viewer);

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

    app = gtk_application_new ("org.milk.shmimview", G_APPLICATION_NON_UNIQUE);
    g_signal_connect (app, "activate", G_CALLBACK (activate), &viewer);
    status = g_application_run (G_APPLICATION (app), 0, NULL);
    g_object_unref (app);

    if (viewer.image) {
        ImageStreamIO_closeIm(viewer.image);
        free(viewer.image);
    }
    if (viewer.display_buffer) free(viewer.display_buffer);
    if (viewer.hist_data) free(viewer.hist_data);

    return status;
}
