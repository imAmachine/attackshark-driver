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

GtkStringList *as_ui_light_list(void);

const char *as_ui_mag_mode_name(int mode);
int as_ui_mag_mode_count(void);

GdkRGBA as_ui_rgba_from_rgb(const uint8_t rgb[3]);
void as_ui_rgb_from_rgba(const GdkRGBA *rgba, uint8_t rgb[3]);
unsigned as_ui_snap_dpi(unsigned dpi);

AdwActionRow *as_ui_value_row(const char *title);
void as_ui_row_set_value(AdwActionRow *row, const char *value);
AdwSwitchRow *as_ui_switch_row(const char *title, const char *subtitle);

/* Setting a value programmatically must not loop back into a device write. */
void as_ui_combo_set(AdwComboRow *row, gboolean *guard, int index);
void as_ui_spin_set(AdwSpinRow *row, gboolean *guard, double value);
void as_ui_switch_set(AdwSwitchRow *row, gboolean *guard, gboolean active);
void as_ui_color_set(GtkColorDialogButton *button, gboolean *guard,
                     const uint8_t rgb[3]);

char *as_ui_mouse_summary(const AsDevice *dev);
char *as_ui_keyboard_summary(const AsDevice *dev);

G_END_DECLS

#endif
