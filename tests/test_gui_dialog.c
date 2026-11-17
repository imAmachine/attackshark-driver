#include <glib.h>
#include "as-store.h"
#include "../src/gui/as-ui.c"

static int sample_travel[AS_KB_KEY_MAX];

static int mock_travel(AsStore *store, const char *path, int key)
{
    (void)store;
    (void)path;
    return key >= 0 && key < AS_KB_KEY_MAX ? sample_travel[key] : 0;
}

#define as_store_travel mock_travel
#include "../src/gui/as-key-dialog.c"
#undef as_store_travel

static void test_read_validity(void)
{
    AsStore *store = as_store_get();
    AsDevice *dev = g_new0(AsDevice, 1);
    GtkWindow *window = NULL;
    KeyDialog *d = NULL;
    GListModel *windows;
    int key = keyboxes[0].pos;
    double initial;

    g_strlcpy(dev->path, "/nonexistent/dialog-regression", sizeof(dev->path));
    g_ptr_array_add(as_store_devices(store), dev);
    as_key_dialog_open(NULL, store, dev->path);
    windows = gtk_window_get_toplevels();
    for (guint i = 0; i < g_list_model_get_n_items(windows); i++) {
        GtkWindow *candidate = g_list_model_get_item(windows, i);

        d = g_object_get_data(G_OBJECT(candidate), "as-key-dialog");
        if (d) {
            window = candidate;
            break;
        }
        g_object_unref(candidate);
    }
    g_assert_nonnull(d);
    initial = d->act;
    g_assert_cmpfloat(initial, >, 0.0);
    g_assert_false(gtk_widget_get_sensitive(d->apply));
    on_apply(NULL, d);
    g_assert_cmpuint(d->pending_request, ==, 0);
    dev->key_travel_valid = TRUE;
    dev->key_actuation[key] = 1.25;
    on_store_changed(store, d);
    g_assert_cmpfloat(d->act, ==, 1.25);
    g_assert_true(gtk_widget_get_sensitive(d->apply));
    dev->key_travel_valid = FALSE;
    dev->key_actuation[key] = 0.0;
    on_store_changed(store, d);
    load_values(d);
    g_assert_cmpfloat(d->act, ==, 1.25);
    g_assert_false(gtk_widget_get_sensitive(d->apply));
    dev->key_travel_valid = TRUE;
    on_store_changed(store, d);
    as_ui_slider_set(d->actuation, 1.75);
    on_slider_changed(d);
    dev->key_travel_valid = FALSE;
    dev->key_actuation[key] = 0.0;
    on_store_changed(store, d);
    g_assert_cmpfloat(d->act, ==, 1.75);
    g_assert_false(gtk_widget_get_sensitive(d->apply));
    on_apply(NULL, d);
    g_assert_cmpuint(d->pending_request, ==, 0);
    d->pending_request = 42;
    d->edited_while_pending = FALSE;
    on_operation_completed(store, dev->path, AS_OP_KB_KEYS_TRAVEL, 42, TRUE, d);
    g_assert_true(d->edited);
    g_assert_cmpfloat(d->act, ==, 1.75);
    g_assert_false(gtk_widget_get_sensitive(d->apply));
    d->pending_request = 44;
    on_operation_completed(store, dev->path, AS_OP_KB_KEYS_TRAVEL, 44, FALSE, d);
    g_assert_true(d->edited);
    g_assert_false(gtk_widget_get_sensitive(d->apply));
    dev->key_travel_valid = TRUE;
    dev->key_actuation[key] = 0.5;
    on_store_changed(store, d);
    g_assert_true(gtk_widget_get_sensitive(d->apply));
    g_assert_cmpfloat(d->act, ==, 1.75);
    dev->permission_denied = TRUE;
    on_store_changed(store, d);
    g_assert_false(gtk_widget_get_sensitive(d->apply));
    dev->permission_denied = FALSE;
    d->pending_request = 43;
    gtk_label_set_text(GTK_LABEL(d->status), "write failed");
    on_operation_completed(store, dev->path, AS_OP_KB_KEYS_TRAVEL, 43, FALSE, d);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(d->status)), ==, "write failed");
    g_ptr_array_set_size(as_store_devices(store), 0);
    on_store_changed(store, d);
    g_assert_false(gtk_widget_get_sensitive(d->apply));
    on_apply(NULL, d);
    g_assert_cmpuint(d->pending_request, ==, 0);
    g_assert_cmpfloat(d->act, ==, 1.75);
    gtk_window_destroy(window);
    g_object_unref(window);
}

static void test_physical_key_feedback(void)
{
    KeyDialog d = { 0 };
    GtkWidget *q = gtk_button_new();
    GtkWidget *a = gtk_button_new();

    g_object_ref_sink(q);
    g_object_ref_sink(a);
    d.demo_area = gtk_drawing_area_new();
    g_object_ref_sink(d.demo_area);
    for (guint i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        if (keyboxes[i].code == 0x14) {
            d.buttons[i] = q;
        } else if (keyboxes[i].code == 0x04) {
            d.buttons[i] = a;
        }
    }
    on_test_pressed(NULL, GDK_KEY_a, 24, 0, &d);
    g_assert_true(gtk_widget_has_css_class(q, "pressed"));
    g_assert_false(gtk_widget_has_css_class(a, "pressed"));
    on_test_released(NULL, GDK_KEY_Cyrillic_shorti, 24, 0, &d);
    g_assert_false(gtk_widget_has_css_class(q, "pressed"));
    on_test_pressed(NULL, GDK_KEY_a, 0, 0, &d);
    g_assert_true(gtk_widget_has_css_class(a, "pressed"));
    on_test_released(NULL, GDK_KEY_a, 0, 0, &d);
    g_assert_false(gtk_widget_has_css_class(a, "pressed"));
    g_object_unref(q);
    g_object_unref(a);
    g_object_unref(d.demo_area);
}

static void test_slow_demo_tracking(void)
{
    KeyDialog d = { .demo_active = TRUE, .demo_key = keyboxes[0].pos,
                    .act = 1.0, .rel = 0.5, .rtp = 0.1,
                    .rtr = 0.1, .dz = 0.05 };
    int active = keyboxes[0].pos;
    int other = keyboxes[1].pos;

    d.demo_area = gtk_drawing_area_new();
    d.rt = ADW_SWITCH_ROW(adw_switch_row_new());
    g_object_ref_sink(d.demo_area);
    g_object_ref_sink(d.rt);
    for (guint index = 0; index < G_N_ELEMENTS(keyboxes); index++) {
        d.buttons[index] = gtk_button_new();
        g_object_ref_sink(d.buttons[index]);
    }
    for (int rapid = 0; rapid < 2; rapid++) {
        adw_switch_row_set_active(d.rt, rapid);
        memset(sample_travel, 0, sizeof(sample_travel));
        d.demo_key = active;
        d.demo_pressed = FALSE;
        d.demo_tracking = FALSE;
        for (int depth = 5; depth <= 2000; depth += 5) {
            sample_travel[active] = depth;
            on_analog(NULL, &d);
            g_assert_cmpint(d.demo_key, ==, active);
            g_assert_cmpfloat(demo_depth(&d), ==, depth / 1000.0);
        }
        sample_travel[other] = 1500;
        for (int depth = 1995; depth >= 100; depth -= 5) {
            sample_travel[active] = depth;
            on_analog(NULL, &d);
            g_assert_cmpint(d.demo_key, ==, active);
            g_assert_cmpfloat(demo_depth(&d), ==, depth / 1000.0);
        }
        g_assert_false(d.demo_pressed);
        for (int depth = 105; depth <= 2000; depth += 5) {
            sample_travel[active] = depth;
            on_analog(NULL, &d);
            g_assert_cmpint(d.demo_key, ==, active);
            g_assert_cmpfloat(demo_depth(&d), ==, depth / 1000.0);
        }
        g_assert_true(d.demo_pressed);
        sample_travel[active] = 0;
        sample_travel[other] = 500;
        on_analog(NULL, &d);
        g_assert_cmpint(d.demo_key, ==, other);
        g_assert_cmpfloat(demo_depth(&d), ==, 0.5);
    }
    for (guint index = 0; index < G_N_ELEMENTS(keyboxes); index++) {
        g_object_unref(d.buttons[index]);
    }
    g_object_unref(d.demo_area);
    g_object_unref(d.rt);
}

static void test_slider_precision(void)
{
    AsStore *store = as_store_get();
    AsDevice *dev = g_new0(AsDevice, 1);
    KeyDialog d = { .store = store, .updating = TRUE };
    const guint versions[] = { 767, 768, 1280 };
    const double steps[] = { 0.1, 0.01, 0.005 };

    g_strlcpy(dev->path, "/nonexistent/dialog-precision", sizeof(dev->path));
    g_strlcpy(d.path, dev->path, sizeof(d.path));
    dev->usb_version_valid = TRUE;
    g_ptr_array_add(as_store_devices(store), dev);
    for (guint i = 0; i < G_N_ELEMENTS(versions); i++) {
        AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
        AdwActionRow *row;
        SliderData *slider;

        g_object_ref_sink(group);
        dev->usb_version = versions[i];
        row = make_slider(group, "Travel", 0.0, 2.0, &d.act, 1.0, &d);
        slider = g_object_get_data(G_OBJECT(row), "as-slider");
        g_assert_cmpfloat(slider->step, ==, steps[i]);
        g_assert_cmpfloat(gtk_adjustment_get_step_increment(
            gtk_range_get_adjustment(GTK_RANGE(slider->scale))), ==, steps[i]);
        g_object_unref(group);
    }
    g_ptr_array_set_size(as_store_devices(store), 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (!gtk_init_check()) {
        g_print("GTK display unavailable\n");
        return 77;
    }
    g_test_add_func("/gui/dialog/read-validity", test_read_validity);
    g_test_add_func("/gui/dialog/slider-precision", test_slider_precision);
    g_test_add_func("/gui/dialog/physical-key-feedback", test_physical_key_feedback);
    g_test_add_func("/gui/dialog/slow-demo-tracking", test_slow_demo_tracking);
    return g_test_run();
}
