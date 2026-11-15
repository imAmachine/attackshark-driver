/* SPDX-License-Identifier: MIT */
#ifndef AS_UI_H
#define AS_UI_H

#include <adwaita.h>
#include <stdint.h>

#include "as-store.h"

G_BEGIN_DECLS

/* Polling rates the driver accepts, in the order they are shown. */
GtkStringList *as_ui_rate_list(void);
int as_ui_rate_to_index(int hz);
int as_ui_rate_from_index(guint index);

/* Light modes offered by the UI. "off" is left out on purpose: the firmware
 * ignores it, the backlight is switched by the master switch instead. `side`
 * selects the smaller list of the edge lighting, `modes` receives the wire
 * values so the combo index can be translated back. */
GtkStringList *as_ui_light_list_for(int side, int *modes, int max);
int as_ui_light_index(const int *modes, int count, int mode);

const char *as_ui_mag_mode_name(int mode);
int as_ui_mag_mode_count(void);

GdkRGBA as_ui_rgba_from_rgb(const uint8_t rgb[3]);
void as_ui_rgb_from_rgba(const GdkRGBA *rgba, uint8_t rgb[3]);
unsigned as_ui_snap_dpi(unsigned dpi);
/* HID usage of a GTK key value, 0 when the key has no simple mapping. Used
 * to find the same key on the layout the firmware addresses. */
unsigned as_ui_hid_from_keyval(guint keyval);

AdwActionRow *as_ui_value_row(const char *title);
/* Row with minus/plus buttons around a slider. The value is kept in out and
 * changed() is called on every user change. The row itself carries no stored
 * value, so call as_ui_slider_set() to show a reading from the device. */
AdwActionRow *as_ui_slider_row(const char *title, double min, double max,
                               double step, double value, double *out,
                               void (*changed)(gpointer), gpointer user_data);
void as_ui_slider_set(AdwActionRow *row, double value);
void as_ui_row_set_value(AdwActionRow *row, const char *value);
/* Sets a row subtitle from a printf format. */
void as_ui_row_format(AdwActionRow *row, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);
AdwSwitchRow *as_ui_switch_row(const char *title, const char *subtitle);
AdwPreferencesGroup *as_ui_group(const char *title, const char *description);
AdwComboRow *as_ui_combo(const char *title, GListModel *model);
AdwSpinRow *as_ui_spin(const char *title, double min, double max, double step,
                       const char *subtitle);

/* Setting a value programmatically must not loop back into a device write. */
void as_ui_combo_set(AdwComboRow *row, gboolean *guard, int index);
void as_ui_spin_set(AdwSpinRow *row, gboolean *guard, double value);
void as_ui_switch_set(AdwSwitchRow *row, gboolean *guard, gboolean active);
void as_ui_color_set(GtkColorDialogButton *button, gboolean *guard,
                     const uint8_t rgb[3]);

char *as_ui_mouse_summary(const AsDevice *dev);
char *as_ui_keyboard_summary(const AsDevice *dev);
/* One-line explanation for a page banner or a row. The permission problem can
 * be solved from inside the app, so it must not read like a dead end. */
const char *as_ui_error(const AsDevice *dev);

/* Device pages, one per family. */
GtkWidget *as_mouse_page_new(AsStore *store, const char *path);
GtkWidget *as_keyboard_page_new(AsStore *store, const char *path);

/* --------------------------- device pages --------------------------- */

/* State every device page shares: its snapshot source and the coalesced
 * write queue (a dragged slider must not send one EEPROM write per step). */
typedef struct {
    AsStore *store;
    char *path;
    AdwBanner *banner;
    gboolean updating;
    guint pending_id;
    AsOpRequest pending;
    gboolean pending_valid;
} AsPage;

/* Builds the page skeleton (banner + preferences page) and owns the state. */
void as_page_attach(AsPage *page, GtkWidget *box, AdwPreferencesPage *prefs,
                    AsStore *store, const char *path);
AsDevice *as_page_device(AsPage *page);
/* NULL hides the banner, any text reveals it. */
void as_page_banner(AsPage *page, const char *text);
void as_page_queue(AsPage *page, const AsOpRequest *req);
void as_page_flush(AsPage *page);
void as_page_free(AsPage *page);

G_END_DECLS

#endif
