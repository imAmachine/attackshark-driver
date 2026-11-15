/* SPDX-License-Identifier: MIT */
#ifndef AS_KEY_DIALOG_H
#define AS_KEY_DIALOG_H

#include <adwaita.h>

#include "as-store.h"

G_BEGIN_DECLS

/* Opens the modal keyboard layout. Any set of keys can be selected, and one
 * travel setting is applied to all of them in a single write. */
void as_key_dialog_open(GtkWindow *parent, AsStore *store, const char *path);

G_END_DECLS

#endif