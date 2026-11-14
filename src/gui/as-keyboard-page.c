// SPDX-License-Identifier: MIT
#include "as-keyboard-page.h"

#include "as-ui.h"

typedef struct {
    AsStore *store;
    char *path;
    GtkWidget *root;
    AdwBanner *banner;
    gboolean updating;

    /* status */
    AdwActionRow *row_firmware;
    AdwActionRow *row_revision;
    AdwActionRow *row_profile;

    /* settings */
    AdwComboRow *row_rate_combo;
    AdwSpinRow *row_debounce_spin;
    AdwComboRow *row_mag_combo;
    AdwPreferencesGroup *settings_group;

    /* light */
    AdwSwitchRow *sw_light;
    AdwComboRow *row_light_mode;
    AdwSpinRow *row_bright;
    AdwSpinRow *row_speed;
    AdwSwitchRow *sw_dazzle;
    GtkColorDialogButton *color;
    AdwPreferencesGroup *light_group;

    /* The keyboard drops commands that arrive too fast, so writes are
     * coalesced before they reach the device. */
    guint pending_id;
    AsOpRequest pending;
    gboolean pending_valid;
} AsKeyboardPage;

static AsDevice *page_device(AsKeyboardPage *p)
{
    return as_store_lookup(p->store, p->path);
}

/* ---------------------------- write helpers ---------------------------- */

static void flush_pending(AsKeyboardPage *p)
{
    if (p->pending_id) {
        g_source_remove(p->pending_id);
        p->pending_id = 0;
    }
    if (p->pending_valid) {
        p->pending_valid = FALSE;
        as_store_apply(p->store, p->path, &p->pending);
    }
}

static gboolean on_pending_timeout(gpointer data)
{
    AsKeyboardPage *p = data;

    p->pending_id = 0;
    flush_pending(p);
    return G_SOURCE_REMOVE;
}

static void queue_write(AsKeyboardPage *p, const AsOpRequest *req)
{
    p->pending = *req;
    p->pending_valid = TRUE;
    if (p->pending_id) {
        g_source_remove(p->pending_id);
    }
    p->pending_id = g_timeout_add(350, on_pending_timeout, p);
}

static void apply_rate(AdwComboRow *row, GParamSpec *pspec, gpointer user_data)
{
    AsKeyboardPage *p = user_data;
    AsOpRequest req = { 0 };
    int hz;

    (void)pspec;
    if (p->updating) {
        return;
    }
    hz = as_ui_rate_from_index(adw_combo_row_get_selected(row));
    if (hz < 0) {
        return;
    }
    req.op = AS_OP_KB_RATE;
    req.value = hz;
    as_store_apply(p->store, p->path, &req);
}

static void apply_debounce(AdwSpinRow *row, GParamSpec *pspec, gpointer user_data)
{
    AsKeyboardPage *p = user_data;
    AsOpRequest req = { 0 };

    (void)pspec;
    if (p->updating) {
        return;
    }
    req.op = AS_OP_KB_DEBOUNCE;
    req.value = (int)adw_spin_row_get_value(row);
    queue_write(p, &req);
}

static void apply_mag_mode(AdwComboRow *row, GParamSpec *pspec, gpointer user_data)
{
    AsKeyboardPage *p = user_data;
    AsOpRequest req = { 0 };
    guint selected;

    (void)pspec;
    if (p->updating) {
        return;
    }
    selected = adw_combo_row_get_selected(row);
    if (selected >= (guint)as_ui_mag_mode_count()) {
        return;
    }
    req.op = AS_OP_KB_MAG_MODE;
    req.value = (int)selected;
    as_store_apply(p->store, p->path, &req);
}

static void apply_light_power(AdwSwitchRow *row, GParamSpec *pspec,
                              gpointer user_data)
{
    AsKeyboardPage *p = user_data;
    AsOpRequest req = { 0 };

    (void)pspec;
    if (p->updating) {
        return;
    }
    req.op = AS_OP_KB_LIGHT_POWER;
    req.value = adw_switch_row_get_active(row) ? 1 : 0;
    /* The master switch is not debounced: one switch, one write. */
    as_store_apply(p->store, p->path, &req);
}

static void apply_light(AsKeyboardPage *p)
{
    AsDevice *dev;
    AsOpRequest req = { 0 };
    const GdkRGBA *rgba;

    if (p->updating) {
        return;
    }
    dev = page_device(p);
    if (!dev) {
        return;
    }
    req.op = AS_OP_KB_LIGHT;
    req.light_mode = (int)adw_combo_row_get_selected(p->row_light_mode);
    req.light_brightness = (int)adw_spin_row_get_value(p->row_bright);
    req.light_speed = (int)adw_spin_row_get_value(p->row_speed);
    req.light_dazzle = adw_switch_row_get_active(p->sw_dazzle);
    req.light_option = dev->light_opt;
    rgba = gtk_color_dialog_button_get_rgba(p->color);
    if (rgba) {
        as_ui_rgb_from_rgba(rgba, req.rgb);
    }
    queue_write(p, &req);
}

static void on_light_mode(AdwComboRow *row, GParamSpec *pspec, gpointer user_data)
{
    (void)row;
    (void)pspec;
    apply_light(user_data);
}

static void on_light_spin(AdwSpinRow *row, GParamSpec *pspec, gpointer user_data)
{
    (void)row;
    (void)pspec;
    apply_light(user_data);
}

static void on_light_switch(AdwSwitchRow *row, GParamSpec *pspec,
                            gpointer user_data)
{
    (void)row;
    (void)pspec;
    apply_light(user_data);
}

static void on_light_color(GtkColorDialogButton *button, GParamSpec *pspec,
                           gpointer user_data)
{
    (void)button;
    (void)pspec;
    apply_light(user_data);
}

/* ------------------------------ refreshing ----------------------------- */

static void update_page(AsKeyboardPage *p)
{
    AsDevice *dev = page_device(p);
    char buf[160];

    if (!dev) {
        return;
    }
    p->updating = TRUE;

    adw_banner_set_revealed(p->banner, dev->error[0] != 0);
    adw_banner_set_title(p->banner, dev->error);

    if (dev->usb_version_valid) {
        snprintf(buf, sizeof(buf), "%u (0x%04x)", dev->usb_version,
                 dev->usb_version);
        adw_action_row_set_subtitle(p->row_firmware, buf);
    } else {
        adw_action_row_set_subtitle(p->row_firmware, "—");
    }
    if (dev->revision_valid) {
        snprintf(buf, sizeof(buf), "%d", dev->revision);
        adw_action_row_set_subtitle(p->row_revision, buf);
    } else {
        adw_action_row_set_subtitle(p->row_revision, "—");
    }
    if (dev->profile >= 0) {
        snprintf(buf, sizeof(buf), "%d", dev->profile + 1);
        adw_action_row_set_subtitle(p->row_profile, buf);
    }
    if (dev->debounce >= 0) {
        as_ui_spin_set(p->row_debounce_spin, &p->updating, dev->debounce);
    }
    if (dev->mag_mode >= 0) {
        as_ui_combo_set(p->row_mag_combo, &p->updating, dev->mag_mode);
    }

    as_ui_combo_set(p->row_rate_combo, &p->updating,
                    as_ui_rate_to_index(dev->rate));
    as_ui_switch_set(p->sw_light, &p->updating, dev->light_on > 0);
    as_ui_combo_set(p->row_light_mode, &p->updating, dev->light_mode);
    as_ui_spin_set(p->row_bright, &p->updating, dev->light_brightness);
    as_ui_spin_set(p->row_speed, &p->updating, dev->light_speed);
    as_ui_switch_set(p->sw_dazzle, &p->updating, dev->light_dazzle);
    as_ui_color_set(p->color, &p->updating, dev->rgb);

    p->updating = FALSE;
}

static void on_store_changed(AsStore *store, gpointer user_data)
{
    (void)store;
    update_page(g_object_get_data(G_OBJECT(user_data), "as-page"));
}

static void page_free(gpointer data)
{
    AsKeyboardPage *p = data;

    flush_pending(p);
    g_free(p->path);
    g_free(p);
}

/* ------------------------------ construction --------------------------- */

static AdwPreferencesGroup *build_status(AsKeyboardPage *p)
{
    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());

    adw_preferences_group_set_title(group, "Состояние");
    adw_preferences_group_set_description(
        group, "Читается из самого устройства при каждом обновлении");

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
    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    GtkStringList *mag = gtk_string_list_new(NULL);

    adw_preferences_group_set_title(group, "Настройки");
    p->settings_group = group;

    for (int i = 0; i < as_ui_mag_mode_count(); i++) {
        gtk_string_list_append(mag, as_ui_mag_mode_name(i));
    }

    p->row_rate_combo = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->row_rate_combo),
                                  "Частота опроса");
    adw_combo_row_set_model(p->row_rate_combo,
                            G_LIST_MODEL(as_ui_rate_list()));
    g_signal_connect(p->row_rate_combo, "notify::selected",
                     G_CALLBACK(apply_rate), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_rate_combo));

    p->row_debounce_spin = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 20, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->row_debounce_spin),
                                  "Debounce");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->row_debounce_spin), "мс");
    adw_spin_row_set_digits(p->row_debounce_spin, 0);
    g_signal_connect(p->row_debounce_spin, "notify::value",
                     G_CALLBACK(apply_debounce), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_debounce_spin));

    p->row_mag_combo = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->row_mag_combo),
                                  "Режим магнитов");
    adw_combo_row_set_model(p->row_mag_combo, G_LIST_MODEL(mag));
    g_signal_connect(p->row_mag_combo, "notify::selected",
                     G_CALLBACK(apply_mag_mode), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_mag_combo));
    return group;
}

static AdwPreferencesGroup *build_light(AsKeyboardPage *p)
{
    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());

    adw_preferences_group_set_title(group, "Подсветка");
    adw_preferences_group_set_description(
        group, "Галочка включает всю подсветку целиком; режим её не выключает");
    p->light_group = group;

    p->sw_light = as_ui_switch_row("Подсветка включена", NULL);
    g_signal_connect(p->sw_light, "notify::active",
                     G_CALLBACK(apply_light_power), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_light));

    p->row_light_mode = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->row_light_mode),
                                  "Режим");
    adw_combo_row_set_model(p->row_light_mode,
                            G_LIST_MODEL(as_ui_light_list()));
    g_signal_connect(p->row_light_mode, "notify::selected",
                     G_CALLBACK(on_light_mode), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_light_mode));

    p->row_bright = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 4, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->row_bright), "Яркость");
    adw_spin_row_set_digits(p->row_bright, 0);
    g_signal_connect(p->row_bright, "notify::value",
                     G_CALLBACK(on_light_spin), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_bright));

    p->row_speed = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 4, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->row_speed),
                                  "Скорость");
    adw_spin_row_set_digits(p->row_speed, 0);
    g_signal_connect(p->row_speed, "notify::value",
                     G_CALLBACK(on_light_spin), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_speed));

    {
        AdwActionRow *row = as_ui_value_row("Цвет");
        GtkColorDialog *dialog = gtk_color_dialog_new();
        GdkRGBA rgba = { 1.0, 1.0, 1.0, 1.0 };

        gtk_color_dialog_set_with_alpha(dialog, FALSE);
        p->color = GTK_COLOR_DIALOG_BUTTON(gtk_color_dialog_button_new(dialog));
        gtk_color_dialog_button_set_rgba(p->color, &rgba);
        g_signal_connect(p->color, "notify::rgba",
                         G_CALLBACK(on_light_color), p);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), GTK_WIDGET(p->color));
        adw_preferences_group_add(group, GTK_WIDGET(row));
    }

    p->sw_dazzle = as_ui_switch_row("Dazzle", NULL);
    g_signal_connect(p->sw_dazzle, "notify::active",
                     G_CALLBACK(on_light_switch), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_dazzle));
    return group;
}

GtkWidget *as_keyboard_page_new(AsStore *store, const char *path)
{
    AsKeyboardPage *p = g_new0(AsKeyboardPage, 1);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    p->store = store;
    p->path = g_strdup(path);
    p->root = box;

    p->banner = ADW_BANNER(adw_banner_new(""));
    adw_banner_set_revealed(p->banner, FALSE);

    adw_preferences_page_add(page, build_status(p));
    adw_preferences_page_add(page, build_settings(p));
    adw_preferences_page_add(page, build_light(p));

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(p->banner));
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(page));
    gtk_widget_set_vexpand(GTK_WIDGET(page), TRUE);

    g_object_set_data(G_OBJECT(box), "as-page", p);
    g_object_set_data_full(G_OBJECT(box), "as-page-data", p, page_free);
    g_signal_connect_object(store, "changed", G_CALLBACK(on_store_changed),
                            box, 0);
    update_page(p);
    return box;
}
