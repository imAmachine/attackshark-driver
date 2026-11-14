/* SPDX-License-Identifier: MIT */
#ifndef AS_KEYBOARD_PAGE_H
#define AS_KEYBOARD_PAGE_H

#include <adwaita.h>

#include "as-store.h"

G_BEGIN_DECLS

GtkWidget *as_keyboard_page_new(AsStore *store, const char *path);

G_END_DECLS

#endif
