// SPDX-License-Identifier: MIT
#include "as-mouse-page.h"

#include <string.h>

#include "as-ui.h"

typedef struct {
    AsStore *store;
    char *path;
    GtkWidget *root;
    AdwBanner *banner;
    gboolean updating;

    /* status */
    AdwActionRow *row_link;
    AdwActionRow *row_firmware;
    AdwActionRow *row_battery;
    AdwActionRow *row_profile;
    AdwActionRow *row_dpi_now;
    GtkLevelBar *battery_bar;

    /* sensor */
    AdwComboRow *row_rate;
    AdwSpinRow *row_dpi[AS_MAX_DPI_STAGES];
    AdwSpinRow *row_lod;
    AdwSpinRow *row_debounce;
    AdwSwitchRow *sw_motion;
    AdwSwitchRow *sw_angle;
    AdwSwitchRow *sw_ripple;
    AdwPreferencesGroup *sensor_group;

    /* Writes are coalesced: dragging a slider or the colour picker must not
     * send one EEPROM write per pixel. */
    guint pending_id;
    AsOpRequest pending;
    gboolean pending_valid;
} AsMousePage;

static AsDevice *page_device(AsMousePage *p)
{
    return as_store_lookup(p->store, p->path);
}

/* ---------------------------- write helpers ---------------------------- */

static void flush_pending(AsMousePage *p)
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
    AsMousePage *p = data;

    p->pending_id = 0;
    flush_pending(p);
    return G_SOURCE_REMOVE;
}

static void queue_write(AsMousePage *p, const AsOpRequest *req)
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
    AsMousePage *p = user_data;
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
    req.op = AS_OP_MOUSE_RATE;
    req.value = hz;
    as_store_apply(p->store, p->path, &req);
}

static void apply_dpi(AdwSpinRow *row, GParamSpec *pspec, gpointer user_data)
{
    AsMousePage *p = user_data;
    AsOpRequest req = { 0 };
    unsigned value;

    (void)pspec;
    if (p->updating) {
        return;
    }
    value = as_ui_snap_dpi((unsigned)adw_spin_row_get_value(row));
    if (value != (unsigned)adw_spin_row_get_value(row)) {
        as_ui_spin_set(row, &p->updating, value);
    }
    req.op = AS_OP_MOUSE_DPI_STAGE;
    req.index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "as-stage"));
    req.value = (int)value;
    queue_write(p, &req);
}

static void apply_u8(AdwSpinRow *row, GParamSpec *pspec, gpointer user_data)
{
    AsMousePage *p = user_data;
    AsOpRequest req = { 0 };

    (void)pspec;
    if (p->updating) {
        return;
    }
    req.op = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "as-op"));
    req.value = (int)adw_spin_row_get_value(row);
    queue_write(p, &req);
}

static void apply_toggle(AdwSwitchRow *row, GParamSpec *pspec, gpointer user_data)
{
    AsMousePage *p = user_data;
    AsOpRequest req = { 0 };

    (void)pspec;
    if (p->updating) {
        return;
    }
    req.op = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "as-op"));
    req.value = adw_switch_row_get_active(row);
    as_store_apply(p->store, p->path, &req);
}

/* ------------------------------ refreshing ----------------------------- */

static void update_page(AsMousePage *p)
{
    AsDevice *dev = page_device(p);
    char buf[160];

    if (!dev) {
        return;
    }

    p->updating = TRUE;

    if (dev->error[0]) {
        adw_banner_set_title(p->banner, dev->error);
        adw_banner_set_revealed(p->banner, TRUE);
    } else if (!dev->online) {
        adw_banner_set_title(
            p->banner,
            "Мышь спит или выключена. Пошевелите её, чтобы менять настройки.");
        adw_banner_set_revealed(p->banner, TRUE);
    } else {
        adw_banner_set_revealed(p->banner, FALSE);
    }

    adw_action_row_set_subtitle(p->row_link,
                                dev->online ? "есть" : "нет");
    if (dev->fw_valid) {
        char dongle[64] = "";

        if (dev->dongle_valid) {
            snprintf(dongle, sizeof(dongle), " · донгл v%d.%d.%d",
                     dev->dongle[0], dev->dongle[1], dev->dongle[2]);
        }
        snprintf(buf, sizeof(buf), "v%d.%02x%s", dev->fw_major,
                 dev->fw_minor, dongle);
        adw_action_row_set_subtitle(p->row_firmware, buf);
    } else {
        adw_action_row_set_subtitle(p->row_firmware, "—");
    }

    if (dev->battery > 0) {
        char volt[32] = "";

        if (dev->voltage) {
            snprintf(volt, sizeof(volt), " · %d мВ", dev->voltage);
        }
        snprintf(buf, sizeof(buf), "%d%%%s%s", dev->battery,
                 dev->charging ? " · зарядка" : "", volt);
        adw_action_row_set_subtitle(p->row_battery, buf);
        gtk_level_bar_set_value(p->battery_bar, dev->battery / 100.0);
    } else {
        adw_action_row_set_subtitle(p->row_battery, "—");
        gtk_level_bar_set_value(p->battery_bar, 0);
    }

    if (dev->profile >= 0) {
        snprintf(buf, sizeof(buf), "%d", dev->profile + 1);
        adw_action_row_set_subtitle(p->row_profile, buf);
    }

    if (dev->dpi_stages > 0) {
        snprintf(buf, sizeof(buf), "%d из %d", dev->active_stage + 1,
                 dev->dpi_stages);
        adw_action_row_set_subtitle(p->row_dpi_now, buf);
    }

    gtk_widget_set_sensitive(GTK_WIDGET(p->sensor_group), dev->config_valid);

    as_ui_combo_set(p->row_rate, &p->updating,
                    as_ui_rate_to_index(dev->rate));

    for (int i = 0; i < AS_MAX_DPI_STAGES; i++) {
        gboolean active = i < dev->dpi_stages;
        gtk_widget_set_visible(GTK_WIDGET(p->row_dpi[i]), active);
        if (active) {
            as_ui_spin_set(p->row_dpi[i], &p->updating, dev->dpi[i]);
            adw_preferences_row_set_title(
                ADW_PREFERENCES_ROW(p->row_dpi[i]),
                i == dev->active_stage ? "Активный этап DPI"
                                       : "Этап DPI");
        }
    }

    if (dev->lod >= 0) {
        as_ui_spin_set(p->row_lod, &p->updating, dev->lod);
    }
    if (dev->debounce >= 0) {
        as_ui_spin_set(p->row_debounce, &p->updating, dev->debounce);
    }
    if (dev->motion_sync >= 0) {
        as_ui_switch_set(p->sw_motion, &p->updating, dev->motion_sync);
    }
    if (dev->angle_snap >= 0) {
        as_ui_switch_set(p->sw_angle, &p->updating, dev->angle_snap);
    }
    if (dev->ripple >= 0) {
        as_ui_switch_set(p->sw_ripple, &p->updating, dev->ripple);
    }

    p->updating = FALSE;
}

static void on_store_changed(AsStore *store, gpointer user_data)
{
    (void)store;
    update_page(g_object_get_data(G_OBJECT(user_data), "as-page"));
}

static void page_free(gpointer data)
{
    AsMousePage *p = data;

    flush_pending(p);
    g_free(p->path);
    g_free(p);
}

/* ------------------------------ construction --------------------------- */

static AdwPreferencesGroup *build_status(AsMousePage *p)
{
    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());

    adw_preferences_group_set_title(group, "Состояние");
    adw_preferences_group_set_description(
        group, "Читается из самого устройства при каждом обновлении");

    p->row_link = as_ui_value_row("Связь");
    p->row_firmware = as_ui_value_row("Прошивка");
    p->row_battery = as_ui_value_row("Батарея");
    p->row_profile = as_ui_value_row("Профиль");
    p->row_dpi_now = as_ui_value_row("Профиль DPI");

    p->battery_bar = GTK_LEVEL_BAR(gtk_level_bar_new());
    gtk_level_bar_set_min_value(p->battery_bar, 0);
    gtk_level_bar_set_max_value(p->battery_bar, 1);
    gtk_widget_set_size_request(GTK_WIDGET(p->battery_bar), 90, -1);
    gtk_widget_set_valign(GTK_WIDGET(p->battery_bar), GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(p->row_battery),
                              GTK_WIDGET(p->battery_bar));

    adw_preferences_group_add(group, GTK_WIDGET(p->row_link));
    adw_preferences_group_add(group, GTK_WIDGET(p->row_firmware));
    adw_preferences_group_add(group, GTK_WIDGET(p->row_battery));
    adw_preferences_group_add(group, GTK_WIDGET(p->row_profile));
    adw_preferences_group_add(group, GTK_WIDGET(p->row_dpi_now));
    return group;
}

static AdwPreferencesGroup *build_sensor(AsMousePage *p)
{
    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());

    adw_preferences_group_set_title(group, "Сенсор");
    p->sensor_group = group;

    p->row_rate = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->row_rate),
                                  "Частота опроса");
    adw_combo_row_set_model(p->row_rate, G_LIST_MODEL(as_ui_rate_list()));
    g_signal_connect(p->row_rate, "notify::selected",
                     G_CALLBACK(apply_rate), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_rate));

    for (int i = 0; i < AS_MAX_DPI_STAGES; i++) {
        AdwSpinRow *row = ADW_SPIN_ROW(adw_spin_row_new_with_range(50, 60000, 50));

        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Этап DPI");
        adw_spin_row_set_digits(row, 0);
        g_object_set_data(G_OBJECT(row), "as-stage", GINT_TO_POINTER(i));
        g_signal_connect(row, "notify::value", G_CALLBACK(apply_dpi), p);
        p->row_dpi[i] = row;
        adw_preferences_group_add(group, GTK_WIDGET(row));
    }

    p->row_lod = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, 4, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->row_lod),
                                  "Отрыв (LOD)");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->row_lod),
                                "значение драйвера");
    adw_spin_row_set_digits(p->row_lod, 0);
    g_object_set_data(G_OBJECT(p->row_lod), "as-op",
                      GINT_TO_POINTER(AS_OP_MOUSE_LOD));
    g_signal_connect(p->row_lod, "notify::value", G_CALLBACK(apply_u8), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_lod));

    p->row_debounce = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 20, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->row_debounce),
                                  "Debounce");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->row_debounce), "мс");
    adw_spin_row_set_digits(p->row_debounce, 0);
    g_object_set_data(G_OBJECT(p->row_debounce), "as-op",
                      GINT_TO_POINTER(AS_OP_MOUSE_DEBOUNCE));
    g_signal_connect(p->row_debounce, "notify::value", G_CALLBACK(apply_u8), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_debounce));

    p->sw_motion = as_ui_switch_row("Motion sync", NULL);
    g_object_set_data(G_OBJECT(p->sw_motion), "as-op",
                      GINT_TO_POINTER(AS_OP_MOUSE_MOTION_SYNC));
    g_signal_connect(p->sw_motion, "notify::active",
                     G_CALLBACK(apply_toggle), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_motion));

    p->sw_angle = as_ui_switch_row("Angle snap", NULL);
    g_object_set_data(G_OBJECT(p->sw_angle), "as-op",
                      GINT_TO_POINTER(AS_OP_MOUSE_ANGLE_SNAP));
    g_signal_connect(p->sw_angle, "notify::active",
                     G_CALLBACK(apply_toggle), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_angle));

    p->sw_ripple = as_ui_switch_row("Ripple control", NULL);
    g_object_set_data(G_OBJECT(p->sw_ripple), "as-op",
                      GINT_TO_POINTER(AS_OP_MOUSE_RIPPLE));
    g_signal_connect(p->sw_ripple, "notify::active",
                     G_CALLBACK(apply_toggle), p);
    adw_preferences_group_add(group, GTK_WIDGET(p->sw_ripple));
    return group;
}

GtkWidget *as_mouse_page_new(AsStore *store, const char *path)
{
    AsMousePage *p = g_new0(AsMousePage, 1);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    p->store = store;
    p->path = g_strdup(path);
    p->root = box;

    p->banner = ADW_BANNER(adw_banner_new(""));
    adw_banner_set_revealed(p->banner, FALSE);

    adw_preferences_page_add(page, build_status(p));
    adw_preferences_page_add(page, build_sensor(p));

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
