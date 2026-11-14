/* SPDX-License-Identifier: MIT
 *
 * A single background thread that owns every device handle. The driver talks
 * to hidraw with blocking ioctls and installs a SIGALRM handler, so all device
 * work has to be serialized anyway: one worker keeps that guarantee and keeps
 * the UI thread free at the same time.
 */
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
/* Number of jobs queued or running. */
guint as_worker_pending(void);

void as_job_run(AsJobFunc func, gpointer data, AsJobFreeFunc free_data,
                AsJobDoneFunc done, gpointer user_data,
                AsJobFreeFunc free_user_data);

#endif
