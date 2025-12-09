#include <gtk/gtk.h>
#include <ImageStreamIO/ImageStreamIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Application state
typedef struct {
    IMAGE *image;
    GtkWidget *picture;
    GtkWidget *colorbar;
    GtkWidget *selection_area;
    char *image_name;
    guint timeout_id;

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

    // Selection state
    gboolean selection_active;
    gboolean is_dragging;
    gboolean is_moving_selection;
    double start_x, start_y; // Widget coords
    double curr_x, curr_y;   // Widget coords

    // Selected region in image coordinates (pixels)
    int sel_x1, sel_y1, sel_x2, sel_y2;
    int sel_orig_x1, sel_orig_y1, sel_orig_x2, sel_orig_y2;

    // Zoom state
    gboolean fit_window;
    double zoom_factor; // 1.0 = 100%

    // UI Widgets
    GtkWidget *spin_min;
    GtkWidget *check_min_auto;
    GtkWidget *spin_max;
    GtkWidget *check_max_auto;

    GtkWidget *btn_fit_window;
    GtkWidget *dropdown_zoom;
    GtkWidget *lbl_zoom;
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

// UI Callbacks
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
on_btn_reset_selection_clicked (GtkButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->selection_active = FALSE;
    app->force_redraw = TRUE;
    gtk_widget_queue_draw(app->selection_area);
}

static void
on_fit_window_toggled (GtkCheckButton *btn, gpointer user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    app->fit_window = gtk_check_button_get_active(btn);

    if (!app->fit_window) {
        // Switching to fixed zoom. Initialize zoom_factor to current actual scale
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

    // Zoom levels: 1/8, 1/4, 1/2, 1, 2, 4, 8
    double zooms[] = {0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    if (selected < 7) {
        app->zoom_factor = zooms[selected];

        // Disable Fit Window if it was on
        if (app->fit_window) {
            // This will trigger on_fit_window_toggled loop, so block signal?
            // Or just update state.
            // Better to update toggle state which triggers the logic.
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

    // Check modifiers for Ctrl key (if desired, or just always zoom?)
    // Prompt said "Have mouse wheel roll change zoom ratio".
    // Standard behavior for image viewers often is Scroll to Zoom, Drag to Pan.
    // Since we use Drag for ROI, we rely on Scrollbars for Panning.
    // So Scroll for Zoom is acceptable.

    double zoom_step = 1.1;
    if (dy > 0) {
        app->zoom_factor /= zoom_step;
    } else if (dy < 0) {
        app->zoom_factor *= zoom_step;
    }

    // Disable Fit Window
    if (app->fit_window) {
        // Initialize zoom factor first
        double off_x, off_y, scale;
        get_image_screen_geometry(app, &off_x, &off_y, &scale);
        app->zoom_factor = scale;
        // Apply the scroll step
        if (dy > 0) app->zoom_factor /= zoom_step;
        else if (dy < 0) app->zoom_factor *= zoom_step;

        g_signal_handlers_block_by_func(app->btn_fit_window, on_fit_window_toggled, app);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(app->btn_fit_window), FALSE);
        app->fit_window = FALSE;
        g_signal_handlers_unblock_by_func(app->btn_fit_window, on_fit_window_toggled, app);
    }

    update_zoom_layout(app);
    return TRUE; // Handled
}

// Helpers for coordinate conversion
static void
get_image_screen_geometry(ViewerApp *app, double *offset_x, double *offset_y, double *scale) {
    if (!app->image) {
        *offset_x = 0; *offset_y = 0; *scale = 1.0;
        return;
    }

    // In Fixed Zoom mode, the widget size IS the image display size (mostly)
    // In Fit Window mode, the widget size IS the viewport size

    int widget_w = gtk_widget_get_width(app->picture);
    int widget_h = gtk_widget_get_height(app->picture);

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

    if (app->fit_window) {
        double scale_x = (double)widget_w / img_w;
        double scale_y = (double)widget_h / img_h;
        *scale = (scale_x < scale_y) ? scale_x : scale_y;
    } else {
        // In Fixed Zoom, we forced the widget size to match the zoom.
        // However, GtkPicture might center it if the widget allocation is larger (e.g. strict constraints).
        // But generally, the scaling is driven by our sizing.
        // If we use GTK_CONTENT_FIT_FILL, the image fills the widget.
        // So scale is widget_w / img_w.

        // Wait, if we zoomed OUT (0.1x), the widget is small.
        // If we zoom IN (10x), the widget is large.
        // We set the size request.
        *scale = (double)widget_w / img_w;
    }

    double display_w = img_w * (*scale);
    double display_h = img_h * (*scale);

    *offset_x = (widget_w - display_w) / 2.0;
    *offset_y = (widget_h - display_h) / 2.0;
}

static void
update_zoom_layout(ViewerApp *app) {
    if (!app->image) return;

    double img_w = (double)app->image->md->size[0];
    double img_h = (double)app->image->md->size[1];

    char buf[64];

    if (app->fit_window) {
        gtk_picture_set_content_fit(GTK_PICTURE(app->picture), GTK_CONTENT_FIT_CONTAIN);
        gtk_widget_set_size_request(app->picture, -1, -1);

        // Update label with current calculated scale
        double off_x, off_y, scale;
        get_image_screen_geometry(app, &off_x, &off_y, &scale);
        snprintf(buf, sizeof(buf), "Zoom: %.1f%%", scale * 100.0);
        gtk_label_set_text(GTK_LABEL(app->lbl_zoom), buf);

        // Ensure overlay redraws selection in new place
        gtk_widget_queue_draw(app->selection_area);
    } else {
        // Fixed zoom
        int req_w = (int)(img_w * app->zoom_factor);
        int req_h = (int)(img_h * app->zoom_factor);

        // Use FILL to force image to occupy the requested size (scaling handled by GTK)
        gtk_picture_set_content_fit(GTK_PICTURE(app->picture), GTK_CONTENT_FIT_FILL);
        gtk_widget_set_size_request(app->picture, req_w, req_h);

        snprintf(buf, sizeof(buf), "Zoom: %.1f%%", app->zoom_factor * 100.0);
        gtk_label_set_text(GTK_LABEL(app->lbl_zoom), buf);

        gtk_widget_queue_draw(app->selection_area);
    }
}

// Callback for picture resize (to update zoom label in Fit Window mode)
static void
on_picture_resize (GtkWidget *widget, gpointer user_data)
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
        // Use current modified position
        x1 = app->sel_x1 * scale + offset_x;
        y1 = app->sel_y1 * scale + offset_y;
        x2 = app->sel_x2 * scale + offset_x;
        y2 = app->sel_y2 * scale + offset_y;
        x2 += scale;
        y2 += scale;
    } else if (app->is_dragging) {
        // Drawing new selection
        x1 = app->start_x;
        y1 = app->start_y;
        x2 = app->curr_x;
        y2 = app->curr_y;
    } else {
        // Convert image coords back to widget coords for display
        x1 = app->sel_x1 * scale + offset_x;
        y1 = app->sel_y1 * scale + offset_y;
        x2 = app->sel_x2 * scale + offset_x;
        y2 = app->sel_y2 * scale + offset_y;
        x2 += scale; // Make it cover the pixel
        y2 += scale;
    }

    // Removed transparent fill

    cairo_set_source_rgb(cr, 1, 0, 0); // Red outline
    cairo_set_line_width(cr, 2);
    cairo_rectangle(cr, x1, y1, x2 - x1, y2 - y1);
    cairo_stroke(cr);
}

// Gesture callbacks
static void
drag_begin (GtkGestureDrag *gesture,
            double          x,
            double          y,
            gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;
    int ix, iy;
    widget_to_image_coords(app, x, y, &ix, &iy);

    // Check if clicking inside existing selection
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
        app->selection_active = FALSE; // Clear old
    }

    app->start_x = x;
    app->start_y = y;
    app->curr_x = x;
    app->curr_y = y;

    app->force_redraw = TRUE;
    gtk_widget_queue_draw(app->selection_area);
}

static void
drag_update (GtkGestureDrag *gesture,
             double          offset_x,
             double          offset_y,
             gpointer        user_data)
{
    ViewerApp *app = (ViewerApp *)user_data;

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

        // Clamp
        if (app->sel_x1 < 0) app->sel_x1 = 0;
        if (app->sel_y1 < 0) app->sel_y1 = 0;
        if (app->sel_x1 + w >= img_w) app->sel_x1 = img_w - 1 - w;
        if (app->sel_y1 + h >= img_h) app->sel_y1 = img_h - 1 - h;

        app->sel_x2 = app->sel_x1 + w;
        app->sel_y2 = app->sel_y1 + h;

        app->force_redraw = TRUE; // Update scaling live
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

    if (app->is_moving_selection) {
        app->is_moving_selection = FALSE;
        app->selection_active = TRUE; // Ensure it stays active
    } else if (app->is_dragging) {
        app->is_dragging = FALSE;
        app->curr_x = app->start_x + offset_x;
        app->curr_y = app->start_y + offset_y;

        // If drag is very small, maybe treat as clear?
        if (fabs(offset_x) < 2 && fabs(offset_y) < 2) {
            app->selection_active = FALSE;
        } else {
            app->selection_active = TRUE;

            int ix1, iy1, ix2, iy2;
            widget_to_image_coords(app, app->start_x, app->start_y, &ix1, &iy1);
            widget_to_image_coords(app, app->curr_x, app->curr_y, &ix2, &iy2);

            app->sel_x1 = (ix1 < ix2) ? ix1 : ix2;
            app->sel_x2 = (ix1 < ix2) ? ix2 : ix1;
            app->sel_y1 = (iy1 < iy2) ? iy1 : iy2;
            app->sel_y2 = (iy1 < iy2) ? iy2 : iy1;
        }
    }

    app->force_redraw = TRUE;
    gtk_widget_queue_draw(app->selection_area);
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

    int stride = width * 4;
    guchar *pixels = g_malloc (stride * height);

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
            // Clamp
            if (x_start < 0) x_start = 0;
            if (y_start < 0) y_start = 0;
            if (x_end > width) x_end = width;
            if (y_end > height) y_end = height;
        }

        for (int y = y_start; y < y_end; y++) {
            for (int x = x_start; x < x_end; x++) {
                int i = y * width + x;
                double val = 0;
                if (datatype == _DATATYPE_FLOAT) {
                    val = ((float*)raw_data)[i];
                } else if (datatype == _DATATYPE_DOUBLE) {
                    val = ((double*)raw_data)[i];
                } else if (datatype == _DATATYPE_UINT8) {
                    val = ((uint8_t*)raw_data)[i];
                } else if (datatype == _DATATYPE_INT16) {
                     val = ((int16_t*)raw_data)[i];
                } else if (datatype == _DATATYPE_UINT16) {
                     val = ((uint16_t*)raw_data)[i];
                } else if (datatype == _DATATYPE_INT32) {
                     val = ((int32_t*)raw_data)[i];
                } else if (datatype == _DATATYPE_UINT32) {
                     val = ((uint32_t*)raw_data)[i];
                }

                if (val < min_val) min_val = val;
                if (val > max_val) max_val = val;
            }
        }

        // Handle case where selection might be empty or invalid values found
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

    for (int y = 0; y < height; y++) {
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

            if (val < min_val) val = min_val;
            if (val > max_val) val = max_val;

            uint8_t pixel_val = (uint8_t)((val - min_val) / (max_val - min_val) * 255.0);

            int p_idx = (y * width + x) * 4;
            pixels[p_idx + 0] = pixel_val;
            pixels[p_idx + 1] = pixel_val;
            pixels[p_idx + 2] = pixel_val;
            pixels[p_idx + 3] = 255;
        }
    }

    GBytes *bytes = g_bytes_new_take (pixels, stride * height);
    GdkTexture *texture = gdk_memory_texture_new (width, height, GDK_MEMORY_R8G8B8A8, bytes, stride);
    g_bytes_unref (bytes);

    gtk_picture_set_paintable (GTK_PICTURE (app->picture), GDK_PAINTABLE (texture));
    g_object_unref (texture);

    // Initial zoom layout on first load if fit_window
    if (app->fit_window && gtk_widget_get_width(app->picture) > 0) {
        update_zoom_layout(app);
    }
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
        // Reset zoom on load
        app->fit_window = TRUE;
        if (app->btn_fit_window) gtk_check_button_set_active(GTK_CHECK_BUTTON(app->btn_fit_window), TRUE);
    }

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
    GtkWidget *scrolled_window;
    GtkWidget *overlay;
    GtkWidget *label;
    GtkWidget *btn_autoscale;
    GtkWidget *btn_reset;
    GtkGesture *drag_controller;
    GtkEventController *scroll_controller;

    window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (window), "ImageStreamIO Viewer");
    gtk_window_set_default_size (GTK_WINDOW (window), 1000, 700);

    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_window_set_child (GTK_WINDOW (window), hbox);

    // Sidebar Controls
    vbox_controls = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request (vbox_controls, 200, -1);
    gtk_widget_set_margin_start (vbox_controls, 10);
    gtk_widget_set_margin_end (vbox_controls, 10);
    gtk_widget_set_margin_top (vbox_controls, 10);
    gtk_widget_set_margin_bottom (vbox_controls, 10);
    gtk_box_append (GTK_BOX (hbox), vbox_controls);

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
    gtk_drop_down_set_selected(GTK_DROP_DOWN(viewer->dropdown_zoom), 3); // 1x
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

    // Auto Scale Button
    btn_autoscale = gtk_button_new_with_label ("Auto Scale");
    g_signal_connect (btn_autoscale, "clicked", G_CALLBACK (on_btn_autoscale_clicked), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), btn_autoscale);

    // Reset Selection Button
    btn_reset = gtk_button_new_with_label ("Reset Selection");
    g_signal_connect (btn_reset, "clicked", G_CALLBACK (on_btn_reset_selection_clicked), viewer);
    gtk_box_append (GTK_BOX (vbox_controls), btn_reset);


    // Image Display Area with Overlay
    scrolled_window = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (scrolled_window, TRUE);
    gtk_widget_set_hexpand (scrolled_window, TRUE);
    gtk_box_append (GTK_BOX (hbox), scrolled_window);

    // Add scroll controller for zoom
    scroll_controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect (scroll_controller, "scroll", G_CALLBACK (on_scroll), viewer);
    gtk_widget_add_controller (scrolled_window, scroll_controller);

    overlay = gtk_overlay_new();
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), overlay);

    viewer->picture = gtk_picture_new ();
    // Use resize signal to update zoom label when Fit Window is on
    g_signal_connect(viewer->picture, "resize", G_CALLBACK(on_picture_resize), viewer);

    // Add Picture as child
    gtk_overlay_set_child(GTK_OVERLAY(overlay), viewer->picture);

    // Selection Overlay (Drawing Area)
    viewer->selection_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(viewer->selection_area, TRUE);
    gtk_widget_set_vexpand(viewer->selection_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(viewer->selection_area), draw_selection_func, viewer, NULL);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), viewer->selection_area);

    // Gestures for dragging selection
    drag_controller = gtk_gesture_drag_new();
    g_signal_connect(drag_controller, "drag-begin", G_CALLBACK(drag_begin), viewer);
    g_signal_connect(drag_controller, "drag-update", G_CALLBACK(drag_update), viewer);
    g_signal_connect(drag_controller, "drag-end", G_CALLBACK(drag_end), viewer);
    gtk_widget_add_controller(viewer->selection_area, GTK_EVENT_CONTROLLER(drag_controller));


    // Colorbar
    viewer->colorbar = gtk_drawing_area_new ();
    gtk_widget_set_size_request (viewer->colorbar, 60, -1);
    gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (viewer->colorbar), draw_colorbar_func, viewer, NULL);
    gtk_box_append (GTK_BOX (hbox), viewer->colorbar);

    // Initialize UI State based on CLI args
    gtk_check_button_set_active (GTK_CHECK_BUTTON (viewer->check_min_auto), !viewer->fixed_min);
    gtk_check_button_set_active (GTK_CHECK_BUTTON (viewer->check_max_auto), !viewer->fixed_max);

    gtk_widget_set_sensitive (viewer->spin_min, viewer->fixed_min);
    gtk_widget_set_sensitive (viewer->spin_max, viewer->fixed_max);

    if (viewer->fixed_min) gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->spin_min), viewer->min_val);
    if (viewer->fixed_max) gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->spin_max), viewer->max_val);

    viewer->fit_window = TRUE;
    viewer->zoom_factor = 1.0;

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

    return status;
}
