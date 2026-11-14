// SPDX-License-Identifier: MIT
#include "as-ui.h"

#include <string.h>

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
    if (index < G_N_ELEMENTS(rates)) {
        return rates[index];
    }
    return -1;
}

GtkStringList *as_ui_light_list(void)
{
    GtkStringList *list = gtk_string_list_new(NULL);

    for (int i = 0; i < as_light_mode_count(); i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d · %s", i, as_light_mode_name(i));
        gtk_string_list_append(list, buf);
    }
    return list;
}

const char *as_ui_mag_mode_name(int mode)
{
    static const char *const names[] = { "comfort", "sensitive", "gaming",
                                        "custom" };

    if (mode >= 0 && mode < (int)G_N_ELEMENTS(names)) {
        return names[mode];
    }
    return "custom";
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

AdwSwitchRow *as_ui_switch_row(const char *title, const char *subtitle)
{
    AdwSwitchRow *row = ADW_SWITCH_ROW(adw_switch_row_new());

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }
    return row;
}

void as_ui_combo_set(AdwComboRow *row, gboolean *guard, int index)
{
    *guard = TRUE;
    if (index >= 0) {
        adw_combo_row_set_selected(row, (guint)index);
    }
    *guard = FALSE;
}

void as_ui_spin_set(AdwSpinRow *row, gboolean *guard, double value)
{
    *guard = TRUE;
    adw_spin_row_set_value(row, value);
    *guard = FALSE;
}

void as_ui_switch_set(AdwSwitchRow *row, gboolean *guard, gboolean active)
{
    *guard = TRUE;
    adw_switch_row_set_active(row, active);
    *guard = FALSE;
}

void as_ui_color_set(GtkColorDialogButton *button, gboolean *guard,
                     const uint8_t rgb[3])
{
    GdkRGBA rgba = as_ui_rgba_from_rgb(rgb);

    *guard = TRUE;
    gtk_color_dialog_button_set_rgba(button, &rgba);
    *guard = FALSE;
}

char *as_ui_mouse_summary(const AsDevice *dev)
{
    GString *s = g_string_new(NULL);

    if (dev->error[0]) {
        g_string_append(s, dev->error);
        return g_string_free(s, FALSE);
    }
    g_string_append(s, dev->online ? "связь есть" : "связь потеряна");
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
        g_string_append(s, dev->error);
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
