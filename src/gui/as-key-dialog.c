// SPDX-License-Identifier: MIT
/*
 * Modal keyboard layout: the R85 HE is a 68-key board, and its firmware does
 * not expose the matrix over the config channel. The geometry below comes
 * from the vendor layout for the same product id (3151:5029); pos is the
 * matrix position the Hall-effect tables use for that key.
 */
#include "as-key-dialog.h"

#include "as-ui.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int pos;
    unsigned code;
    int x, y, w, h;
} KeyBox;

#define KEY_GAP 3
#define KEY_HEIGHT 40
#define KEY_WIDTH 750

static const KeyBox keyboxes[] = {
    { 1, 0x00000029, 0, 0, 40, 40 },      /* Esc */
    { 7, 0x0000001e, 45, 0, 40, 40 },     /* 1 */
    { 13, 0x0000001f, 90, 0, 40, 40 },    /* 2 */
    { 19, 0x00000020, 135, 0, 40, 40 },   /* 3 */
    { 25, 0x00000021, 180, 0, 40, 40 },   /* 4 */
    { 31, 0x00000022, 225, 0, 40, 40 },   /* 5 */
    { 37, 0x00000023, 270, 0, 40, 40 },   /* 6 */
    { 43, 0x00000024, 315, 0, 40, 40 },   /* 7 */
    { 49, 0x00000025, 360, 0, 40, 40 },   /* 8 */
    { 55, 0x00000026, 405, 0, 40, 40 },   /* 9 */
    { 61, 0x00000027, 450, 0, 40, 40 },   /* 0 */
    { 67, 0x0000002d, 495, 0, 40, 40 },   /* - */
    { 73, 0x0000002e, 540, 0, 40, 40 },   /* = */
    { 79, 0x0000002a, 585, 0, 87, 40 },   /* Backspace */
    { 85, 0x00000049, 677, 0, 40, 40 },   /* Ins */
    { 2, 0x0000002b, 0, 45, 60, 40 },     /* Tab */
    { 8, 0x00000014, 65, 45, 40, 40 },    /* Q */
    { 14, 0x0000001a, 110, 45, 40, 40 },  /* W */
    { 20, 0x00000008, 155, 45, 40, 40 },  /* E */
    { 26, 0x00000015, 200, 45, 40, 40 },  /* R */
    { 32, 0x00000017, 245, 45, 40, 40 },  /* T */
    { 38, 0x0000001c, 290, 45, 40, 40 },  /* Y */
    { 44, 0x00000018, 335, 45, 40, 40 },  /* U */
    { 50, 0x0000000c, 380, 45, 40, 40 },  /* I */
    { 56, 0x00000012, 425, 45, 40, 40 },  /* O */
    { 62, 0x00000013, 470, 45, 40, 40 },  /* P */
    { 68, 0x0000002f, 515, 45, 40, 40 },  /* [ */
    { 74, 0x00000030, 560, 45, 40, 40 },  /* ] */
    { 80, 0x00000031, 605, 45, 67, 40 },  /* \ */
    { 86, 0x0000004c, 677, 45, 40, 40 },  /* Del */
    { 3, 0x00000039, 0, 90, 70, 40 },     /* Caps */
    { 9, 0x00000004, 75, 90, 40, 40 },    /* A */
    { 15, 0x00000016, 120, 90, 40, 40 },  /* S */
    { 21, 0x00000007, 165, 90, 40, 40 },  /* D */
    { 27, 0x00000009, 210, 90, 40, 40 },  /* F */
    { 33, 0x0000000a, 255, 90, 40, 40 },  /* G */
    { 39, 0x0000000b, 300, 90, 40, 40 },  /* H */
    { 45, 0x0000000d, 345, 90, 40, 40 },  /* J */
    { 51, 0x0000000e, 390, 90, 40, 40 },  /* K */
    { 57, 0x0000000f, 435, 90, 40, 40 },  /* L */
    { 63, 0x00000033, 480, 90, 40, 40 },  /* ; */
    { 69, 0x00000034, 525, 90, 40, 40 },  /* ' */
    { 81, 0x00000028, 570, 90, 102, 40 }, /* Enter */
    { 87, 0x0000004b, 677, 90, 40, 40 },  /* PgUp */
    { 4, 0x000000e1, 0, 135, 95, 40 },    /* LShift */
    { 16, 0x0000001d, 100, 135, 40, 40 }, /* Z */
    { 22, 0x0000001b, 145, 135, 40, 40 }, /* X */
    { 28, 0x00000006, 190, 135, 40, 40 }, /* C */
    { 34, 0x00000019, 235, 135, 40, 40 }, /* V */
    { 40, 0x00000005, 280, 135, 40, 40 }, /* B */
    { 46, 0x00000011, 325, 135, 40, 40 }, /* N */
    { 52, 0x00000010, 370, 135, 40, 40 }, /* M */
    { 58, 0x00000036, 415, 135, 40, 40 }, /* , */
    { 64, 0x00000037, 460, 135, 40, 40 }, /* . */
    { 70, 0x00000038, 505, 135, 40, 40 }, /* / */
    { 76, 0x000000e5, 550, 135, 77, 40 }, /* RShift */
    { 82, 0x00000052, 632, 135, 40, 40 }, /* Up */
    { 88, 0x0000004e, 677, 135, 40, 40 }, /* PgDn */
    { 5, 0x000000e0, 0, 180, 50, 40 },    /* LCtrl */
    { 11, 0x000000e3, 55, 180, 50, 40 },  /* LWin */
    { 17, 0x000000e2, 110, 180, 50, 40 }, /* LAlt */
    { 41, 0x0000002c, 165, 180, 283, 40 },/* Space */
    { 59, 0x000000e6, 452, 180, 40, 40 }, /* RAlt */
    { 65, 0x0a010000, 497, 180, 40, 40 }, /* Fn */
    { 71, 0x000000e4, 542, 180, 40, 40 }, /* RCtrl */
    { 77, 0x00000050, 587, 180, 40, 40 }, /* Left */
    { 83, 0x00000051, 632, 180, 40, 40 }, /* Down */
    { 89, 0x0000004f, 677, 180, 40, 40 }, /* Right */
};

typedef struct {
    GtkWindow *window;
    AsStore *store;
    char path[64];
    uint8_t selected[AS_KB_KEY_MAX];
    GtkWidget *buttons[G_N_ELEMENTS(keyboxes)];
    AdwActionRow *actuation;
    AdwActionRow *release;
    AdwActionRow *rt_press;
    AdwActionRow *rt_release;
    AdwActionRow *deadzone;
    double act, rel, rtp, rtr, dz;
    AdwSwitchRow *rt;
    GtkWidget *apply;
    GtkWidget *status;
    AdwComboRow *switch_row;
    guint32 signature;
    gboolean updating;
} KeyDialog;

static void load_values(KeyDialog *d);

static guint32 device_signature(const AsDevice *dev)
{
    guint32 h = (guint32)dev->key_option[0];

    for (int i = 0; i < AS_KB_KEY_MAX; i++) {
        h = h * 31u + (guint32)(dev->key_actuation[i] * 1000.0);
        h = h * 31u + (guint32)(dev->key_rt_press[i] * 1000.0);
        h = h * 31u + dev->key_option[i];
        h = h * 31u + dev->key_pair[i];
    }
    return h;
}

static void refresh_from_device(KeyDialog *d)
{
    AsDevice *dev = as_store_lookup(d->store, d->path);

    if (!dev) {
        return;
    }
    d->signature = device_signature(dev);
    load_values(d);
}

static void on_store_changed(AsStore *store, gpointer data)
{
    KeyDialog *d = data;
    AsDevice *dev = as_store_lookup(store, d->path);

    (void)store;
    if (dev && device_signature(dev) != d->signature) {
        refresh_from_device(d);
    }
}

static gboolean refresh_later(gpointer data)
{
    KeyDialog *d = data;

    refresh_from_device(d);
    return G_SOURCE_REMOVE;
}

/* The sliders stop at the two millimetres the firmware accepts for a trigger
 * point; the bottom-out distance of the switch is listed only for reference,
 * because it belongs to the switch, not to the profile. */
#define KEY_MAX_TRIGGER 2.0
#define KEY_MIN_TRIGGER 0.01

/* Switches the vendor lists for this family, with their bottom-out travel. */
static const struct {
    const char *name;
    const char *bottom_out;
} switch_presets[] = {
    { "Gateron Magnetic Jade", "3.4 мм" },
    { "Gateron Magnetic Jade Pro", "3.4 мм" },
    { "Gateron Magnetic Jade Gaming", "3.4 мм" },
    { "Geon Raw HE", "3.4 мм" },
    { "TTC King", "3.5 мм" },
    { "Outemu Silent Ocean", "3.4 мм" },
};

static const char *short_key_name(unsigned code, char *buf, size_t len)
{
    static const struct {
        const char *name;
        const char *label;
    } map[] = {
        { "LShift", "Shift" }, { "RShift", "Shift" },
        { "LCtrl", "Ctrl" },   { "RCtrl", "Ctrl" },
        { "LAlt", "Alt" },     { "RAlt", "Alt" },
        { "LWin", "Win" },     { "RWin", "Win" },
        { "Backspace", "⌫" },  { "Enter", "⏎" },
    };
    char name[32];

    as_kb_key_name(code, name, sizeof(name));
    for (size_t i = 0; i < G_N_ELEMENTS(map); i++) {
        if (!strcmp(name, map[i].name)) {
            g_strlcpy(buf, map[i].label, len);
            return buf;
        }
    }
    g_strlcpy(buf, name[0] ? name : "?", len);
    return buf;
}

static int selection_count(const KeyDialog *d)
{
    int n = 0;

    for (int i = 0; i < AS_KB_KEY_MAX; i++) {
        n += d->selected[i] ? 1 : 0;
    }
    return n;
}

static int first_selected(const KeyDialog *d)
{
    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        if (d->selected[keyboxes[i].pos]) {
            return keyboxes[i].pos;
        }
    }
    return -1;
}

static void load_values(KeyDialog *d)
{
    AsDevice *dev = as_store_lookup(d->store, d->path);
    int key = first_selected(d);
    gboolean previous;

    if (!dev || key < 0) {
        return;
    }
    previous = d->updating;
    d->updating = TRUE;
    as_ui_slider_set(d->actuation, dev->key_actuation[key]);
    as_ui_slider_set(d->release, dev->key_release[key]);
    as_ui_slider_set(d->rt_press, dev->key_rt_press[key]);
    as_ui_slider_set(d->rt_release, dev->key_rt_release[key]);
    as_ui_slider_set(d->deadzone, dev->key_deadzone[key]);
    as_ui_switch_set(d->rt, &d->updating,
                     dev->key_option[key] & AS_KB_OPTION_RT);
    d->updating = previous;
}

static void update_status(KeyDialog *d)
{
    char buf[64];
    int n = selection_count(d);

    snprintf(buf, sizeof(buf), "Выбрано клавиш: %d", n);
    gtk_label_set_text(GTK_LABEL(d->status), buf);
    gtk_widget_set_sensitive(d->apply, n > 0);
}

static void on_key_toggled(GtkToggleButton *button, gpointer data)
{
    KeyDialog *d = data;
    int pos = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "as-pos"));

    if (d->updating) {
        return;
    }
    d->selected[pos] = gtk_toggle_button_get_active(button) ? 1 : 0;
    update_status(d);
    load_values(d);
}

static void sync_selection(KeyDialog *d)
{
    memset(d->selected, 0, sizeof(d->selected));
    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->buttons[i]))) {
            d->selected[keyboxes[i].pos] = 1;
        }
    }
    update_status(d);
    load_values(d);
}

static void select_all(KeyDialog *d, gboolean active)
{
    d->updating = TRUE;
    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->buttons[i]), active);
    }
    d->updating = FALSE;
    sync_selection(d);
}

static void select_codes(KeyDialog *d, unsigned lo, unsigned hi)
{
    d->updating = TRUE;
    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        if (keyboxes[i].code >= lo && keyboxes[i].code <= hi) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->buttons[i]),
                                         TRUE);
        }
    }
    d->updating = FALSE;
    sync_selection(d);
}

static void on_select_all(GtkButton *button, gpointer data)
{
    (void)button;
    select_all(data, TRUE);
}

static void on_select_none(GtkButton *button, gpointer data)
{
    (void)button;
    select_all(data, FALSE);
}

static void on_select_letters(GtkButton *button, gpointer data)
{
    (void)button;
    select_codes(data, 4, 29);
}

static void on_select_digits(GtkButton *button, gpointer data)
{
    (void)button;
    select_codes(data, 30, 39);
}

static void on_select_modifiers(GtkButton *button, gpointer data)
{
    (void)button;
    select_codes(data, 224, 231);
}

static void on_apply(GtkButton *button, gpointer data)
{
    KeyDialog *d = data;
    AsOpRequest req = {
        .op = AS_OP_KB_KEYS_TRAVEL,
        .rapid_trigger = adw_switch_row_get_active(d->rt),
        .actuation = d->act,
        .release = d->rel,
        .rt_press = d->rtp,
        .rt_release = d->rtr,
        .deadzone = d->dz,
    };
    char buf[64];

    (void)button;
    /* The firmware accepts a trigger point up to 2 mm; anything longer is
     * the bottom-out of the switch, not a trigger. */
    if (req.actuation > KEY_MAX_TRIGGER) {
        req.actuation = KEY_MAX_TRIGGER;
    }
    if (req.release > KEY_MAX_TRIGGER) {
        req.release = KEY_MAX_TRIGGER;
    }
    if (req.rt_press > KEY_MAX_TRIGGER) {
        req.rt_press = KEY_MAX_TRIGGER;
    }
    if (req.rt_release > KEY_MAX_TRIGGER) {
        req.rt_release = KEY_MAX_TRIGGER;
    }
    memcpy(req.keys, d->selected, sizeof(req.keys));
    if (!as_store_apply(d->store, d->path, &req)) {
        return;
    }
    snprintf(buf, sizeof(buf), "Применено к клавишам: %d",
             selection_count(d));
    gtk_label_set_text(GTK_LABEL(d->status), buf);
}

/* Every switch gets the same starting point the vendor uses in its "gaming"
 * mode: 1.5 mm trigger, 0.2 mm Rapid Trigger offsets. The type only decides
 * which switch names are offered, since the firmware stores one profile for
 * all keys. */
static void on_switch_type(AdwComboRow *row, GParamSpec *pspec, gpointer data)
{
    KeyDialog *d = data;
    guint selected = adw_combo_row_get_selected(row);
    gboolean previous;

    (void)pspec;
    if (d->updating || selected >= G_N_ELEMENTS(switch_presets)) {
        return;
    }
    previous = d->updating;
    d->updating = TRUE;
    as_ui_slider_set(d->actuation, 1.5);
    as_ui_slider_set(d->release, 1.5);
    as_ui_slider_set(d->rt_press, 0.2);
    as_ui_slider_set(d->rt_release, 0.2);
    as_ui_switch_set(d->rt, &d->updating, TRUE);
    d->updating = previous;
}

static void on_close(GtkButton *button, gpointer data)
{
    (void)button;
    gtk_window_destroy(GTK_WINDOW(data));
}

static AdwActionRow *make_slider(AdwPreferencesGroup *group, const char *title,
                                 double min, double max, double *out,
                                 double value, KeyDialog *d)
{
    AdwActionRow *row = as_ui_slider_row(title, min, max, 0.05, value, out,
                                         NULL, d);

    adw_preferences_group_add(group, GTK_WIDGET(row));
    return row;
}

static GtkWidget *make_button(const char *label, GCallback callback,
                              gpointer data)
{
    GtkWidget *button = gtk_button_new_with_label(label);

    g_signal_connect(button, "clicked", callback, data);
    return button;
}

static void dialog_free(gpointer data)
{
    g_free(data);
}

static void load_css(void)
{
    static gboolean loaded;
    GtkCssProvider *provider;

    if (loaded) {
        return;
    }
    loaded = TRUE;
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        provider,
        "button.keycap { min-height: 0; min-width: 0; padding: 0 2px;"
        "  font-size: 11px; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void as_key_dialog_open(GtkWindow *parent, AsStore *store, const char *path)
{
    KeyDialog *d = g_new0(KeyDialog, 1);
    GtkWidget *window = gtk_window_new();
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *description = gtk_label_new(
        "Нажмите клавиши, чтобы выбрать их, затем задайте ход. Настройка "
        "применится только к выбранным клавишам.");
    GtkWidget *fixed = gtk_fixed_new();
    GtkWidget *select = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    AdwPreferencesGroup *group = as_ui_group(
        "Ход выбранных клавиш", "Показаны значения первой выбранной клавиши.");

    d->store = store;
    g_strlcpy(d->path, path, sizeof(d->path));
    load_css();
    d->window = GTK_WINDOW(window);
    g_object_set_data_full(G_OBJECT(window), "as-key-dialog", d, dialog_free);
    gtk_window_set_title(d->window, "Раскладка клавиш");
    gtk_window_set_transient_for(d->window, parent);
    gtk_window_set_modal(d->window, TRUE);
    gtk_window_set_default_size(d->window, 840, 620);

    gtk_widget_set_margin_top(outer, 18);
    gtk_widget_set_margin_bottom(outer, 18);
    gtk_widget_set_margin_start(outer, 18);
    gtk_widget_set_margin_end(outer, 18);
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_label_set_xalign(GTK_LABEL(description), 0.0);
    gtk_box_append(GTK_BOX(outer), description);

    gtk_widget_set_size_request(fixed, KEY_WIDTH, 220);
    gtk_widget_set_halign(fixed, GTK_ALIGN_CENTER);
    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        char label[16];
        GtkWidget *button = gtk_toggle_button_new_with_label(
            short_key_name(keyboxes[i].code, label, sizeof(label)));

        /* GtkFixed places widgets at their natural size, so the gaps between
         * the vendor coordinates are folded into each key instead. */
        gtk_widget_set_size_request(button, keyboxes[i].w - KEY_GAP,
                                    KEY_HEIGHT - KEY_GAP);
        gtk_widget_set_hexpand(button, FALSE);
        gtk_widget_set_halign(button, GTK_ALIGN_FILL);
        gtk_widget_set_valign(button, GTK_ALIGN_FILL);
        gtk_widget_add_css_class(button, "keycap");
        gtk_fixed_put(GTK_FIXED(fixed), button, keyboxes[i].x,
                      keyboxes[i].y);
        g_object_set_data(G_OBJECT(button), "as-pos",
                          GINT_TO_POINTER(keyboxes[i].pos));
        g_signal_connect(button, "toggled", G_CALLBACK(on_key_toggled), d);
        d->buttons[i] = button;
    }
    gtk_box_append(GTK_BOX(outer), fixed);

    gtk_box_append(GTK_BOX(select), make_button("Все", G_CALLBACK(on_select_all), d));
    gtk_box_append(GTK_BOX(select),
                   make_button("Буквы", G_CALLBACK(on_select_letters), d));
    gtk_box_append(GTK_BOX(select),
                   make_button("Цифры", G_CALLBACK(on_select_digits), d));
    gtk_box_append(GTK_BOX(select),
                   make_button("Спец. клавиши",
                               G_CALLBACK(on_select_modifiers), d));
    gtk_box_append(GTK_BOX(select),
                   make_button("Снять", G_CALLBACK(on_select_none), d));
    gtk_box_append(GTK_BOX(outer), select);

    {
        GtkStringList *types = gtk_string_list_new(NULL);

        for (size_t i = 0; i < G_N_ELEMENTS(switch_presets); i++) {
            char label[96];

            snprintf(label, sizeof(label), "%s, ход %s",
                     switch_presets[i].name, switch_presets[i].bottom_out);
            gtk_string_list_append(types, label);
        }
        d->switch_row = as_ui_combo("Тип свитчей", G_LIST_MODEL(types));
        g_signal_connect(d->switch_row, "notify::selected",
                         G_CALLBACK(on_switch_type), d);
        adw_preferences_group_add(group, GTK_WIDGET(d->switch_row));
    }
    d->actuation = make_slider(group, "Точка срабатывания", 0.1, 2.0,
                               &d->act, 1.5, d);
    d->release = make_slider(group, "Точка отпускания", 0.1, 2.0, &d->rel, 1.5,
                             d);
    d->rt_press = make_slider(group, "Rapid Trigger: нажатие", 0.1, 2.0,
                              &d->rtp, 0.2, d);
    d->rt_release = make_slider(group, "Rapid Trigger: отпускание", 0.1, 2.0,
                                &d->rtr, 0.2, d);
    d->deadzone = make_slider(group, "Мёртвая зона", 0.0, 1.0, &d->dz, 0.3, d);
    d->rt = as_ui_switch_row("Rapid Trigger", NULL);
    adw_preferences_group_add(group, GTK_WIDGET(d->rt));
    gtk_box_append(GTK_BOX(outer), GTK_WIDGET(group));

    d->status = gtk_label_new("Выбрано клавиш: 0");
    gtk_label_set_xalign(GTK_LABEL(d->status), 0.0);
    gtk_box_append(GTK_BOX(outer), d->status);

    d->apply = gtk_button_new_with_label("Применить к выбранным");
    gtk_widget_add_css_class(d->apply, "suggested-action");
    gtk_widget_set_sensitive(d->apply, FALSE);
    g_signal_connect(d->apply, "clicked", G_CALLBACK(on_apply), d);
    gtk_box_append(GTK_BOX(actions), d->apply);
    gtk_box_append(GTK_BOX(actions),
                   make_button("Закрыть", G_CALLBACK(on_close), window));
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(outer), actions);

    gtk_window_set_child(d->window, outer);
    /* The page may still be waiting for the first scan; read the values now
     * and again once the store has them, and follow later writes the same
     * way so the sliders never show stale numbers. */
    g_signal_connect(store, "changed", G_CALLBACK(on_store_changed), d);
    gtk_window_present(d->window);
    g_timeout_add(500, refresh_later, d);
}