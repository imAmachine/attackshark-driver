#include <glib.h>
#include "as-worker.h"

typedef struct {
    GMutex mutex;
    GCond cond;
    gboolean entered;
    gboolean release;
    gint order;
    gint completed;
    GThread *main_thread;
} TestState;

static void blocked_job(gpointer data, GError **error)
{
    TestState *state = data;

    (void)error;
    g_mutex_lock(&state->mutex);
    state->entered = TRUE;
    g_cond_broadcast(&state->cond);
    while (!state->release) {
        g_cond_wait(&state->cond, &state->mutex);
    }
    g_mutex_unlock(&state->mutex);
    g_assert_cmpint(g_atomic_int_add(&state->order, 1), ==, 0);
}

static void next_job(gpointer data, GError **error)
{
    TestState *state = data;

    (void)error;
    g_assert_cmpint(g_atomic_int_add(&state->order, 1), ==, 1);
}

static void quick_job(gpointer data, GError **error)
{
    (void)data;
    (void)error;
}

static void done(gpointer data, GError *error, gpointer user_data)
{
    TestState *state = data;

    (void)user_data;
    g_assert_no_error(error);
    g_assert_true(g_thread_self() == state->main_thread);
    state->completed++;
}

static void test_device_isolation(void)
{
    TestState state = { .main_thread = g_thread_self() };
    gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND * 5;
    gboolean entered;

    as_worker_start();
    as_worker_start();
    as_job_run_for_device("mouse", blocked_job, &state, NULL, done, NULL, NULL);
    g_mutex_lock(&state.mutex);
    while (!state.entered && g_cond_wait_until(&state.cond, &state.mutex,
                                               deadline)) {
    }
    entered = state.entered;
    g_mutex_unlock(&state.mutex);
    as_job_run_for_device("mouse", next_job, &state, NULL, done, NULL, NULL);
    as_job_run_for_device("keyboard", quick_job, &state, NULL, done, NULL, NULL);
    as_job_run(quick_job, &state, NULL, done, NULL, NULL);
    while (state.completed < 2 && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    gint independent = state.completed;
    gint order = g_atomic_int_get(&state.order);
    g_mutex_lock(&state.mutex);
    state.release = TRUE;
    g_cond_broadcast(&state.cond);
    g_mutex_unlock(&state.mutex);
    as_worker_stop();
    while (g_main_context_iteration(NULL, FALSE)) {
    }
    g_assert_true(entered);
    g_assert_cmpint(independent, ==, 2);
    g_assert_cmpint(order, ==, 0);
    g_assert_cmpint(state.completed, ==, 4);
    g_assert_cmpint(g_atomic_int_get(&state.order), ==, 2);
    as_worker_stop();
    g_cond_clear(&state.cond);
    g_mutex_clear(&state.mutex);
}

static void count_release(gpointer data)
{
    gint *released = data;

    (*released)++;
}

static void test_restart_and_release(void)
{
    gint released = 0;

    as_job_run(quick_job, &released, count_release, NULL, &released, count_release);
    g_assert_cmpint(released, ==, 2);
    as_worker_start();
    as_job_run(quick_job, &released, count_release, NULL, &released, count_release);
    as_worker_stop();
    while (g_main_context_iteration(NULL, FALSE)) {
    }
    g_assert_cmpint(released, ==, 4);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/worker/device-isolation", test_device_isolation);
    g_test_add_func("/worker/restart-and-release", test_restart_and_release);
    return g_test_run();
}
