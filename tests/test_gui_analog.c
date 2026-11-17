#define _GNU_SOURCE
#include <glib.h>
#include <adwaita.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/gui/as-store.c"
#include "../src/gui/as-ui.c"
#include "../src/gui/as-key-dialog.c"

static int observer_calls;
static double observer_depth[32];
static gboolean observer_pressed[32];
static gboolean observer_tracking[32];
static int observer_key[32];

static void observe_analog(AsStore *store, gpointer data)
{
    KeyDialog *dialog = data;

    int index = observer_calls++;

    g_assert_cmpint(index, <, 32);
    observer_depth[index] = as_store_travel(store, dialog->path, dialog->demo_key) / 1000.0;
    observer_pressed[index] = dialog->demo_pressed;
    observer_tracking[index] = dialog->demo_tracking;
    observer_key[index] = dialog->demo_key;
    g_test_message("depth %.3f pressed %s", observer_depth[index],
                   observer_pressed[index] ? "true" : "false");
}

static void send_report(int fd, int key, int raw)
{
    uint8_t report[5] = { 0x05, 0x1b, raw & 0xff, (raw >> 8) & 0xff, key };

    g_assert_cmpint(send(fd, report, sizeof(report), 0), ==, (ssize_t)sizeof(report));
}

static void tick_report(AsStore *store, int fd, int key, int raw)
{
    send_report(fd, key, raw);
    analog_tick(store);
}

static void test_real_analog_dialog(void)
{
    AsStore *store = as_store_get();
    KeyDialog dialog = { 0 };
    int sockets[2];
    int active = keyboxes[0].pos;
    int other = keyboxes[1].pos;
    int flags;

    g_assert_cmpint(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets), ==, 0);
    flags = fcntl(sockets[1], F_GETFL, 0);
    g_assert_cmpint(fcntl(sockets[1], F_SETFL, flags | O_NONBLOCK), ==, 0);

    dialog.store = store;
    g_strlcpy(dialog.path, "/mock/analog", sizeof(dialog.path));
    dialog.demo_active = TRUE;
    dialog.demo_key = active;
    dialog.act = 1.0;
    dialog.rel = 0.5;
    dialog.rtp = 0.1;
    dialog.rtr = 0.1;
    dialog.dz = 0.05;
    dialog.demo_area = gtk_drawing_area_new();
    dialog.rt = ADW_SWITCH_ROW(adw_switch_row_new());
    g_object_ref_sink(dialog.demo_area);
    g_object_ref_sink(dialog.rt);
    for (guint i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        dialog.buttons[i] = gtk_button_new();
        g_object_ref_sink(dialog.buttons[i]);
    }

    store->analog_running = TRUE;
    g_strlcpy(store->analog_path, dialog.path, sizeof(store->analog_path));
    store->analog_fd = sockets[1];
    g_signal_connect(store, "analog", G_CALLBACK(on_analog), &dialog);
    g_signal_connect(store, "analog", G_CALLBACK(observe_analog), &dialog);

    tick_report(store, sockets[0], active, 160);
    g_assert_false(dialog.demo_pressed);
    observer_calls = 0;
    send_report(sockets[0], active, 240);
    send_report(sockets[0], active, 160);
    g_assert_true(analog_tick(store));
    g_assert_cmpint(observer_calls, ==, 2);
    g_assert_cmpfloat(observer_depth[0], ==, 1.2);
    g_assert_cmpfloat(observer_depth[1], ==, 0.8);
    g_assert_true(observer_pressed[0]);
    g_assert_true(observer_pressed[1]);
    g_assert_true(dialog.demo_pressed);
    g_assert_true(gtk_widget_has_css_class(dialog.buttons[0], "pressed"));
    g_assert_cmpint(as_store_travel(store, dialog.path, active), ==, 800);

    adw_switch_row_set_active(dialog.rt, TRUE);
    tick_report(store, sockets[0], active, 0);
    tick_report(store, sockets[0], active, 300);
    g_assert_true(dialog.demo_pressed);
    observer_calls = 0;
    send_report(sockets[0], active, 400);
    send_report(sockets[0], active, 370);
    g_assert_true(analog_tick(store));
    g_assert_cmpint(observer_calls, ==, 2);
    g_assert_cmpfloat(observer_depth[0], ==, 2.0);
    g_assert_cmpfloat(observer_depth[1], ==, 1.85);
    g_assert_true(observer_pressed[0]);
    g_assert_false(observer_pressed[1]);
    g_assert_false(dialog.demo_pressed);
    g_assert_true(dialog.demo_tracking);

    observer_calls = 0;
    send_report(sockets[0], active, 0);
    send_report(sockets[0], active, 160);
    send_report(sockets[0], active, 300);
    g_assert_true(analog_tick(store));
    g_assert_cmpint(observer_calls, ==, 3);
    g_assert_cmpfloat(observer_depth[0], ==, 0.0);
    g_assert_false(observer_pressed[0]);
    g_assert_false(observer_tracking[0]);
    g_assert_cmpfloat(observer_depth[1], ==, 0.8);
    g_assert_false(observer_pressed[1]);
    g_assert_false(observer_tracking[1]);
    g_assert_true(observer_pressed[2]);
    g_assert_true(observer_tracking[2]);
    g_assert_cmpfloat(dialog.demo_extreme, ==, 1.5);

    adw_switch_row_set_active(dialog.rt, FALSE);
    observer_calls = 0;
    send_report(sockets[0], other, 240);
    send_report(sockets[0], other, 160);
    send_report(sockets[0], other, 0);
    g_assert_true(analog_tick(store));
    g_assert_cmpint(observer_calls, ==, 3);
    for (int i = 0; i < observer_calls; i++) {
        g_assert_cmpint(observer_key[i], ==, other);
    }
    g_assert_true(observer_pressed[0]);
    g_assert_true(observer_pressed[1]);
    g_assert_false(observer_pressed[2]);
    g_assert_cmpint(dialog.demo_key, ==, other);
    g_assert_cmpint(as_store_travel(store, dialog.path, active), ==, 1500);
    g_assert_true(gtk_widget_has_css_class(dialog.buttons[0], "pressed"));
    g_assert_false(gtk_widget_has_css_class(dialog.buttons[1], "pressed"));
    observer_calls = 0;
    g_assert_true(analog_tick(store));
    g_assert_cmpint(observer_calls, ==, 0);
    tick_report(store, sockets[0], active, 301);
    g_assert_cmpint(dialog.demo_key, ==, active);
    g_assert_true(dialog.demo_pressed);
    g_signal_handlers_disconnect_by_data(store, &dialog);

    close(sockets[0]);
    close(sockets[1]);
    store->analog_fd = -1;
    store->analog_running = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(keyboxes); i++) {
        g_object_unref(dialog.buttons[i]);
    }
    g_object_unref(dialog.demo_area);
    g_object_unref(dialog.rt);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (!gtk_init_check()) {
        g_print("GTK display unavailable\n");
        return 77;
    }
    g_test_add_func("/gui/analog/real-parser-dialog", test_real_analog_dialog);
    return g_test_run();
}
