/* SPDX-License-Identifier: MIT */
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

typedef struct {
    GAsyncQueue *queue;
    GThread *thread;
} AsWorker;

static GHashTable *workers;
static GMutex workers_lock;
static gboolean stopping_workers;

#define AS_JOB_QUIT ((gpointer)1)

static void job_release(gpointer data)
{
    AsJob *job = data;

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
    return G_SOURCE_REMOVE;
}

static gpointer worker_main(gpointer data)
{
    AsWorker *worker = data;

    for (;;) {
        AsJob *job = g_async_queue_pop(worker->queue);

        if (job == AS_JOB_QUIT) {
            break;
        }
        job->func(job->data, &job->error);
        g_idle_add_full(G_PRIORITY_DEFAULT, job_finish, job, job_release);
    }
    return NULL;
}

void as_worker_start(void)
{
    g_mutex_lock(&workers_lock);
    if (!workers && !stopping_workers) {
        workers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    }
    g_mutex_unlock(&workers_lock);
}

void as_worker_stop(void)
{
    GHashTable *stopping;
    GHashTableIter iter;
    gpointer value;

    g_mutex_lock(&workers_lock);
    stopping = workers;
    if (stopping) {
        stopping_workers = TRUE;
    }
    workers = NULL;
    g_mutex_unlock(&workers_lock);
    if (!stopping) {
        return;
    }
    g_hash_table_iter_init(&iter, stopping);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        AsWorker *worker = value;

        g_async_queue_push(worker->queue, AS_JOB_QUIT);
    }
    g_hash_table_iter_init(&iter, stopping);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        AsWorker *worker = value;

        g_thread_join(worker->thread);
        g_async_queue_unref(worker->queue);
    }
    g_hash_table_destroy(stopping);
    g_mutex_lock(&workers_lock);
    stopping_workers = FALSE;
    g_mutex_unlock(&workers_lock);
}

void as_job_run_for_device(const char *path, AsJobFunc func, gpointer data,
                           AsJobFreeFunc free_data, AsJobDoneFunc done,
                           gpointer user_data, AsJobFreeFunc free_user_data)
{
    AsJob *job;
    AsWorker *worker;
    const char *key = path ? path : "";

    g_mutex_lock(&workers_lock);
    if (!workers) {
        g_mutex_unlock(&workers_lock);
        if (free_data) {
            free_data(data);
        }
        if (free_user_data) {
            free_user_data(user_data);
        }
        return;
    }
    worker = g_hash_table_lookup(workers, key);
    if (!worker) {
        worker = g_new0(AsWorker, 1);
        worker->queue = g_async_queue_new();
        worker->thread = g_thread_new("attackshark-io", worker_main, worker);
        g_hash_table_insert(workers, g_strdup(key), worker);
    }
    job = g_new0(AsJob, 1);
    job->func = func;
    job->data = data;
    job->free_data = free_data;
    job->done = done;
    job->user_data = user_data;
    job->free_user_data = free_user_data;
    g_async_queue_push(worker->queue, job);
    g_mutex_unlock(&workers_lock);
}

void as_job_run(AsJobFunc func, gpointer data, AsJobFreeFunc free_data,
                AsJobDoneFunc done, gpointer user_data,
                AsJobFreeFunc free_user_data)
{
    as_job_run_for_device(NULL, func, data, free_data, done, user_data,
                          free_user_data);
}
