#include "as-ui.h"
#include "as-key-dialog.h"

#include "../src/gui/as-keyboard-page.c"
#define apply_rate mouse_apply_rate
#define update_page mouse_update_page
#define on_store_changed mouse_on_store_changed
#define build_status mouse_build_status
#include "../src/gui/as-mouse-page.c"
#undef apply_rate
#undef update_page
#undef on_store_changed
#undef build_status

typedef struct { GObject parent; } MockStore;
typedef struct { GObjectClass parent; } MockStoreClass;
G_DEFINE_TYPE(MockStore, mock_store, G_TYPE_OBJECT)

static void mock_store_class_init(MockStoreClass *klass)
{
    g_signal_new("changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                 NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void mock_store_init(MockStore *store)
{
    (void)store;
}

static AsDevice device;
static AsOpRequest sent[32];
static int sent_count;
static gboolean have_display;

AsDevice *as_store_lookup(AsStore *store, const char *path)
{
    (void)store;
    (void)path;
    return &device;
}

gboolean as_store_apply(AsStore *store, const char *path, const AsOpRequest *req)
{
    (void)store;
    (void)path;
    g_assert_cmpint(sent_count, <, (int)G_N_ELEMENTS(sent));
    sent[sent_count++] = *req;
    return TRUE;
}

void as_key_dialog_open(GtkWindow *parent, AsStore *store, const char *path)
{
    (void)parent;
    (void)store;
    (void)path;
}

static void test_physical_keys(void)
{
    g_assert_cmpuint(as_ui_hid_from_key(GDK_KEY_Cyrillic_ef, 38), ==, 4);
    g_assert_cmpuint(as_ui_hid_from_key(GDK_KEY_exclam, 10), ==, 30);
    g_assert_cmpuint(as_ui_hid_from_key(GDK_KEY_colon, 47), ==, 51);
    g_assert_cmpuint(as_ui_hid_from_key(GDK_KEY_question, 61), ==, 56);
    g_assert_cmpuint(as_ui_hid_from_keycode(50), ==, 225);
    g_assert_cmpuint(as_ui_hid_from_keycode(104), ==, 88);
    g_assert_cmpuint(as_ui_hid_from_keycode(7), ==, 0);
    g_assert_cmpuint(as_ui_hid_from_keycode(9999), ==, 0);
    g_assert_cmpuint(as_ui_hid_from_key(GDK_KEY_a, 0), ==, 4);
    g_assert_cmpuint(as_ui_hid_from_key(GDK_KEY_Cyrillic_ef, 0), ==, 0);
}

static GtkWidget *keyboard_page(GObject **store)
{
    memset(&device, 0, sizeof(device));
    device.light_valid = 1;
    device.side_light_valid = 1;
    device.light_mode = 1;
    device.side_light_mode = 1;
    device.side_light_mode_saved = 1;
    device.mag_mode = 0;
    device.key_code_valid = 1;
    sent_count = 0;
    *store = g_object_new(mock_store_get_type(), NULL);
    return g_object_ref_sink(as_keyboard_page_new((AsStore *)*store, "/mock"));
}

static void test_light_pending(void)
{
    GObject *store;
    GtkWidget *box;
    AsKeyboardPage *p;
    AsOpRequest desired;
    GdkRGBA color = { 0.2, 0.4, 0.6, 1.0 };

    if (!have_display) {
        g_test_skip("No GTK display");
        return;
    }
    box = keyboard_page(&store);
    p = g_object_get_data(G_OBJECT(box), "as-page");
    adw_spin_row_set_value(p->row_side_bright, 3);
    adw_spin_row_set_value(p->row_side_speed, 2);
    adw_switch_row_set_active(p->sw_side_dazzle, TRUE);
    gtk_color_dialog_button_set_rgba(p->side_color, &color);
    desired = *pending_light(p, 1);
    update_page(p);
    g_assert_cmpfloat(adw_spin_row_get_value(p->row_side_bright), ==, 3);
    g_assert_cmpfloat(adw_spin_row_get_value(p->row_side_speed), ==, 2);
    g_assert_true(adw_switch_row_get_active(p->sw_side_dazzle));
    adw_switch_row_set_active(p->sw_side_light, FALSE);
    g_assert_cmpint(sent_count, ==, 2);
    g_assert_cmpint(sent[0].light_mode, ==, desired.light_mode);
    g_assert_cmpint(sent[1].light_mode, ==, 0);
    desired.light_mode = 0;
    g_assert_cmpmem(&sent[1], sizeof(desired), &desired, sizeof(desired));
    g_assert_cmpint(p->page.pending_count, ==, 0);
    g_assert_cmpuint(p->page.pending_id, ==, 0);
    adw_spin_row_set_value(p->row_bright, 4);
    update_page(p);
    g_assert_cmpfloat(adw_spin_row_get_value(p->row_bright), ==, 4);
    adw_switch_row_set_active(p->sw_light, FALSE);
    g_assert_cmpint(sent[2].op, ==, AS_OP_KB_LIGHT);
    g_assert_cmpint(sent[3].op, ==, AS_OP_KB_LIGHT_POWER);
    g_assert_cmpint(sent[3].light_on, ==, 1);
    g_object_unref(box);
    g_object_unref(store);
}

static void test_light_repeated_toggles(void)
{
    GObject *store;
    GtkWidget *box;
    AsKeyboardPage *p;

    if (!have_display) {
        g_test_skip("No GTK display");
        return;
    }
    box = keyboard_page(&store);
    p = g_object_get_data(G_OBJECT(box), "as-page");
    device.side_light_mode = 0;
    device.side_light_mode_saved = 20;
    device.side_light_brightness = 3;
    device.side_light_speed = 2;
    device.side_light_dazzle = 1;
    device.side_rgb[0] = 51;
    device.side_rgb[1] = 102;
    device.side_rgb[2] = 153;
    update_page(p);
    for (int i = 0; i < 6; i++) {
        gboolean active = i % 2 == 0;

        adw_switch_row_set_active(p->sw_side_light, active);
        g_assert_cmpint(sent_count, ==, i + 1);
        g_assert_cmpint(sent[i].op, ==, AS_OP_KB_SIDE_LIGHT);
        g_assert_cmpint(sent[i].light_mode, ==, active ? 20 : 0);
        g_assert_cmpint(sent[i].light_brightness, ==, 3);
        g_assert_cmpint(sent[i].light_speed, ==, 2);
        g_assert_cmpint(sent[i].light_dazzle, ==, 1);
        g_assert_cmpmem(sent[i].rgb, sizeof(sent[i].rgb),
                        device.side_rgb, sizeof(device.side_rgb));
        g_assert_cmpint(device.side_light_mode, ==, 0);
        g_assert_cmpint(device.side_light_mode_saved, ==, 20);
    }
    g_assert_cmpint(p->page.pending_count, ==, 0);
    g_object_unref(box);
    g_object_unref(store);
}

static void test_snap_selected_peer(void)
{
    GObject *store;
    GtkWidget *box;
    AsKeyboardPage *p;

    if (!have_display) {
        g_test_skip("No GTK display");
        return;
    }
    box = keyboard_page(&store);
    p = g_object_get_data(G_OBJECT(box), "as-page");
    device.key_option[1] = AS_KB_MODE_SNAP;
    device.key_option[2] = AS_KB_MODE_SNAP;
    device.key_pair[1] = 2;
    device.key_pair[2] = 1;
    p->snap_key1 = 1;
    p->snap_key2 = 3;
    update_page(p);
    g_assert_false(adw_switch_row_get_active(p->sw_snap));
    p->snap_key2 = 2;
    update_page(p);
    g_assert_true(adw_switch_row_get_active(p->sw_snap));
    g_assert_cmpint(sent_count, ==, 0);
    g_object_unref(box);
    g_object_unref(store);
}

static void test_dpi_steps(void)
{
    GObject *store;
    GtkWidget *box;
    AsMousePage *p;
    GtkAdjustment *adjustment;
    const unsigned values[] = { 30200, 30100, 30000, 29950 };

    if (!have_display) {
        g_test_skip("No GTK display");
        return;
    }
    memset(&device, 0, sizeof(device));
    device.dpi_stages = 1;
    device.dpi[0] = 30300;
    store = g_object_new(mock_store_get_type(), NULL);
    box = g_object_ref_sink(as_mouse_page_new((AsStore *)store, "/mock"));
    p = g_object_get_data(G_OBJECT(box), "as-page");
    adjustment = adw_spin_row_get_adjustment(p->row_dpi[0]);
    for (guint i = 0; i < G_N_ELEMENTS(values); i++) {
        adw_spin_row_set_value(p->row_dpi[0],
                              adw_spin_row_get_value(p->row_dpi[0]) -
                                  gtk_adjustment_get_step_increment(adjustment));
        g_assert_cmpfloat(adw_spin_row_get_value(p->row_dpi[0]), ==, values[i]);
        g_assert_cmpint(p->page.pending[0].value, ==, (int)values[i]);
    }
    for (int i = (int)G_N_ELEMENTS(values) - 2; i >= 0; i--) {
        adw_spin_row_set_value(p->row_dpi[0],
                              adw_spin_row_get_value(p->row_dpi[0]) +
                                  gtk_adjustment_get_step_increment(adjustment));
        g_assert_cmpfloat(adw_spin_row_get_value(p->row_dpi[0]), ==, values[i]);
    }
    sent_count = 0;
    g_object_unref(box);
    g_object_unref(store);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    have_display = gtk_init_check();
    if (have_display) {
        adw_init();
    }
    g_test_add_func("/pages/physical-keys", test_physical_keys);
    g_test_add_func("/pages/light-pending", test_light_pending);
    g_test_add_func("/pages/light-repeated-toggles", test_light_repeated_toggles);
    g_test_add_func("/pages/snap-selected-peer", test_snap_selected_peer);
    g_test_add_func("/pages/dpi-steps", test_dpi_steps);
    return g_test_run();
}
