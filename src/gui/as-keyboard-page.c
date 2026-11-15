// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "as-ui.h"

/*
 * The R85 HE stores per-key lighting settings but ignores them: writing a
 * mode, a colour or a brightness there changes nothing on the keyboard. The
 * edge lighting is a separate zone with its own command and it does respond,
 * so the page exposes that zone and uses its "off" mode as the master switch.
 */
typedef struct {
    AsPage page;
    AdwActionRow *row_firmware;
    AdwActionRow *row_revision;
    AdwActionRow *row_profile;
    AdwComboRow *row_rate_combo;
    AdwSpinRow *row_debounce_spin;
    AdwComboRow *row_mag_combo;
    AdwSpinRow *row_actuation;
    AdwSpinRow *row_release;
    AdwSpinRow *row_rt_press;
    AdwSpinRow *row_rt_release;
    AdwSpinRow *row_deadzone;
    AdwSwitchRow *sw_rapid_trigger;
    AdwSwitchRow *sw_light;
    AdwComboRow *row_light_mode;
    AdwSpinRow *row_bright;
    AdwSpinRow *row_speed;
    AdwSwitchRow *sw_dazzle;
    GtkColorDialogButton *color;
    AdwPreferencesGroup *light_group;
    int light_modes[16];
    int light_mode_count;
    AdwSwitchRow *sw_side_light;
    AdwComboRow *row_side_mode;
    AdwSpinRow *row_side_bright;
    AdwSpinRow *row_side_speed;
    AdwSwitchRow *sw_side_dazzle;
    GtkColorDialogButton *side_color;
    AdwPreferencesGroup *side_light_group;
    int side_modes[16];
    int side_mode_count;

    /* One key at a time. The page keeps the matrix positions of the two
     * combos because the combos are ordered by the physical rows of the
     * matrix instead of by position number. */
    AdwComboRow *row_key_select;
    AdwSpinRow *row_key_actuation;
    AdwSpinRow *row_key_release;
    AdwSpinRow *row_key_rt_press;
    AdwSpinRow *row_key_rt_release;
    AdwSpinRow *row_key_deadzone;
    AdwSwitchRow *sw_key_rt;
    AdwPreferencesGroup *key_group;
    AdwSwitchRow *sw_snap;
    AdwComboRow *row_snap_key;
    AdwComboRow *row_snap_mode;
    AdwSwitchRow *sw_snap_full;
    AdwPreferencesGroup *snap_group;
    guint32 key_signature;
    int key_selected;        /* index into key_positions */
    int key_positions[AS_KB_KEY_MAX];
    int key_count;
    int partner_positions[AS_KB_KEY_MAX];
    int partner_count;
    int partner_for;         /* selected position the partner list was built for */
} AsKeyboardPage;

/* ---------------------------- write helpers ---------------------------- */

static void apply_rate(AdwComboRow *row, GParamSpec *pspec, gpointer data)
{
    AsKeyboardPage *p = data;
    int hz = as_ui_rate_from_index(adw_combo_row_get_selected(row));
    AsOpRequest req = { .op = AS_OP_KB_RATE, .value = hz };

    (void)pspec;
    if (!p->page.updating && hz >= 0) {
        as_store_apply(p->page.store, p->page.path, &req);
    }
}

static void apply_debounce(AdwSpinRow *row, GParamSpec *pspec, gpointer data)
{
    AsKeyboardPage *p = data;
    AsOpRequest req = {
        .op = AS_OP_KB_DEBOUNCE,
        .value = (int)adw_spin_row_get_value(row),
    };

    (void)pspec;
    if (!p->page.updating) {
        as_page_queue(&p->page, &req);
    }
}

static void apply_mag_mode(AdwComboRow *row, GParamSpec *pspec, gpointer data)
{
    AsKeyboardPage *p = data;
    guint selected = adw_combo_row_get_selected(row);
    AsOpRequest req = { .op = AS_OP_KB_MAG_MODE, .value = (int)selected };

    (void)pspec;
    if (!p->page.updating && selected < (guint)as_ui_mag_mode_count()) {
        as_store_apply(p->page.store, p->page.path, &req);
    }
}

static void apply_travel(AsKeyboardPage *p)
{
    AsOpRequest req = {
        .op = AS_OP_KB_TRAVEL,
        .rapid_trigger = adw_switch_row_get_active(p->sw_rapid_trigger),
        .actuation = adw_spin_row_get_value(p->row_actuation),
        .release = adw_spin_row_get_value(p->row_release),
        .rt_press = adw_spin_row_get_value(p->row_rt_press),
        .rt_release = adw_spin_row_get_value(p->row_rt_release),
        .deadzone = adw_spin_row_get_value(p->row_deadzone),
    };

    if (!p->page.updating) {
        as_page_queue(&p->page, &req);
    }
}

static void on_travel_control(GtkWidget *widget, GParamSpec *pspec,
                              gpointer data)
{
    (void)widget;
    (void)pspec;
    apply_travel(data);
}

/* --------------------------- one key at a time -------------------------- */

static int selected_key(const AsKeyboardPage *p)
{
    if (p->key_selected < 0 || p->key_selected >= p->key_count) {
        return -1;
    }
    return p->key_positions[p->key_selected];
}

static int selected_partner(const AsKeyboardPage *p)
{
    guint selected = adw_combo_row_get_selected(p->row_snap_key);

    if (selected >= (guint)p->partner_count) {
        return -1;
    }
    return p->partner_positions[selected];
}

/* The R85 HE matrix is 15 columns by 6 rows. Walking it row by row makes the
 * dropdown read like the keyboard instead of like flash addresses. */
static int key_rank(int pos)
{
    if (pos < 90) {
        return (pos % 6) * 1000 + pos / 6;
    }
    return 6000 + pos;
}

static gint key_rank_cmp(gconstpointer a, gconstpointer b)
{
    return key_rank(*(const int *)a) - key_rank(*(const int *)b);
}

static guint32 key_signature(const AsDevice *dev)
{
    guint32 h = dev->key_code_valid ? 1u : 2u;

    for (int i = 0; i < AS_KB_KEY_MAX; i++) {
        h = h * 31u + dev->key_code[i];
    }
    return h;
}

static void key_label(const AsDevice *dev, int pos, char *buf, size_t len)
{
    char name[32];

    if (dev->key_code_valid) {
        as_kb_key_name(dev->key_code[pos], name, sizeof(name));
        if (name[0]) {
            snprintf(buf, len, "%s", name);
            return;
        }
    }
    snprintf(buf, len, "Поз. %d", pos + 1);
}

static GtkStringList *key_model(const AsDevice *dev, const int *positions,
                                int count)
{
    GtkStringList *list = gtk_string_list_new(NULL);

    for (int i = 0; i < count; i++) {
        char buf[64];

        key_label(dev, positions[i], buf, sizeof(buf));
        gtk_string_list_append(list, buf);
    }
    return list;
}

/* Rebuilds the two combos when the matrix changes, and the partner combo when
 * another key is selected. Both are guarded by update_page(), so replacing a
 * model never writes to the device. */
static void rebuild_key_lists(AsKeyboardPage *p, AsDevice *dev)
{
    guint32 sig = key_signature(dev);
    int previous = selected_key(p);
    int selected;

    if (sig != p->key_signature) {
        int count = 0;

        p->key_signature = sig;
        for (int i = 0; i < AS_KB_KEY_MAX; i++) {
            if (dev->key_code_valid && dev->key_code[i] == 0) {
                continue;
            }
            p->key_positions[count++] = i;
        }
        qsort(p->key_positions, (size_t)count, sizeof(p->key_positions[0]),
              key_rank_cmp);
        p->key_count = count;
        adw_combo_row_set_model(p->row_key_select,
                                G_LIST_MODEL(key_model(dev, p->key_positions,
                                                       count)));
        /* Keep pointing at the same physical key when the matrix changes. */
        p->key_selected = count > 0 ? 0 : -1;
        for (int i = 0; i < count; i++) {
            if (p->key_positions[i] == previous) {
                p->key_selected = i;
            }
        }
        p->partner_for = -1;
    }
    selected = selected_key(p);
    if (selected != p->partner_for) {
        int count = 0;

        p->partner_for = selected;
        for (int i = 0; i < p->key_count; i++) {
            if (p->key_positions[i] != selected) {
                p->partner_positions[count++] = p->key_positions[i];
            }
        }
        p->partner_count = count;
        adw_combo_row_set_model(p->row_snap_key,
                                G_LIST_MODEL(key_model(dev,
                                                       p->partner_positions,
                                                       count)));
    }
}

static void apply_key_travel(AsKeyboardPage *p)
{
    AsOpRequest req = {
        .op = AS_OP_KB_KEY_TRAVEL,
        .index = selected_key(p),
        .rapid_trigger = adw_switch_row_get_active(p->sw_key_rt),
        .actuation = adw_spin_row_get_value(p->row_key_actuation),
        .release = adw_spin_row_get_value(p->row_key_release),
        .rt_press = adw_spin_row_get_value(p->row_key_rt_press),
        .rt_release = adw_spin_row_get_value(p->row_key_rt_release),
        .deadzone = adw_spin_row_get_value(p->row_key_deadzone),
    };

    if (!p->page.updating && req.index >= 0) {
        as_page_queue(&p->page, &req);
    }
}

static void apply_snap(AsKeyboardPage *p, int disable)
{
    AsOpRequest req = {
        .op = AS_OP_KB_SNAP,
        .index = selected_key(p),
        .snap_disable = disable,
    };

    if (p->page.updating || req.index < 0) {
        return;
    }
    if (!disable) {
        req.value = selected_partner(p);
        if (req.value < 0) {
            return;
        }
        req.snap_mode = (int)adw_combo_row_get_selected(p->row_snap_mode);
        req.snap_full_bottom = adw_switch_row_get_active(p->sw_snap_full);
    }
    as_store_apply(p->page.store, p->page.path, &req);
}

static void apply_zone_power(AdwSwitchRow *row, GParamSpec *pspec,
                              gpointer data)
{
    AsKeyboardPage *p = data;
    AsDevice *dev;
    int side = row == p->sw_side_light;
    AsOpRequest req = { .op = side ? AS_OP_KB_SIDE_LIGHT : AS_OP_KB_LIGHT_POWER };

    (void)pspec;
    if (p->page.updating) {
        return;
    }
    dev = as_page_device(&p->page);
    if (!dev) {
        return;
    }
    if (side) {
        req.light_brightness = dev->side_light_brightness;
        req.light_speed = dev->side_light_speed;
        req.light_dazzle = dev->side_light_dazzle;
        memcpy(req.rgb, dev->side_rgb, sizeof(req.rgb));
        req.light_mode = adw_switch_row_get_active(row)
                             ? (dev->side_light_mode_saved > 0
                                    ? dev->side_light_mode_saved
                                    : dev->side_light_mode > 0
                                      ? dev->side_light_mode
                                      : 1)
                             : 0;
    } else {
        req.light_on = !adw_switch_row_get_active(row);
    }
    as_store_apply(p->page.store, p->page.path, &req);
}

static void apply_light(AsKeyboardPage *p, int side)
{
    const GdkRGBA *rgba;
    AdwComboRow *mode_row = side ? p->row_side_mode : p->row_light_mode;
    AdwSpinRow *bright_row = side ? p->row_side_bright : p->row_bright;
    AdwSpinRow *speed_row = side ? p->row_side_speed : p->row_speed;
    AdwSwitchRow *dazzle_row = side ? p->sw_side_dazzle : p->sw_dazzle;
    GtkColorDialogButton *color = side ? p->side_color : p->color;
    int *modes = side ? p->side_modes : p->light_modes;
    int mode_count = side ? p->side_mode_count : p->light_mode_count;
    guint selected = adw_combo_row_get_selected(mode_row);
    AsOpRequest req = {
        .op = side ? AS_OP_KB_SIDE_LIGHT : AS_OP_KB_LIGHT,
        .light_brightness = (int)adw_spin_row_get_value(bright_row),
        .light_speed = (int)adw_spin_row_get_value(speed_row),
        .light_dazzle = adw_switch_row_get_active(dazzle_row),
    };

    if (p->page.updating || selected >= (guint)mode_count) {
        return;
    }
    req.light_mode = modes[selected];
    rgba = gtk_color_dialog_button_get_rgba(color);
    if (rgba) {
        as_ui_rgb_from_rgba(rgba, req.rgb);
    }
    as_page_queue(&p->page, &req);
}

static void on_light_control(GtkWidget *widget, GParamSpec *pspec,
                             gpointer data)
{
    (void)widget;
    (void)pspec;
    apply_light(data, 0);
}

static void on_side_light_control(GtkWidget *widget, GParamSpec *pspec,
                                  gpointer data)
{
    (void)widget;
    (void)pspec;
    apply_light(data, 1);
}

/* ------------------------------ refreshing ----------------------------- */

static void update_page(AsKeyboardPage *p)
{
    AsDevice *dev = as_page_device(&p->page);
    gboolean side_on = FALSE;
    gboolean light_on = FALSE;

    if (!dev) {
        return;
    }
    p->page.updating = TRUE;
    as_page_banner(&p->page, dev->error[0] ? as_ui_error(dev) : NULL);
    if (dev->usb_version_valid) {
        as_ui_row_format(p->row_firmware, "%u (0x%04x)", dev->usb_version,
                         dev->usb_version);
    } else {
        as_ui_row_set_value(p->row_firmware, "—");
    }
    if (dev->revision_valid) {
        as_ui_row_format(p->row_revision, "%d", dev->revision);
    } else {
        as_ui_row_set_value(p->row_revision, "—");
    }
    if (dev->profile >= 0) {
        as_ui_row_format(p->row_profile, "%d", dev->profile + 1);
    }
    if (dev->debounce >= 0) {
        as_ui_spin_set(p->row_debounce_spin, &p->page.updating, dev->debounce);
    }
    if (dev->mag_mode >= 0) {
        as_ui_combo_set(p->row_mag_combo, &p->page.updating, dev->mag_mode);
    }
    if (dev->actuation > 0.0) {
        as_ui_spin_set(p->row_actuation, &p->page.updating, dev->actuation);
        as_ui_spin_set(p->row_release, &p->page.updating, dev->release);
        as_ui_spin_set(p->row_rt_press, &p->page.updating, dev->rt_press);
        as_ui_spin_set(p->row_rt_release, &p->page.updating, dev->rt_release);
        as_ui_spin_set(p->row_deadzone, &p->page.updating, dev->deadzone);
        as_ui_switch_set(p->sw_rapid_trigger, &p->page.updating,
                         dev->rapid_trigger);
    }
    as_ui_combo_set(p->row_rate_combo, &p->page.updating,
                    as_ui_rate_to_index(dev->rate));
    gtk_widget_set_visible(GTK_WIDGET(p->light_group), dev->light_valid);
    if (dev->light_valid) {
        light_on = dev->light_on <= 0;
        as_ui_switch_set(p->sw_light, &p->page.updating, light_on);
        as_ui_combo_set(p->row_light_mode, &p->page.updating,
                        as_ui_light_index(p->light_modes, p->light_mode_count,
                                          dev->light_mode));
        as_ui_spin_set(p->row_bright, &p->page.updating, dev->light_brightness);
        as_ui_spin_set(p->row_speed, &p->page.updating, dev->light_speed);
        as_ui_switch_set(p->sw_dazzle, &p->page.updating, dev->light_dazzle);
        as_ui_color_set(p->color, &p->page.updating, dev->light_rgb);
        gtk_widget_set_sensitive(GTK_WIDGET(p->row_light_mode), light_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->row_bright), light_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->row_speed), light_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->sw_dazzle), light_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->color), light_on);
    }
    gtk_widget_set_visible(GTK_WIDGET(p->side_light_group), dev->side_light_valid);
    if (dev->side_light_valid) {
        side_on = dev->side_light_mode != 0;
        as_ui_switch_set(p->sw_side_light, &p->page.updating, side_on);
        as_ui_combo_set(p->row_side_mode, &p->page.updating,
                        as_ui_light_index(p->side_modes, p->side_mode_count,
                                          dev->side_light_mode));
        as_ui_spin_set(p->row_side_bright, &p->page.updating,
                       dev->side_light_brightness);
        as_ui_spin_set(p->row_side_speed, &p->page.updating, dev->side_light_speed);
        as_ui_switch_set(p->sw_side_dazzle, &p->page.updating,
                         dev->side_light_dazzle);
        as_ui_color_set(p->side_color, &p->page.updating, dev->side_rgb);
        gtk_widget_set_sensitive(GTK_WIDGET(p->row_side_mode), side_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->row_side_bright), side_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->row_side_speed), side_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->sw_side_light), light_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->row_side_mode), light_on && side_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->row_side_bright), light_on && side_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->row_side_speed), light_on && side_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->sw_side_dazzle), light_on && side_on);
        gtk_widget_set_sensitive(GTK_WIDGET(p->side_color), light_on && side_on);
    }
    gtk_widget_set_visible(GTK_WIDGET(p->key_group), dev->mag_mode >= 0);
    gtk_widget_set_visible(GTK_WIDGET(p->snap_group), dev->mag_mode >= 0);
    if (dev->mag_mode >= 0) {
        int key, mode, partner = -1, partner_index = -1;

        rebuild_key_lists(p, dev);
        if (p->key_selected < 0 && p->key_count > 0) {
            p->key_selected = 0;
        }
        key = selected_key(p);
        as_ui_combo_set(p->row_key_select, &p->page.updating, p->key_selected);
        if (key >= 0) {
            as_ui_spin_set(p->row_key_actuation, &p->page.updating,
                           dev->key_actuation[key]);
            as_ui_spin_set(p->row_key_release, &p->page.updating,
                           dev->key_release[key]);
            as_ui_spin_set(p->row_key_rt_press, &p->page.updating,
                           dev->key_rt_press[key]);
            as_ui_spin_set(p->row_key_rt_release, &p->page.updating,
                           dev->key_rt_release[key]);
            as_ui_spin_set(p->row_key_deadzone, &p->page.updating,
                           dev->key_deadzone[key]);
            as_ui_switch_set(p->sw_key_rt, &p->page.updating,
                             dev->key_option[key] & AS_KB_OPTION_RT);
        }
        mode = key >= 0 ? as_kb_snap_mode_of(dev->key_option, dev->key_pair,
                                             key)
                        : -1;
        if (mode >= 0) {
            partner = dev->key_pair[key];
        }
        for (int i = 0; i < p->partner_count; i++) {
            if (p->partner_positions[i] == partner) {
                partner_index = i;
            }
        }
        /* Offer the first free key as the partner, so Snap Tap can be turned
         * on without picking both keys first. */
        if (partner_index < 0 && p->partner_count > 0) {
            partner_index = 0;
        }
        as_ui_switch_set(p->sw_snap, &p->page.updating, mode >= 0);
        as_ui_combo_set(p->row_snap_key, &p->page.updating, partner_index);
        as_ui_combo_set(p->row_snap_mode, &p->page.updating,
                        mode >= 0 ? mode : AS_KB_SNAP_LAST);
        as_ui_switch_set(p->sw_snap_full, &p->page.updating,
                         key >= 0 && (dev->key_option[key] &
                                      AS_KB_OPTION_FULL_BOTTOM));
    }
    p->page.updating = FALSE;
}

static void on_store_changed(AsStore *store, gpointer box)
{
    (void)store;
    update_page(g_object_get_data(G_OBJECT(box), "as-page"));
}

/* Selecting another key only reloads the controls; nothing is written until
 * one of them changes. */
static void on_key_selected(AdwComboRow *row, GParamSpec *pspec, gpointer data)
{
    AsKeyboardPage *p = data;

    (void)pspec;
    if (p->page.updating) {
        return;
    }
    p->key_selected = (int)adw_combo_row_get_selected(row);
    update_page(p);
}

static void on_key_travel(GtkWidget *widget, GParamSpec *pspec, gpointer data)
{
    (void)widget;
    (void)pspec;
    apply_key_travel(data);
}

static void on_snap_toggle(AdwSwitchRow *row, GParamSpec *pspec, gpointer data)
{
    AsKeyboardPage *p = data;

    (void)pspec;
    apply_snap(p, !adw_switch_row_get_active(row));
}

static void on_snap_control(GtkWidget *widget, GParamSpec *pspec, gpointer data)
{
    AsKeyboardPage *p = data;

    (void)widget;
    (void)pspec;
    if (adw_switch_row_get_active(p->sw_snap)) {
        apply_snap(p, 0);
    }
}

/* ------------------------------ construction --------------------------- */

static AdwPreferencesGroup *build_status(AsKeyboardPage *p)
{
    AdwPreferencesGroup *group = as_ui_group(
        "Состояние", "Читается из самого устройства при каждом обновлении");

    p->row_firmware = as_ui_value_row("Прошивка");
    p->row_revision = as_ui_value_row("Ревизия");
    p->row_profile = as_ui_value_row("Профиль");

    adw_preferences_group_add(group, GTK_WIDGET(p->row_firmware));
    adw_preferences_group_add(group, GTK_WIDGET(p->row_revision));
    adw_preferences_group_add(group, GTK_WIDGET(p->row_profile));
    return group;
}

static AdwPreferencesGroup *build_settings(AsKeyboardPage *p)
{
    AdwPreferencesGroup *group = as_ui_group("Настройки", NULL);
    GtkStringList *mag = gtk_string_list_new(NULL);

    for (int i = 0; i < as_ui_mag_mode_count(); i++) {
        gtk_string_list_append(mag, as_ui_mag_mode_name(i));
    }

    p->row_rate_combo = as_ui_combo("Частота опроса",
                                    G_LIST_MODEL(as_ui_rate_list()));
    g_signal_connect(p->row_rate_combo, "notify::selected",
                     G_CALLBACK(apply_rate), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_rate_combo));

    p->row_debounce_spin = as_ui_spin("Debounce", 0, 20, 1, "мс");
    g_signal_connect(p->row_debounce_spin, "notify::value",
                     G_CALLBACK(apply_debounce), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_debounce_spin));

    p->row_mag_combo = as_ui_combo("Режим магнитов", G_LIST_MODEL(mag));
    g_signal_connect(p->row_mag_combo, "notify::selected",
                     G_CALLBACK(apply_mag_mode), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_mag_combo));
    return group;
}

static AdwPreferencesGroup *build_travel(AsKeyboardPage *p)
{
    AdwPreferencesGroup *group = as_ui_group(
        "Срабатывание клавиш", "Настройки применяются ко всем клавишам.");
    AdwSpinRow **rows[] = { &p->row_actuation, &p->row_release,
                            &p->row_rt_press, &p->row_rt_release,
                            &p->row_deadzone };
    const char *titles[] = { "Точка срабатывания", "Точка отпускания",
                            "Rapid Trigger: нажатие", "Rapid Trigger: отпускание",
                            "Мёртвая зона" };

    for (size_t i = 0; i < G_N_ELEMENTS(rows); i++) {
        *rows[i] = as_ui_spin(titles[i], i == 4 ? 0.0 : 0.1,
                              i == 4 ? 1.0 : 4.0, 0.1, "мм");
        adw_spin_row_set_digits(*rows[i], 2);
        adw_spin_row_set_value(*rows[i], i == 4 ? 0.0 : i < 2 ? 1.5 : 0.2);
        g_signal_connect(*rows[i], "notify::value",
                         G_CALLBACK(on_travel_control), p);
        adw_preferences_group_add(group, GTK_WIDGET(*rows[i]));
    }
    p->sw_rapid_trigger = as_ui_switch_row("Rapid Trigger", NULL);
    g_signal_connect(p->sw_rapid_trigger, "notify::active",
                     G_CALLBACK(on_travel_control), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_rapid_trigger));
    return group;
}

static AdwPreferencesGroup *build_key_travel(AsKeyboardPage *p)
{
    AdwPreferencesGroup *group = as_ui_group(
        "Отдельная клавиша",
        "Сначала выберите клавишу, затем задайте её ход. Значения остальных "
        "клавиш не меняются.");
    AdwSpinRow **rows[] = { &p->row_key_actuation, &p->row_key_release,
                            &p->row_key_rt_press, &p->row_key_rt_release,
                            &p->row_key_deadzone };
    const char *titles[] = { "Точка срабатывания", "Точка отпускания",
                            "Rapid Trigger: нажатие", "Rapid Trigger: отпускание",
                            "Мёртвая зона" };

    p->key_group = group;
    p->key_selected = -1;
    p->partner_for = -2;
    {
        GtkStringList *empty = gtk_string_list_new(NULL);

        p->row_key_select = as_ui_combo("Клавиша", G_LIST_MODEL(empty));
        g_object_unref(empty);
    }
    g_signal_connect(p->row_key_select, "notify::selected",
                     G_CALLBACK(on_key_selected), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_key_select));

    for (size_t i = 0; i < G_N_ELEMENTS(rows); i++) {
        *rows[i] = as_ui_spin(titles[i], i == 4 ? 0.0 : 0.1,
                              i == 4 ? 1.0 : 4.0, 0.1, "мм");
        adw_spin_row_set_digits(*rows[i], 2);
        g_signal_connect(*rows[i], "notify::value",
                         G_CALLBACK(on_key_travel), p);
        adw_preferences_group_add(group, GTK_WIDGET(*rows[i]));
    }
    p->sw_key_rt = as_ui_switch_row("Rapid Trigger", NULL);
    g_signal_connect(p->sw_key_rt, "notify::active",
                     G_CALLBACK(on_key_travel), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_key_rt));
    return group;
}

static AdwPreferencesGroup *build_snap(AsKeyboardPage *p)
{
    AdwPreferencesGroup *group = as_ui_group(
        "Snap Tap",
        "Две клавиши работают как одна ось: при удержании обеих отпускание "
        "одной не прерывает движение второй.");
    GtkStringList *modes = gtk_string_list_new(NULL);

    gtk_string_list_append(modes, "Последнее нажатие");
    gtk_string_list_append(modes, "Нейтрально (обе отпущены)");
    gtk_string_list_append(modes, "Первая клавиша в приоритете");
    gtk_string_list_append(modes, "Вторая клавиша в приоритете");

    p->snap_group = group;
    p->sw_snap = as_ui_switch_row(
        "Snap Tap включён", "Пара привязывается к выбранной выше клавише.");
    g_signal_connect(p->sw_snap, "notify::active",
                     G_CALLBACK(on_snap_toggle), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_snap));

    p->row_snap_key = as_ui_combo("Вторая клавиша", NULL);
    {
        GtkStringList *empty = gtk_string_list_new(NULL);

        adw_combo_row_set_model(p->row_snap_key, G_LIST_MODEL(empty));
        g_object_unref(empty);
    }
    g_signal_connect(p->row_snap_key, "notify::selected",
                     G_CALLBACK(on_snap_control), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_snap_key));

    p->row_snap_mode = as_ui_combo("Приоритет", G_LIST_MODEL(modes));
    g_signal_connect(p->row_snap_mode, "notify::selected",
                     G_CALLBACK(on_snap_control), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_snap_mode));

    p->sw_snap_full = as_ui_switch_row(
        "Полное нажатие внизу", "Действие срабатывает только на полном ходе.");
    g_signal_connect(p->sw_snap_full, "notify::active",
                     G_CALLBACK(on_snap_control), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_snap_full));
    return group;
}

static GtkColorDialogButton *build_color(AsKeyboardPage *p, int side)
{
    AdwActionRow *row = as_ui_value_row("Цвет");
    GtkColorDialog *dialog = gtk_color_dialog_new();
    GdkRGBA rgba = { 1.0, 1.0, 1.0, 1.0 };
    GtkColorDialogButton *button;

    gtk_color_dialog_set_with_alpha(dialog, FALSE);
    button = GTK_COLOR_DIALOG_BUTTON(gtk_color_dialog_button_new(dialog));
    gtk_color_dialog_button_set_rgba(button, &rgba);
    g_signal_connect(button, "notify::rgba",
                     G_CALLBACK(side ? on_side_light_control : on_light_control), p);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), GTK_WIDGET(button));
    adw_preferences_group_add(side ? p->side_light_group : p->light_group,
                              GTK_WIDGET(row));
    return button;
}

static AdwPreferencesGroup *build_light(AsKeyboardPage *p)
{
    AdwPreferencesGroup *group = as_ui_group(
        "Подсветка клавиш", "Настройки подсветки клавиш.");
    GtkStringList *modes = as_ui_light_list_for(
        0, p->light_modes, (int)G_N_ELEMENTS(p->light_modes));

    p->light_group = group;
    p->light_mode_count = (int)g_list_model_get_n_items(G_LIST_MODEL(modes));
    p->sw_light = as_ui_switch_row("Подсветка включена", NULL);
    g_signal_connect(p->sw_light, "notify::active",
                     G_CALLBACK(apply_zone_power), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_light));
    p->row_light_mode = as_ui_combo("Режим", G_LIST_MODEL(modes));
    g_signal_connect(p->row_light_mode, "notify::selected",
                     G_CALLBACK(on_light_control), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_light_mode));
    p->row_bright = as_ui_spin("Яркость", 0, 4, 1, NULL);
    g_signal_connect(p->row_bright, "notify::value",
                     G_CALLBACK(on_light_control), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_bright));
    p->row_speed = as_ui_spin("Скорость", 0, 4, 1, NULL);
    g_signal_connect(p->row_speed, "notify::value",
                     G_CALLBACK(on_light_control), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_speed));
    p->color = build_color(p, 0);
    p->sw_dazzle = as_ui_switch_row("Dazzle", NULL);
    g_signal_connect(p->sw_dazzle, "notify::active",
                     G_CALLBACK(on_light_control), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_dazzle));
    gtk_widget_set_visible(GTK_WIDGET(group), FALSE);
    return group;
}

GtkWidget *as_keyboard_page_new(AsStore *store, const char *path)
{
    AsKeyboardPage *p = g_new0(AsKeyboardPage, 1);
    AdwPreferencesPage *prefs = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkStringList *modes;

    as_page_attach(&p->page, box, prefs, store, path);
    adw_preferences_page_add(prefs, build_status(p));
    adw_preferences_page_add(prefs, build_settings(p));
    adw_preferences_page_add(prefs, build_travel(p));
    adw_preferences_page_add(prefs, build_key_travel(p));
    adw_preferences_page_add(prefs, build_snap(p));
    adw_preferences_page_add(prefs, build_light(p));

    p->side_light_group = as_ui_group(
        "Боковая подсветка", "Настройки подсветки боковой панели.");
    modes = as_ui_light_list_for(1, p->side_modes,
                                 (int)G_N_ELEMENTS(p->side_modes));
    p->side_mode_count = (int)g_list_model_get_n_items(G_LIST_MODEL(modes));
    p->sw_side_light = as_ui_switch_row("Подсветка включена", NULL);
    g_signal_connect(p->sw_side_light, "notify::active",
                     G_CALLBACK(apply_zone_power), p);
    adw_preferences_group_add(p->side_light_group, GTK_WIDGET(p->sw_side_light));
    p->row_side_mode = as_ui_combo("Режим", G_LIST_MODEL(modes));
    g_signal_connect(p->row_side_mode, "notify::selected",
                     G_CALLBACK(on_side_light_control), p);
    adw_preferences_group_add(p->side_light_group, GTK_WIDGET(p->row_side_mode));
    p->row_side_bright = as_ui_spin("Яркость", 0, 4, 1, NULL);
    g_signal_connect(p->row_side_bright, "notify::value",
                     G_CALLBACK(on_side_light_control), p);
    adw_preferences_group_add(p->side_light_group, GTK_WIDGET(p->row_side_bright));
    p->row_side_speed = as_ui_spin("Скорость", 0, 4, 1, NULL);
    g_signal_connect(p->row_side_speed, "notify::value",
                     G_CALLBACK(on_side_light_control), p);
    adw_preferences_group_add(p->side_light_group, GTK_WIDGET(p->row_side_speed));
    p->side_color = build_color(p, 1);
    p->sw_side_dazzle = as_ui_switch_row("Dazzle", NULL);
    g_signal_connect(p->sw_side_dazzle, "notify::active",
                     G_CALLBACK(on_side_light_control), p);
    adw_preferences_group_add(p->side_light_group, GTK_WIDGET(p->sw_side_dazzle));
    gtk_widget_set_visible(GTK_WIDGET(p->side_light_group), FALSE);
    adw_preferences_page_add(prefs, p->side_light_group);

    g_signal_connect_object(store, "changed", G_CALLBACK(on_store_changed),
                            box, 0);
    update_page(p);
    return box;
}
