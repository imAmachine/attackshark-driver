// SPDX-License-Identifier: MIT
/*
 * Modal keyboard layout: the R85 HE is a 68-key board, and its firmware does
 * not expose the matrix over the config channel. The geometry below comes
 * from the vendor layout for the same product id (3151:5029); pos is the
 * matrix position the Hall-effect tables use for that key.
 */
#include "as-key-dialog.h"

#include "as-ui.h"

#include <cairo.h>
#include <math.h>
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
    GtkWidget *group_buttons[4];
    GtkWidget *demo_area;
    AdwSwitchRow *demo_switch;
    int demo_key;           /* matrix position the demo follows */
    double switch_bottom;   /* travel of the selected switch type, mm */
    gboolean demo_active;
    gboolean edited;        /* the user changed a slider: keep those values */
    guint refresh_id;
    guint32 signature;
    gboolean updating;
    guint64 pending_request;
    gboolean edited_while_pending;
    int pending_count;
} KeyDialog;

enum {
    GROUP_ALL,
    GROUP_LETTERS,
    GROUP_DIGITS,
    GROUP_SPECIAL,
};

static gboolean group_contains(int group, unsigned code)
{
    switch (group) {
    case GROUP_LETTERS:
        return code >= 4 && code <= 29;
    case GROUP_DIGITS:
        return code >= 30 && code <= 39;
    case GROUP_SPECIAL:
        return code >= 224 && code <= 231;
    default:
        return TRUE;
    }
}

static void load_values(KeyDialog *d);
static void refresh_key_styles(KeyDialog *d);

static void refresh_from_device(KeyDialog *d)
{
    AsDevice *dev = as_store_lookup(d->store, d->path);

    if (!dev) {
        return;
    }
    d->signature = as_ui_device_signature(dev);
    refresh_key_styles(d);
    if (!d->edited) {
        load_values(d);
    }
}

static void on_store_changed(AsStore *store, gpointer data)
{
    KeyDialog *d = data;
    AsDevice *dev = as_store_lookup(store, d->path);

    (void)store;
    if (dev && as_ui_device_signature(dev) != d->signature) {
        refresh_from_device(d);
    }
}

/* A failed write shows up as a toast behind this modal window, so the reason
 * is repeated where the user is looking. */
static void on_store_error(AsStore *store, const char *path,
                           const char *message, gpointer data)
{
    KeyDialog *d = data;

    (void)store;
    if (path && g_strcmp0(path, d->path) != 0) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(d->status), message);
}

static gboolean refresh_later(gpointer data)
{
    KeyDialog *d = data;

    d->refresh_id = 0;
    refresh_from_device(d);
    return G_SOURCE_REMOVE;
}

/* The sliders stop at the two millimetres the firmware accepts for a trigger
 * point; the bottom-out distance of the switch is listed only for reference,
 * because it belongs to the switch, not to the profile. */
#define KEY_MAX_TRIGGER 2.0

/* Switches the vendor lists for this family, with their bottom-out travel. */
static const struct {
    const char *name;
    const char *full_name;
    double bottom_out;
} switch_presets[] = {
    { "Gateron Jade", "Gateron Magnetic Jade", 3.4 },
    { "Jade Pro", "Gateron Magnetic Jade Pro", 3.4 },
    { "Jade Gaming", "Gateron Magnetic Jade Gaming", 3.4 },
    { "Geon Raw HE", "Geon Raw HE", 3.4 },
    { "TTC King", "TTC King", 3.5 },
    { "Outemu Silent", "Outemu Silent Ocean", 3.4 },
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

static void set_class(GtkWidget *widget, const char *name, gboolean on)
{
    if (on) {
        gtk_widget_add_css_class(widget, name);
    } else {
        gtk_widget_remove_css_class(widget, name);
    }
}

/* Most keys of a board keep the factory travel, so the value the layout shows
 * most often is the board-wide setting: a key that reads differently is one
 * the user tuned on its own. The board-wide value is not a protocol field,
 * it is only the starting point of the highlight below. */
static double bulk_travel(const AsDevice *dev, int release)
{
    int best = -1, best_count = 0;

    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        int pos = keyboxes[i].pos;
        double value = release ? dev->key_release[pos]
                               : dev->key_actuation[pos];
        int count = 0;

        for (size_t k = 0; k < G_N_ELEMENTS(keyboxes); k++) {
            int other = keyboxes[k].pos;
            double peer = release ? dev->key_release[other]
                                  : dev->key_actuation[other];

            if (fabs(peer - value) < 0.0025) {
                count++;
            }
        }
        if (count > best_count) {
            best_count = count;
            best = pos;
        }
    }
    if (best < 0) {
        return 0.0;
    }
    return release ? dev->key_release[best] : dev->key_actuation[best];
}

/* Shows what is already configured on the board: a tinted key has Rapid
 * Trigger, an outlined key belongs to a Snap Tap pair, and a bold key has a
 * travel different from the rest of the board. */
static void refresh_key_styles(KeyDialog *d)
{
    AsDevice *dev = as_store_lookup(d->store, d->path);
    double bulk_act, bulk_rel;

    if (!dev) {
        return;
    }
    bulk_act = bulk_travel(dev, 0);
    bulk_rel = bulk_travel(dev, 1);
    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        int pos = keyboxes[i].pos;
        gboolean snap = as_kb_snap_mode_of(dev->key_option, dev->key_pair,
                                           pos) >= 0;
        gboolean rt = (dev->key_option[pos] & AS_KB_OPTION_RT) != 0;
        gboolean changed =
            fabs(dev->key_actuation[pos] - bulk_act) > 0.0025 ||
            fabs(dev->key_release[pos] - bulk_rel) > 0.0025;

        set_class(d->buttons[i], "rt", rt);
        set_class(d->buttons[i], "snap", snap);
        set_class(d->buttons[i], "changed", changed);
    }
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
    refresh_key_styles(d);
}

static void refresh_group_buttons(KeyDialog *d);

static void update_status(KeyDialog *d)
{
    char buf[64];
    int n = selection_count(d);

    if (d->pending_request) {
        snprintf(buf, sizeof(buf), "Применение к клавишам: %d…", d->pending_count);
    } else {
        snprintf(buf, sizeof(buf), "Выбрано клавиш: %d", n);
    }
    gtk_label_set_text(GTK_LABEL(d->status), buf);
    gtk_widget_set_sensitive(d->apply, n > 0 && !d->pending_request);
    refresh_group_buttons(d);
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
    /* Selecting another key shows that key's values, but only while the user
     * has not typed a value of their own: otherwise picking the keys to
     * apply to would throw the entered travel away. */
    if (!d->edited) {
        load_values(d);
    }
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
    if (!d->edited) {
        load_values(d);
    }
}

static gboolean group_all_selected(const KeyDialog *d, int group)
{
    gboolean any = FALSE;

    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        if (!group_contains(group, keyboxes[i].code)) {
            continue;
        }
        any = TRUE;
        if (!d->selected[keyboxes[i].pos]) {
            return FALSE;
        }
    }
    return any;
}

static void refresh_group_buttons(KeyDialog *d)
{
    gboolean previous = d->updating;

    d->updating = TRUE;
    for (int group = 0; group < 4; group++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->group_buttons[group]),
                                     group_all_selected(d, group));
    }
    d->updating = previous;
}

static void on_none(GtkButton *button, gpointer data)
{
    (void)button;
    KeyDialog *d = data;

    d->updating = TRUE;
    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->buttons[i]), FALSE);
    }
    d->updating = FALSE;
    sync_selection(d);
}

static void on_group_toggled(GtkToggleButton *button, gpointer data)
{
    KeyDialog *d = data;
    int group = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "as-group"));
    gboolean select;

    if (d->updating) {
        return;
    }
    /* A second press on a group clears exactly that group, so a group that
     * was selected next to the others can be removed without losing them. */
    select = !group_all_selected(d, group);
    d->updating = TRUE;
    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        if (group_contains(group, keyboxes[i].code)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->buttons[i]),
                                         select);
        }
    }
    d->updating = FALSE;
    sync_selection(d);
}

static void on_operation_completed(AsStore *store, const char *path, int op,
                                   guint64 request_id, gboolean success,
                                   gpointer data)
{
    KeyDialog *d = data;
    char buf[128];

    (void)store;
    if (g_strcmp0(path, d->path) != 0 || op != AS_OP_KB_KEYS_TRAVEL ||
        !d->pending_request || request_id != d->pending_request) {
        return;
    }
    d->pending_request = 0;
    gtk_widget_set_sensitive(d->apply, selection_count(d) > 0);
    if (!success) {
        d->edited = TRUE;
        return;
    }
    d->edited = d->edited_while_pending;
    refresh_from_device(d);
    snprintf(buf, sizeof(buf), "Применено к клавишам: %d%s", d->pending_count,
             d->edited ? "; есть новые изменения" : "");
    gtk_label_set_text(GTK_LABEL(d->status), buf);
}

static void on_apply(GtkButton *button, gpointer data)
{
    static guint64 next_request_id;
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
    gboolean restart = d->demo_active;

    (void)button;
    if (d->pending_request || selection_count(d) == 0) {
        return;
    }
    req.request_id = ++next_request_id;
    /* The firmware does not take configuration commands while the travel
     * stream is running, so the demo pauses for the write and resumes after
     * it: the worker queue keeps that order. */
    if (restart) {
        as_store_analog_stop(d->store);
    }
    memcpy(req.keys, d->selected, sizeof(req.keys));
    if (!as_store_apply(d->store, d->path, &req)) {
        if (restart) {
            as_store_analog_start(d->store, d->path);
        }
        gtk_label_set_text(GTK_LABEL(d->status), "Устройство недоступно");
        return;
    }
    d->pending_request = req.request_id;
    d->pending_count = selection_count(d);
    d->edited_while_pending = FALSE;
    d->edited = TRUE;
    gtk_widget_set_sensitive(d->apply, FALSE);
    gtk_label_set_text(GTK_LABEL(d->status), "Применение…");
    if (restart) {
        as_store_analog_start(d->store, d->path);
    }
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
    d->switch_bottom = switch_presets[selected].bottom_out;
    as_ui_slider_set(d->actuation, AS_KB_DEFAULT_ACTUATION);
    as_ui_slider_set(d->release, AS_KB_DEFAULT_ACTUATION);
    as_ui_slider_set(d->rt_press, AS_KB_DEFAULT_RT);
    as_ui_slider_set(d->rt_release, AS_KB_DEFAULT_RT);
    as_ui_switch_set(d->rt, &d->updating, TRUE);
    d->updating = previous;
    d->edited = TRUE;
    d->edited_while_pending = TRUE;
}

static void on_close(GtkButton *button, gpointer data)
{
    (void)button;
    gtk_window_destroy(GTK_WINDOW(data));
}

static void on_slider_changed(gpointer data)
{
    KeyDialog *d = data;

    if (!d->updating) {
        d->edited = TRUE;
        d->edited_while_pending = TRUE;
    }
    gtk_widget_queue_draw(d->demo_area);
}

static void on_rt_toggle(AdwSwitchRow *row, GParamSpec *pspec, gpointer data)
{
    KeyDialog *d = data;

    (void)row;
    (void)pspec;
    if (!d->updating) {
        d->edited = TRUE;
        d->edited_while_pending = TRUE;
    }
}

static void on_demo_toggle(AdwSwitchRow *row, GParamSpec *pspec, gpointer data)
{
    KeyDialog *d = data;
    gboolean active = adw_switch_row_get_active(row);
    gboolean previous = d->updating;

    (void)pspec;
    if (d->updating) {
        return;
    }
    if (active && !as_store_analog_start(d->store, d->path)) {
        d->updating = TRUE;
        adw_switch_row_set_active(row, FALSE);
        d->updating = previous;
        gtk_label_set_text(GTK_LABEL(d->status),
                           "Поток хода недоступен для этой клавиатуры");
        return;
    }
    if (!active) {
        as_store_analog_stop(d->store);
    }
    d->demo_active = active;
    gtk_widget_set_visible(d->demo_area, active);
    gtk_widget_queue_draw(d->demo_area);
}

static AdwActionRow *make_slider(AdwPreferencesGroup *group, const char *title,
                                 double min, double max, double *out,
                                 double value, KeyDialog *d)
{
    AdwActionRow *row = as_ui_slider_row(title, min, max, 0.05, value, out,
                                         on_slider_changed, d);

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

/* ---------------------------- switch demo ------------------------------ */

static double demo_travel(const KeyDialog *d)
{
    return d->switch_bottom > 0.1 ? d->switch_bottom : 3.4;
}

/* The depth comes straight from the analog report of the keyboard, so the cap
 * sits exactly where the physical switch is. */
static double demo_depth(const KeyDialog *d)
{
    if (!d->demo_active || d->demo_key < 0) {
        return 0.0;
    }
    return as_store_travel(d->store, d->path, d->demo_key) / 1000.0;
}

static GtkWidget *button_for_code(KeyDialog *d, unsigned code)
{
    if (!code) {
        return NULL;
    }
    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        if (keyboxes[i].code == code) {
            return d->buttons[i];
        }
    }
    return NULL;
}

static gboolean on_test_pressed(GtkEventControllerKey *controller,
                                guint keyval, guint keycode,
                                GdkModifierType state, gpointer data)
{
    KeyDialog *d = data;
    unsigned code = as_ui_hid_from_keyval(keyval);
    GtkWidget *button = button_for_code(d, code);

    (void)controller;
    (void)keycode;
    (void)state;
    if (button) {
        gtk_widget_add_css_class(button, "pressed");
    }
    for (size_t i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        if (keyboxes[i].code == code) {
            d->demo_key = keyboxes[i].pos;
            break;
        }
    }
    gtk_widget_queue_draw(d->demo_area);
    return FALSE;
}

static void on_test_released(GtkEventControllerKey *controller, guint keyval,
                             guint keycode, GdkModifierType state,
                             gpointer data)
{
    KeyDialog *d = data;
    GtkWidget *button = button_for_code(d, as_ui_hid_from_keyval(keyval));

    (void)controller;
    (void)keycode;
    (void)state;
    if (button) {
        gtk_widget_remove_css_class(button, "pressed");
    }
    gtk_widget_queue_draw(d->demo_area);
}

static void demo_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data)
{
    KeyDialog *d = data;
    double travel = demo_travel(d);
    double depth = MIN(demo_depth(d), travel);
    double top = 22.0, bottom = height - 22.0, span = bottom - top;
    double act = MIN(d->act, travel), rel = MIN(d->rel, travel);
    double y_act = top + act / travel * span;
    double y_rel = top + rel / travel * span;
    double y_now = top + depth / travel * span;
    double housing_x = 46.0, housing_w = 74.0;
    char buf[96];

    (void)area;
    (void)width;
    cairo_set_source_rgb(cr, 0.20, 0.20, 0.22);
    cairo_rectangle(cr, housing_x, top - 8, housing_w, span + 16);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.20, 0.60, 1.0, 0.30);
    cairo_rectangle(cr, housing_x, top, housing_w, y_act - top);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.16, 0.65, 0.40, 0.35);
    cairo_rectangle(cr, housing_x, y_act, housing_w, bottom - y_act);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.95, 0.65, 0.10, 0.40);
    cairo_rectangle(cr, housing_x, y_rel, housing_w,
                    MAX(1.0, y_act - y_rel));
    cairo_fill(cr);

    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgb(cr, 0.20, 0.60, 1.0);
    cairo_move_to(cr, housing_x - 10, y_act);
    cairo_line_to(cr, housing_x + housing_w + 10, y_act);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.20, 0.85, 0.45);
    cairo_move_to(cr, housing_x - 10, y_rel);
    cairo_line_to(cr, housing_x + housing_w + 10, y_rel);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.35, 0.38, 0.45);
    cairo_rectangle(cr, housing_x + 4, y_now - 11, housing_w - 8, 22);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.85, 0.87, 0.92);
    cairo_rectangle(cr, housing_x + 14, y_now - 5, housing_w - 28, 3);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14.0);
    snprintf(buf, sizeof(buf), "%.2f мм из %.2f", depth, travel);
    cairo_move_to(cr, housing_x + housing_w + 24, top + 4);
    cairo_show_text(cr, buf);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);
    snprintf(buf, sizeof(buf), "Срабатывание: %.2f мм", d->act);
    cairo_set_source_rgb(cr, 0.55, 0.75, 1.0);
    cairo_move_to(cr, housing_x + housing_w + 24, top + 30);
    cairo_show_text(cr, buf);

    snprintf(buf, sizeof(buf), "Отпускание: %.2f мм", d->rel);
    cairo_set_source_rgb(cr, 0.55, 0.9, 0.65);
    cairo_move_to(cr, housing_x + housing_w + 24, top + 50);
    cairo_show_text(cr, buf);

    if (depth >= act) {
        snprintf(buf, sizeof(buf), "зона: сработала");
    } else if (depth <= rel) {
        snprintf(buf, sizeof(buf), "зона: отпущена");
    } else {
        snprintf(buf, sizeof(buf), "зона: до срабатывания");
    }
    cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
    cairo_move_to(cr, housing_x + housing_w + 24, top + 70);
    cairo_show_text(cr, buf);
}

static void on_analog(AsStore *store, gpointer data)
{
    GtkWidget *area = data;

    (void)store;
    gtk_widget_queue_draw(area);
}

static void dialog_free(gpointer data)
{
    KeyDialog *d = data;

    if (d->refresh_id) {
        g_source_remove(d->refresh_id);
    }
    /* The store outlives this dialog, so a handler left behind would read
     * freed memory on the next refresh. */
    g_signal_handlers_disconnect_by_data(d->store, d);
    if (d->demo_active) {
        as_store_analog_stop(d->store);
    }
    g_free(d);
}

static GtkWidget *make_legend(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget *rt = gtk_label_new(NULL);
    GtkWidget *snap = gtk_label_new(NULL);
    GtkWidget *changed = gtk_label_new(NULL);

    gtk_label_set_markup(GTK_LABEL(rt),
                         "<span foreground='#2ec27e'>●</span> Rapid Trigger");
    gtk_label_set_markup(GTK_LABEL(snap),
                         "<span foreground='#e66100'>▣</span> Snap Tap");
    gtk_label_set_markup(GTK_LABEL(changed),
                         "<span foreground='#3584e4'><b>●</b></span> "
                         "отдельный ход");
    gtk_box_append(GTK_BOX(box), rt);
    gtk_box_append(GTK_BOX(box), snap);
    gtk_box_append(GTK_BOX(box), changed);
    return box;
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
        "  font-size: 11px; border-radius: 5px; }"
        "button.keycap.rt { background-color: rgba(46,194,126,0.30); }"
        "button.keycap.changed { font-weight: bold;"
        "  box-shadow: inset 0 0 0 2px rgba(53,132,228,0.55); }"
        "button.keycap.snap { border: 2px solid #e66100; }"
        "button.keycap.pressed { background-color: rgba(53,132,228,0.75);"
        "  color: #ffffff; }");
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
    gtk_window_set_resizable(d->window, TRUE);
    gtk_window_set_transient_for(d->window, parent);
    gtk_window_set_modal(d->window, TRUE);
    gtk_window_set_default_size(d->window, 920, 720);

    gtk_widget_set_margin_top(outer, 18);
    gtk_widget_set_margin_bottom(outer, 18);
    gtk_widget_set_margin_start(outer, 18);
    gtk_widget_set_margin_end(outer, 18);
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(description), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(GTK_LABEL(description), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(description), PANGO_ELLIPSIZE_NONE);
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
        gtk_widget_set_focusable(button, FALSE);
        gtk_widget_set_tooltip_text(button, label);
        gtk_fixed_put(GTK_FIXED(fixed), button, keyboxes[i].x,
                      keyboxes[i].y);
        g_object_set_data(G_OBJECT(button), "as-pos",
                          GINT_TO_POINTER(keyboxes[i].pos));
        g_signal_connect(button, "toggled", G_CALLBACK(on_key_toggled), d);
        d->buttons[i] = button;
    }
    gtk_box_append(GTK_BOX(outer), fixed);
    gtk_box_append(GTK_BOX(outer), make_legend());
    {
        GtkWidget *hint = gtk_label_new(
            "Нажмите клавиши на клавиатуре: подсветка покажет, что нажатие "
            "доходит, а цвета — какие настройки уже сохранены.");

        gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(hint), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
        gtk_box_append(GTK_BOX(outer), hint);
    }

    {
        static const char *const group_names[] = { "Все", "Буквы", "Цифры",
                                                   "Спец. клавиши" };
        static const char *const group_hints[] = {
            "Выбрать все клавиши; повторное нажатие снимет выбор",
            "Выбрать все буквы; повторное нажатие снимет выбор",
            "Выбрать все цифры; повторное нажатие снимет выбор",
            "Модификаторы, Ctrl, Alt, Shift, Win; повторное нажатие снимает",
        };

        for (int group = 0; group < 4; group++) {
            GtkWidget *button = gtk_toggle_button_new_with_label(
                group_names[group]);

            g_object_set_data(G_OBJECT(button), "as-group",
                              GINT_TO_POINTER(group));
            gtk_widget_set_tooltip_text(button, group_hints[group]);
            g_signal_connect(button, "toggled", G_CALLBACK(on_group_toggled),
                             d);
            d->group_buttons[group] = button;
            gtk_box_append(GTK_BOX(select), button);
        }
        gtk_box_append(GTK_BOX(select),
                       make_button("Снять", G_CALLBACK(on_none), d));
    }
    gtk_box_append(GTK_BOX(outer), select);

    {
        GtkStringList *types = gtk_string_list_new(NULL);

        for (size_t i = 0; i < G_N_ELEMENTS(switch_presets); i++) {
            char label[64];

            snprintf(label, sizeof(label), "%s (%.1f мм)",
                     switch_presets[i].name, switch_presets[i].bottom_out);
            gtk_string_list_append(types, label);
        }
        d->switch_row = as_ui_combo("Тип свитчей", G_LIST_MODEL(types));
        d->switch_bottom = switch_presets[0].bottom_out;
        gtk_widget_set_tooltip_text(
            GTK_WIDGET(d->switch_row),
            "Ход свитча нужен только для демо-режима; прошивка хранит "
            "один профиль на все клавиши");
        g_signal_connect(d->switch_row, "notify::selected",
                         G_CALLBACK(on_switch_type), d);
        adw_preferences_group_add(group, GTK_WIDGET(d->switch_row));
    }
    d->actuation = make_slider(group, "Точка срабатывания", 0.0,
                               KEY_MAX_TRIGGER, &d->act, AS_KB_DEFAULT_ACTUATION, d);
    d->release = make_slider(group, "Точка отпускания", 0.0, KEY_MAX_TRIGGER,
                             &d->rel, AS_KB_DEFAULT_ACTUATION, d);
    d->rt_press = make_slider(group, "Rapid Trigger: нажатие", 0.0,
                              KEY_MAX_TRIGGER, &d->rtp, AS_KB_DEFAULT_RT, d);
    d->rt_release = make_slider(group, "Rapid Trigger: отпускание", 0.0,
                                KEY_MAX_TRIGGER, &d->rtr, AS_KB_DEFAULT_RT, d);
    d->deadzone = make_slider(group, "Мёртвая зона", 0.0, 1.0, &d->dz, 0.3, d);
    d->rt = as_ui_switch_row("Rapid Trigger", NULL);
    g_signal_connect(d->rt, "notify::active", G_CALLBACK(on_rt_toggle), d);
    gtk_widget_set_tooltip_text(GTK_WIDGET(d->rt),
                                "Rapid Trigger: клавиша отпускается сразу, "
                                "как только ход уменьшился на заданное значение");
    adw_preferences_group_add(group, GTK_WIDGET(d->rt));
    gtk_box_append(GTK_BOX(outer), GTK_WIDGET(group));

    {
        AdwPreferencesGroup *demo = as_ui_group(
            "Проверка нажатия",
            "Включите демо-свич и нажмите клавишу на клавиатуре: ползунок "
            "покажет, в какой зоне находится ход.");

        d->demo_switch = as_ui_switch_row(
            "Демо-свич", "Рисует ход выбранного типа свитча");
        g_signal_connect(d->demo_switch, "notify::active",
                         G_CALLBACK(on_demo_toggle), d);
        adw_preferences_group_add(demo, GTK_WIDGET(d->demo_switch));
        d->demo_area = gtk_drawing_area_new();
        gtk_widget_set_size_request(d->demo_area, -1, 150);
        gtk_widget_set_visible(d->demo_area, FALSE);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(d->demo_area),
                                       demo_draw, d, NULL);
        g_signal_connect_object(store, "analog", G_CALLBACK(on_analog),
                                d->demo_area, 0);
        adw_preferences_group_add(demo, d->demo_area);
        {
            GtkWidget *zone_legend = gtk_label_new(NULL);
            char *markup =
                g_strdup_printf(
                    "<span foreground='#3584e4'>▬</span> до срабатывания   "
                    "<span foreground='#2ec27e'>▬</span> нажата   "
                    "<span foreground='#e66100'>▬</span> отпускание");

            gtk_label_set_markup(GTK_LABEL(zone_legend), markup);
            gtk_label_set_wrap(GTK_LABEL(zone_legend), TRUE);
            g_free(markup);
            adw_preferences_group_add(demo, zone_legend);
        }
        gtk_box_append(GTK_BOX(outer), GTK_WIDGET(demo));
    }

    d->status = gtk_label_new("Выбрано клавиш: 0");
    gtk_label_set_xalign(GTK_LABEL(d->status), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(d->status), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(outer), d->status);

    d->apply = gtk_button_new_with_label("Применить к выбранным");
    gtk_widget_add_css_class(d->apply, "suggested-action");
    gtk_widget_set_tooltip_text(d->apply,
                                "Записать выбранный ход во все отмеченные "
                                "клавиши");
    gtk_widget_set_sensitive(d->apply, FALSE);
    g_signal_connect(d->apply, "clicked", G_CALLBACK(on_apply), d);
    gtk_box_append(GTK_BOX(actions), d->apply);
    gtk_box_append(GTK_BOX(actions),
                   make_button("Закрыть", G_CALLBACK(on_close), window));
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(outer), actions);

    {
        GtkEventController *keys = gtk_event_controller_key_new();

        gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
        g_signal_connect(keys, "key-pressed", G_CALLBACK(on_test_pressed), d);
        g_signal_connect(keys, "key-released", G_CALLBACK(on_test_released), d);
        gtk_widget_add_controller(window, keys);
    }
    {
        GtkWidget *scroll = gtk_scrolled_window_new();

        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), outer);
        gtk_window_set_child(d->window, scroll);
    }
    /* The page may still be waiting for the first scan; read the values now
     * and again once the store has them, and follow later writes the same
     * way so the sliders never show stale numbers. */
    g_signal_connect(store, "changed", G_CALLBACK(on_store_changed), d);
    g_signal_connect(store, "error", G_CALLBACK(on_store_error), d);
    g_signal_connect(store, "operation-completed",
                     G_CALLBACK(on_operation_completed), d);
    d->demo_key = keyboxes[0].pos;
    d->updating = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->buttons[0]), TRUE);
    d->updating = FALSE;
    sync_selection(d);
    gtk_window_present(d->window);
    d->refresh_id = g_timeout_add(500, refresh_later, d);
}