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

/* Hashing the bits of the double keeps two values that differ below the
 * hundredths the labels show apart, so a write always refreshes the widgets. */
static guint32 hash_mm(guint32 h, double mm)
{
    guint64 bits;

    memcpy(&bits, &mm, sizeof(bits));
    h = h * 31u + (guint32)(bits >> 32);
    return h * 31u + (guint32)bits;
}

guint32 as_ui_device_signature(const AsDevice *dev)
{
    guint32 h = dev->key_code_valid ? 1u : 2u;

    for (int i = 0; i < AS_KB_KEY_MAX; i++) {
        h = h * 31u + dev->key_code[i];
        h = hash_mm(h, dev->key_actuation[i]);
        h = hash_mm(h, dev->key_release[i]);
        h = hash_mm(h, dev->key_rt_press[i]);
        h = hash_mm(h, dev->key_rt_release[i]);
        h = hash_mm(h, dev->key_deadzone[i]);
        h = h * 31u + dev->key_option[i];
        h = h * 31u + dev->key_pair[i];
    }
    return h;
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
unsigned as_ui_hid_from_keyval(guint keyval)
{
    guint lower = gdk_keyval_to_lower(keyval);

    if (lower >= GDK_KEY_a && lower <= GDK_KEY_z) {
        return 4u + (lower - GDK_KEY_a);
    }
    if (lower >= GDK_KEY_0 && lower <= GDK_KEY_9) {
        return lower == GDK_KEY_0 ? 39u : 30u + (lower - GDK_KEY_1);
    }
    if (lower >= GDK_KEY_F1 && lower <= GDK_KEY_F12) {
        return 58u + (lower - GDK_KEY_F1);
    }
    switch (lower) {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter: return 40;
    case GDK_KEY_Escape: return 41;
    case GDK_KEY_BackSpace: return 42;
    case GDK_KEY_Tab: return 43;
    case GDK_KEY_space: return 44;
    case GDK_KEY_minus: return 45;
    case GDK_KEY_equal: return 46;
    case GDK_KEY_bracketleft: return 47;
    case GDK_KEY_bracketright: return 48;
    case GDK_KEY_backslash: return 49;
    case GDK_KEY_semicolon: return 51;
    case GDK_KEY_apostrophe: return 52;
    case GDK_KEY_grave: return 53;
    case GDK_KEY_comma: return 54;
    case GDK_KEY_period: return 55;
    case GDK_KEY_slash: return 56;
    case GDK_KEY_Caps_Lock: return 57;
    case GDK_KEY_Print: return 70;
    case GDK_KEY_Scroll_Lock: return 71;
    case GDK_KEY_Pause: return 72;
    case GDK_KEY_Insert: return 73;
    case GDK_KEY_Home: return 74;
    case GDK_KEY_Page_Up: return 75;
    case GDK_KEY_Delete: return 76;
    case GDK_KEY_End: return 77;
    case GDK_KEY_Page_Down: return 78;
    case GDK_KEY_Right: return 79;
    case GDK_KEY_Left: return 80;
    case GDK_KEY_Down: return 81;
    case GDK_KEY_Up: return 82;
    case GDK_KEY_Num_Lock: return 83;
    case GDK_KEY_KP_Divide: return 84;
    case GDK_KEY_Control_L: return 224;
    case GDK_KEY_Shift_L: return 225;
    case GDK_KEY_Alt_L: return 226;
    case GDK_KEY_Super_L: return 227;
    case GDK_KEY_Control_R: return 228;
    case GDK_KEY_Shift_R: return 229;
    case GDK_KEY_Alt_R: return 230;
    case GDK_KEY_Super_R: return 231;
    default:
        return 0;
    }
}

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
    GtkLabel *label;
    AdwActionRow *row;
} SliderData;

static void slider_show(SliderData *s)
{
    char buf[32], hint[96];

    g_snprintf(buf, sizeof(buf), "%.2f мм", *s->value);
    gtk_label_set_text(s->label, buf);
    g_snprintf(hint, sizeof(hint), "%s: %.2f мм, диапазон %.2f–%.2f мм",
               adw_preferences_row_get_title(ADW_PREFERENCES_ROW(s->row)),
               *s->value, s->min, s->max);
    gtk_widget_set_tooltip_text(GTK_WIDGET(s->row), hint);
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
    GtkWidget *value_label = gtk_label_new("");

    s->min = min;
    s->max = max;
    s->step = step;
    s->value = out;
    s->changed = changed;
    s->user_data = user_data;
    s->row = row;
    {
        char range[48];

        g_snprintf(range, sizeof(range), "диапазон %.2f–%.2f мм", min, max);
        adw_action_row_set_subtitle(row, range);
    }
    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }
    *out = value;

    s->scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                  min, max, step));
    s->label = GTK_LABEL(value_label);
    gtk_label_set_width_chars(s->label, 7);
    gtk_widget_set_size_request(GTK_WIDGET(s->scale), 160, -1);
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
    gtk_box_append(GTK_BOX(box), value_label);
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
        g_string_append_printf(s, " · %s",
                               as_kb_mag_mode_name(dev->mag_mode));
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
    for (int i = 0; i < page->pending_count; i++) {
        as_store_apply(page->store, page->path, &page->pending[i]);
    }
    page->pending_count = 0;
}

void as_page_queue(AsPage *page, const AsOpRequest *req)
{
    int slot = -1;

    for (int i = 0; i < page->pending_count; i++) {
        if (page->pending[i].op == req->op &&
            ((req->op != AS_OP_MOUSE_DPI_STAGE && req->op != AS_OP_KB_SNAP) ||
             page->pending[i].index == req->index)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (page->pending_count == AS_PAGE_PENDING_MAX) {
            as_page_flush(page);
        }
        slot = page->pending_count++;
    }
    page->pending[slot] = *req;
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
