/* SPDX-License-Identifier: MIT
 *
 * One background thread owns every device handle. The driver talks to hidraw
 * with blocking ioctls and installs a SIGALRM timeout handler, so device work
 * has to be serialized anyway; a single worker keeps that guarantee while
 * keeping the GTK main loop responsive.
 */
#include "as-worker.h"

typedef struct {
    AsJobFunc func;
    gpointer data;
    AsJobFreeFunc free_data;
    AsJobDoneFunc done;
    gpointer user_data;
    AsJobFreeFunc free_user_data;
    GError *error;
} AsJob;

static GAsyncQueue *queue;
static GThread *thread;

/* g_async_queue_push() refuses NULL, so the stop request is a pointer that
 * can never be a valid job. */
#define AS_JOB_QUIT ((gpointer)1)

static void job_release(AsJob *job)
{
    if (job->error) {
        g_error_free(job->error);
    }
    if (job->free_data) {
        job->free_data(job->data);
    }
    if (job->free_user_data) {
        job->free_user_data(job->user_data);
    }
    g_free(job);
}

static gboolean job_finish(gpointer data)
{
    AsJob *job = data;

    if (job->done) {
        job->done(job->data, job->error, job->user_data);
    }
    job_release(job);
    return G_SOURCE_REMOVE;
}

static gpointer worker_main(gpointer unused)
{
    (void)unused;

    for (;;) {
        AsJob *job = g_async_queue_pop(queue);

        if (job == AS_JOB_QUIT) {
            break;
        }
        job->func(job->data, &job->error);
        g_main_context_invoke(NULL, job_finish, job);
    }
    return NULL;
}

void as_worker_start(void)
{
    if (thread) {
        return;
    }
    queue = g_async_queue_new();
    thread = g_thread_new("attackshark-io", worker_main, NULL);
}

void as_worker_stop(void)
{
    AsJob *job;

    if (!thread) {
        return;
    }
    g_async_queue_push(queue, AS_JOB_QUIT);
    g_thread_join(thread);
    thread = NULL;

    while ((job = g_async_queue_try_pop(queue)) != NULL) {
        job_release(job);
    }
    g_async_queue_unref(queue);
    queue = NULL;
}

void as_job_run(AsJobFunc func, gpointer data, AsJobFreeFunc free_data,
                AsJobDoneFunc done, gpointer user_data,
                AsJobFreeFunc free_user_data)
{
    AsJob *job = g_new0(AsJob, 1);

    g_return_if_fail(queue != NULL);

    job->func = func;
    job->data = data;
    job->free_data = free_data;
    job->done = done;
    job->user_data = user_data;
    job->free_user_data = free_user_data;
    g_async_queue_push(queue, job);
}
