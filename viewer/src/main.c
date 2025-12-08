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

    // UI Widgets
    GtkWidget *spin_min;
    GtkWidget *check_min_auto;
    GtkWidget *spin_max;
    GtkWidget *check_max_auto;
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
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->check_min_auto), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->check_max_auto), TRUE);
    app->force_redraw = TRUE;
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

    // Draw Border
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    cairo_rectangle (cr, bar_x, margin_top, bar_width, bar_height);
    cairo_stroke(cr);

    // Draw Labels
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);

    char buf[64];
    cairo_text_extents_t extents;

    // Max Value (Top)
    snprintf(buf, sizeof(buf), "%.2g", app->current_max);
    cairo_text_extents(cr, buf, &extents);
    cairo_move_to(cr, (width - extents.width)/2, margin_top - 5);
    cairo_show_text(cr, buf);

    // Min Value (Bottom)
    snprintf(buf, sizeof(buf), "%.2g", app->current_min);
    cairo_text_extents(cr, buf, &extents);
    cairo_move_to(cr, (width - extents.width)/2, height - margin_bottom + extents.height + 5);
    cairo_show_text(cr, buf);
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
        for (int i = 0; i < width * height; i++) {
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

    if (app->fixed_min) min_val = app->min_val;
    if (app->fixed_max) max_val = app->max_val;

    if (max_val == min_val) max_val = min_val + 1.0;

    // Update current scaling values for colorbar
    app->current_min = min_val;
    app->current_max = max_val;

    // Trigger colorbar redraw
    if (app->colorbar) {
        gtk_widget_queue_draw(app->colorbar);
    }

    // Update spin buttons if in auto mode so user sees the range
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
    GtkWidget *label;
    GtkWidget *btn_autoscale;

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


    // Image Display
    scrolled_window = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (scrolled_window, TRUE);
    gtk_widget_set_hexpand (scrolled_window, TRUE);
    gtk_box_append (GTK_BOX (hbox), scrolled_window);

    viewer->picture = gtk_picture_new ();
    gtk_picture_set_content_fit (GTK_PICTURE (viewer->picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), viewer->picture);

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
