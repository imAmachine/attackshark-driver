/* SPDX-License-Identifier: MIT */
#ifndef AS_WORKER_H
#define AS_WORKER_H

#include <glib.h>

/* Runs on the worker thread. The job takes ownership of its own data. */
typedef void (*AsJobFunc)(gpointer data, GError **error);
/* Runs on the main thread once the job finished. */
typedef void (*AsJobDoneFunc)(gpointer data, GError *error, gpointer user_data);
typedef void (*AsJobFreeFunc)(gpointer data);

void as_worker_start(void);
void as_worker_stop(void);
void as_job_run_for_device(const char *path, AsJobFunc func, gpointer data,
                           AsJobFreeFunc free_data, AsJobDoneFunc done,
                           gpointer user_data, AsJobFreeFunc free_user_data);
void as_job_run(AsJobFunc func, gpointer data, AsJobFreeFunc free_data,
                AsJobDoneFunc done, gpointer user_data,
                AsJobFreeFunc free_user_data);

#endif
