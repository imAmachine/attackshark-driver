/* SPDX-License-Identifier: MIT */
#ifndef AS_MOUSE_PAGE_H
#define AS_MOUSE_PAGE_H

#include <adwaita.h>

#include "as-store.h"

G_BEGIN_DECLS

GtkWidget *as_mouse_page_new(AsStore *store, const char *path);

G_END_DECLS

#endif
