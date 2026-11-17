// SPDX-License-Identifier: MIT
#include <stdio.h>

#include "as-ui.h"

typedef struct {
    AsPage page;

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
    AdwSwitchRow *sw_performance;
    AdwSpinRow *row_performance;
    AdwPreferencesGroup *sensor_group;
} AsMousePage;

/* ---------------------------- write helpers ---------------------------- */

static void apply_rate(AdwComboRow *row, GParamSpec *pspec, gpointer data)
{
    AsMousePage *p = data;
    int hz = as_ui_rate_from_index(adw_combo_row_get_selected(row));
    AsOpRequest req = { .op = AS_OP_MOUSE_RATE, .value = hz };

    (void)pspec;
    if (!p->page.updating && hz >= 0) {
        as_store_apply(p->page.store, p->page.path, &req);
    }
}

static void apply_dpi(AdwSpinRow *row, GParamSpec *pspec, gpointer data)
{
    AsMousePage *p = data;
    unsigned value;
    AsOpRequest req;

    (void)pspec;
    if (p->page.updating) {
        return;
    }
    value = as_ui_snap_dpi((unsigned)adw_spin_row_get_value(row));
    if (value != (unsigned)adw_spin_row_get_value(row)) {
        as_ui_spin_set(row, &p->page.updating, value);
    }
    gtk_adjustment_set_step_increment(adw_spin_row_get_adjustment(row),
                                      value >= 30100 ? 100 : 50);
    req = (AsOpRequest){
        .op = AS_OP_MOUSE_DPI_STAGE,
        .index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "as-stage")),
        .value = (int)value,
    };
    as_page_queue(&p->page, &req);
}

static void apply_u8(AdwSpinRow *row, GParamSpec *pspec, gpointer data)
{
    AsMousePage *p = data;
    AsOpRequest req = {
        .op = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "as-op")),
        .value = (int)adw_spin_row_get_value(row),
    };

    (void)pspec;
    if (!p->page.updating) {
        as_page_queue(&p->page, &req);
    }
}

static void apply_toggle(AdwSwitchRow *row, GParamSpec *pspec, gpointer data)
{
    AsMousePage *p = data;
    AsOpRequest req = {
        .op = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "as-op")),
        .value = adw_switch_row_get_active(row),
    };

    (void)pspec;
    if (!p->page.updating) {
        as_store_apply(p->page.store, p->page.path, &req);
    }
}

/* ------------------------------ refreshing ----------------------------- */

static void update_page(AsMousePage *p)
{
    AsDevice *dev = as_page_device(&p->page);

    if (!dev) {
        return;
    }
    p->page.updating = TRUE;

    if (dev->error[0]) {
        as_page_banner(&p->page, as_ui_error(dev));
    } else if (!dev->online) {
        as_page_banner(&p->page,
                       "Мышь спит или выключена. Пошевелите её, чтобы менять "
                       "настройки.");
    } else {
        as_page_banner(&p->page, NULL);
    }

    as_ui_row_set_value(p->row_link, dev->online ? "есть" : "нет");
    if (dev->fw_valid) {
        char dongle[64] = "";

        if (dev->dongle_valid) {
            snprintf(dongle, sizeof(dongle), " · донгл v%d.%d.%d",
                     dev->dongle[0], dev->dongle[1], dev->dongle[2]);
        }
        as_ui_row_format(p->row_firmware, "v%d.%02x%s", dev->fw_major,
                         dev->fw_minor, dongle);
    } else {
        as_ui_row_set_value(p->row_firmware, "—");
    }

    if (dev->battery > 0) {
        char volt[32] = "";

        if (dev->voltage) {
            snprintf(volt, sizeof(volt), " · %d мВ", dev->voltage);
        }
        as_ui_row_format(p->row_battery, "%d%%%s%s", dev->battery,
                         dev->charging ? " · зарядка" : "", volt);
        gtk_level_bar_set_value(p->battery_bar, dev->battery / 100.0);
    } else {
        as_ui_row_set_value(p->row_battery, "—");
        gtk_level_bar_set_value(p->battery_bar, 0);
    }

    if (dev->profile >= 0) {
        as_ui_row_format(p->row_profile, "%d", dev->profile + 1);
    }
    if (dev->dpi_stages > 0) {
        as_ui_row_format(p->row_dpi_now, "%d из %d", dev->active_stage + 1,
                         dev->dpi_stages);
    }

    gtk_widget_set_sensitive(GTK_WIDGET(p->sensor_group), dev->config_valid);
    as_ui_combo_set(p->row_rate, &p->page.updating,
                    as_ui_rate_to_index(dev->rate));

    for (int i = 0; i < AS_MAX_DPI_STAGES; i++) {
        gboolean active = i < dev->dpi_stages;

        gtk_widget_set_visible(GTK_WIDGET(p->row_dpi[i]), active);
        if (active) {
            as_ui_spin_set(p->row_dpi[i], &p->page.updating, dev->dpi[i]);
            gtk_adjustment_set_step_increment(
                adw_spin_row_get_adjustment(p->row_dpi[i]),
                dev->dpi[i] >= 30100 ? 100 : 50);
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->row_dpi[i]),
                                          i == dev->active_stage
                                              ? "Активный этап DPI"
                                              : "Этап DPI");
        }
    }

    if (dev->lod >= 0) {
        as_ui_spin_set(p->row_lod, &p->page.updating, dev->lod);
    }
    if (dev->debounce >= 0) {
        as_ui_spin_set(p->row_debounce, &p->page.updating, dev->debounce);
    }
    if (dev->motion_sync >= 0) {
        as_ui_switch_set(p->sw_motion, &p->page.updating, dev->motion_sync);
    }
    if (dev->angle_snap >= 0) {
        as_ui_switch_set(p->sw_angle, &p->page.updating, dev->angle_snap);
    }
    if (dev->ripple >= 0) {
        as_ui_switch_set(p->sw_ripple, &p->page.updating, dev->ripple);
    }
    if (dev->performance_state >= 0) {
        as_ui_switch_set(p->sw_performance, &p->page.updating,
                         dev->performance_state);
    }
    if (dev->performance >= 0) {
        as_ui_spin_set(p->row_performance, &p->page.updating, dev->performance);
    }

    p->page.updating = FALSE;
}

static void on_store_changed(AsStore *store, gpointer box)
{
    (void)store;
    update_page(g_object_get_data(G_OBJECT(box), "as-page"));
}

/* ------------------------------ construction --------------------------- */

static AdwPreferencesGroup *build_status(AsMousePage *p)
{
    AdwPreferencesGroup *group = as_ui_group(
        "Состояние", "Читается из самого устройства при каждом обновлении");

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

/* One spin row with an operation tag and a value-change handler. */
static AdwSpinRow *spin_row(AsMousePage *p, const char *title, int op,
                            double min, double max, double step,
                            const char *subtitle)
{
    AdwSpinRow *row = as_ui_spin(title, min, max, step, subtitle);

    g_object_set_data(G_OBJECT(row), "as-op", GINT_TO_POINTER(op));
    g_signal_connect(row, "notify::value", G_CALLBACK(apply_u8), p);
    adw_preferences_group_add(p->sensor_group, GTK_WIDGET(row));
    return row;
}

static AdwSwitchRow *switch_row(AsMousePage *p, const char *title, int op)
{
    AdwSwitchRow *row = as_ui_switch_row(title, NULL);

    g_object_set_data(G_OBJECT(row), "as-op", GINT_TO_POINTER(op));
    g_signal_connect(row, "notify::active", G_CALLBACK(apply_toggle), p);
    adw_preferences_group_add(p->sensor_group, GTK_WIDGET(row));
    return row;
}

static AdwPreferencesGroup *build_sensor(AsMousePage *p)
{
    AdwPreferencesGroup *group = as_ui_group("Сенсор", NULL);

    p->sensor_group = group;
    p->row_rate = as_ui_combo("Частота опроса",
                              G_LIST_MODEL(as_ui_rate_list()));
    g_signal_connect(p->row_rate, "notify::selected", G_CALLBACK(apply_rate),
                     p);
    adw_preferences_group_add(group, GTK_WIDGET(p->row_rate));

    for (int i = 0; i < AS_MAX_DPI_STAGES; i++) {
        AdwSpinRow *row = as_ui_spin("Этап DPI", 50, 60000, 50, NULL);

        g_object_set_data(G_OBJECT(row), "as-stage", GINT_TO_POINTER(i));
        g_signal_connect(row, "notify::value", G_CALLBACK(apply_dpi), p);
        p->row_dpi[i] = row;
        adw_preferences_group_add(group, GTK_WIDGET(row));
    }

    p->row_lod = spin_row(p, "Отрыв (LOD)", AS_OP_MOUSE_LOD, 1, 4, 1,
                          "значение драйвера");
    p->row_debounce = spin_row(p, "Debounce", AS_OP_MOUSE_DEBOUNCE, 0, 20, 1,
                               "мс");
    p->sw_motion = switch_row(p, "Motion sync", AS_OP_MOUSE_MOTION_SYNC);
    p->sw_angle = switch_row(p, "Angle snap", AS_OP_MOUSE_ANGLE_SNAP);
    p->sw_ripple = switch_row(p, "Ripple control", AS_OP_MOUSE_RIPPLE);
    p->sw_performance = switch_row(p, "Hunting Shark", AS_OP_MOUSE_PERFORMANCE_STATE);
    p->row_performance = spin_row(p, "Время Hunting Shark", AS_OP_MOUSE_PERFORMANCE,
                                  1, 90, 1, "секунд");
    return group;
}

GtkWidget *as_mouse_page_new(AsStore *store, const char *path)
{
    AsMousePage *p = g_new0(AsMousePage, 1);
    AdwPreferencesPage *prefs = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    as_page_attach(&p->page, box, prefs, store, path);
    adw_preferences_page_add(prefs, build_status(p));
    adw_preferences_page_add(prefs, build_sensor(p));
    g_signal_connect_object(store, "changed", G_CALLBACK(on_store_changed),
                            box, 0);
    update_page(p);
    return box;
}
