// SPDX-License-Identifier: MIT
#include "as-ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WRITE_DELAY_MS 350

static const int rates[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };

GtkStringList *as_ui_rate_list(void)
{
    GtkStringList *list = gtk_string_list_new(NULL);

    for (size_t i = 0; i < G_N_ELEMENTS(rates); i++) {
        char buf[32];

        snprintf(buf, sizeof(buf), "%d Hz", rates[i]);
        gtk_string_list_append(list, buf);
    }
    return list;
}

int as_ui_rate_to_index(int hz)
{
    for (size_t i = 0; i < G_N_ELEMENTS(rates); i++) {
        if (rates[i] == hz) {
            return (int)i;
        }
    }
    return -1;
}

int as_ui_rate_from_index(guint index)
{
    return index < G_N_ELEMENTS(rates) ? rates[index] : -1;
}

GtkStringList *as_ui_light_list_for(int side, int *modes, int max)
{
    static const int side_modes[] = { 1, 2, 3, 4, 5, 20, 21, 22 };
    GtkStringList *list = gtk_string_list_new(NULL);
    int n = 0;

    if (side) {
        for (size_t i = 0; i < G_N_ELEMENTS(side_modes) && n < max; i++) {
            modes[n++] = side_modes[i];
        }
    } else {
        for (int m = 1; m < as_light_mode_count() && n < max; m++) {
            modes[n++] = m;
        }
    }
    for (int i = 0; i < n; i++) {
        char buf[64];

        snprintf(buf, sizeof(buf), "%d · %s", modes[i],
                 as_light_mode_name(modes[i]));
        gtk_string_list_append(list, buf);
    }
    return list;
}

int as_ui_light_index(const int *modes, int count, int mode)
{
    for (int i = 0; i < count; i++) {
        if (modes[i] == mode) {
            return i;
        }
    }
    return -1;
}

const char *as_ui_mag_mode_name(int mode)
{
    static const char *const names[] = { "comfort", "sensitive", "gaming",
                                        "custom" };

    return mode >= 0 && mode < (int)G_N_ELEMENTS(names) ? names[mode]
                                                       : "custom";
}

int as_ui_mag_mode_count(void)
{
    return 4;
}

GdkRGBA as_ui_rgba_from_rgb(const uint8_t rgb[3])
{
    GdkRGBA c = { rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0, 1.0 };

    return c;
}

void as_ui_rgb_from_rgba(const GdkRGBA *rgba, uint8_t rgb[3])
{
    rgb[0] = (uint8_t)(rgba->red * 255.0 + 0.5);
    rgb[1] = (uint8_t)(rgba->green * 255.0 + 0.5);
    rgb[2] = (uint8_t)(rgba->blue * 255.0 + 0.5);
}

/* The sensor accepts 50..30000 in steps of 50 and 30100..60000 in steps of
 * 100; anything else is rejected by the driver. */
unsigned as_ui_snap_dpi(unsigned dpi)
{
    unsigned v;

    if (dpi < 50) {
        return 50;
    }
    if (dpi > 60000) {
        return 60000;
    }
    if (dpi <= 30000) {
        v = 50 + ((dpi - 50 + 25) / 50) * 50;
        return v > 30000 ? 30000 : v;
    }
    if (dpi < 30100) {
        return 30100;
    }
    v = ((dpi + 50) / 100) * 100;
    return v > 60000 ? 60000 : v;
}

AdwActionRow *as_ui_value_row(const char *title)
{
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    return row;
}

void as_ui_row_set_value(AdwActionRow *row, const char *value)
{
    adw_action_row_set_subtitle(row, value);
}

void as_ui_row_format(AdwActionRow *row, const char *fmt, ...)
{
    char buf[192];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    adw_action_row_set_subtitle(row, buf);
}

AdwSwitchRow *as_ui_switch_row(const char *title, const char *subtitle)
{
    AdwSwitchRow *row = ADW_SWITCH_ROW(adw_switch_row_new());

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }
    return row;
}

AdwPreferencesGroup *as_ui_group(const char *title, const char *description)
{
    AdwPreferencesGroup *group =
        ADW_PREFERENCES_GROUP(adw_preferences_group_new());

    adw_preferences_group_set_title(group, title);
    if (description) {
        adw_preferences_group_set_description(group, description);
    }
    return group;
}

AdwComboRow *as_ui_combo(const char *title, GListModel *model)
{
    AdwComboRow *row = ADW_COMBO_ROW(adw_combo_row_new());

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_combo_row_set_model(row, model);
    return row;
}

/* ------------------------------ slider row ----------------------------- */

typedef struct {
    double min, max, step;
    double *value;
    void (*changed)(gpointer);
    gpointer user_data;
    GtkScale *scale;
    AdwActionRow *row;
} SliderData;

static void slider_show(SliderData *s)
{
    char buf[32];

    g_snprintf(buf, sizeof(buf), "%.2f мм", *s->value);
    adw_action_row_set_subtitle(s->row, buf);
    if (gtk_range_get_value(GTK_RANGE(s->scale)) != *s->value) {
        gtk_range_set_value(GTK_RANGE(s->scale), *s->value);
    }
}

static void slider_changed(GtkRange *range, gpointer data)
{
    SliderData *s = data;
    double value = gtk_range_get_value(range);

    if (value == *s->value) {
        return;
    }
    *s->value = value;
    slider_show(s);
    if (s->changed) {
        s->changed(s->user_data);
    }
}

static void slider_step(GtkButton *button, gpointer data)
{
    SliderData *s = data;
    double delta = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),
                                                     "as-delta")) * s->step;
    double value = *s->value + delta;

    if (value < s->min) {
        value = s->min;
    }
    if (value > s->max) {
        value = s->max;
    }
    /* Keep steps on the grid so the same button always adds the same amount. */
    value = s->min + ((int)((value - s->min) / s->step + 0.5)) * s->step;
    if (value < s->min) {
        value = s->min;
    }
    if (value > s->max) {
        value = s->max;
    }
    if (value == *s->value) {
        return;
    }
    gtk_range_set_value(GTK_RANGE(s->scale), value);
}

static void slider_free(gpointer data)
{
    g_free(data);
}

AdwActionRow *as_ui_slider_row(const char *title, double min, double max,
                               double step, double value, double *out,
                               void (*changed)(gpointer), gpointer user_data)
{
    SliderData *s = g_new0(SliderData, 1);
    AdwActionRow *row = as_ui_value_row(title);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *minus = gtk_button_new_from_icon_name("value-decrease-symbolic");
    GtkWidget *plus = gtk_button_new_from_icon_name("value-increase-symbolic");

    s->min = min;
    s->max = max;
    s->step = step;
    s->value = out;
    s->changed = changed;
    s->user_data = user_data;
    s->row = row;
    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }
    *out = value;

    s->scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                  min, max, step));
    gtk_widget_set_size_request(GTK_WIDGET(s->scale), 220, -1);
    gtk_scale_set_draw_value(s->scale, FALSE);
    gtk_widget_set_valign(GTK_WIDGET(s->scale), GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(minus), "as-delta", GINT_TO_POINTER(-1));
    g_object_set_data(G_OBJECT(plus), "as-delta", GINT_TO_POINTER(1));
    gtk_widget_add_css_class(minus, "flat");
    gtk_widget_add_css_class(plus, "flat");
    g_signal_connect(minus, "clicked", G_CALLBACK(slider_step), s);
    g_signal_connect(plus, "clicked", G_CALLBACK(slider_step), s);
    g_signal_connect(s->scale, "value-changed", G_CALLBACK(slider_changed), s);

    gtk_box_append(GTK_BOX(box), minus);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(s->scale));
    gtk_box_append(GTK_BOX(box), plus);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(row, box);
    slider_show(s);
    g_object_set_data_full(G_OBJECT(row), "as-slider", s, slider_free);
    return row;
}

void as_ui_slider_set(AdwActionRow *row, double value)
{
    SliderData *s = g_object_get_data(G_OBJECT(row), "as-slider");

    if (!s) {
        return;
    }
    if (value < s->min) {
        value = s->min;
    }
    if (value > s->max) {
        value = s->max;
    }
    *s->value = value;
    slider_show(s);
}

AdwSpinRow *as_ui_spin(const char *title, double min, double max, double step,
                       const char *subtitle)
{
    AdwSpinRow *row = ADW_SPIN_ROW(adw_spin_row_new_with_range(min, max, step));

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_spin_row_set_digits(row, 0);
    if (subtitle) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }
    return row;
}

/* The guard must be restored, not cleared: the setter is normally called from
 * update_page(), which already holds it. Clearing it there let the following
 * programmatic changes look like user input and write to the device. */
#define GUARDED(guard, statement)                                              \
    G_STMT_START                                                               \
    {                                                                          \
        gboolean previous__ = *(guard);                                        \
        *(guard) = TRUE;                                                       \
        statement;                                                             \
        *(guard) = previous__;                                                 \
    }                                                                          \
    G_STMT_END

void as_ui_combo_set(AdwComboRow *row, gboolean *guard, int index)
{
    GUARDED(guard, adw_combo_row_set_selected(
                       row, index >= 0 ? (guint)index
                                       : GTK_INVALID_LIST_POSITION));
}

void as_ui_spin_set(AdwSpinRow *row, gboolean *guard, double value)
{
    GUARDED(guard, adw_spin_row_set_value(row, value));
}

void as_ui_switch_set(AdwSwitchRow *row, gboolean *guard, gboolean active)
{
    GUARDED(guard, adw_switch_row_set_active(row, active));
}

void as_ui_color_set(GtkColorDialogButton *button, gboolean *guard,
                     const uint8_t rgb[3])
{
    GdkRGBA rgba = as_ui_rgba_from_rgb(rgb);

    GUARDED(guard, gtk_color_dialog_button_set_rgba(button, &rgba));
}

const char *as_ui_error(const AsDevice *dev)
{
    if (dev->permission_denied) {
        return "Нет доступа к hidraw: нажмите «Исправить» в шапке окна";
    }
    return dev->error;
}

char *as_ui_mouse_summary(const AsDevice *dev)
{
    GString *s = g_string_new(NULL);

    if (dev->error[0]) {
        g_string_append(s, as_ui_error(dev));
        return g_string_free(s, FALSE);
    }
    g_string_append(s, dev->online ? "связь есть" : "спит или выключена");
    if (dev->battery > 0) {
        g_string_append_printf(s, " · заряд %d%%", dev->battery);
    }
    if (dev->rate > 0) {
        g_string_append_printf(s, " · %d Hz", dev->rate);
    }
    return g_string_free(s, FALSE);
}

char *as_ui_keyboard_summary(const AsDevice *dev)
{
    GString *s = g_string_new(NULL);

    if (dev->error[0]) {
        g_string_append(s, as_ui_error(dev));
        return g_string_free(s, FALSE);
    }
    if (dev->rate > 0) {
        g_string_append_printf(s, "%d Hz", dev->rate);
    }
    if (dev->profile >= 0) {
        g_string_append_printf(s, "%sпрофиль %d", s->len ? " · " : "",
                               dev->profile + 1);
    }
    if (dev->mag_mode >= 0) {
        g_string_append_printf(s, " · %s", as_ui_mag_mode_name(dev->mag_mode));
    }
    return g_string_free(s, FALSE);
}

/* --------------------------- device pages --------------------------- */

static gboolean pending_timeout(gpointer data)
{
    AsPage *page = data;

    page->pending_id = 0;
    as_page_flush(page);
    return G_SOURCE_REMOVE;
}

void as_page_flush(AsPage *page)
{
    if (page->pending_id) {
        g_source_remove(page->pending_id);
        page->pending_id = 0;
    }
    if (page->pending_valid) {
        page->pending_valid = FALSE;
        as_store_apply(page->store, page->path, &page->pending);
    }
}

void as_page_queue(AsPage *page, const AsOpRequest *req)
{
    page->pending = *req;
    page->pending_valid = TRUE;
    if (page->pending_id) {
        g_source_remove(page->pending_id);
    }
    page->pending_id = g_timeout_add(WRITE_DELAY_MS, pending_timeout, page);
}

void as_page_free(AsPage *page)
{
    as_page_flush(page);
    g_free(page->path);
    g_free(page);
}

AsDevice *as_page_device(AsPage *page)
{
    return as_store_lookup(page->store, page->path);
}

void as_page_banner(AsPage *page, const char *text)
{
    if (text) {
        adw_banner_set_title(page->banner, text);
    }
    adw_banner_set_revealed(page->banner, text != NULL);
}

void as_page_attach(AsPage *page, GtkWidget *box, AdwPreferencesPage *prefs,
                    AsStore *store, const char *path)
{
    page->store = store;
    page->path = g_strdup(path);
    page->banner = ADW_BANNER(adw_banner_new(""));
    adw_banner_set_revealed(page->banner, FALSE);

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(page->banner));
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(prefs));
    gtk_widget_set_vexpand(GTK_WIDGET(prefs), TRUE);

    g_object_set_data(G_OBJECT(box), "as-page", page);
    g_object_set_data_full(G_OBJECT(box), "as-page-data", page,
                           (GDestroyNotify)as_page_free);
}
