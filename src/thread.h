/*
 * Threading functionality.
 *
 * This file is part of ANT (Ant is Not a Telephone)
 *
 * Copyright 2007 Ivan Schreter
 *
 * ANT is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * ANT is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ANT; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _ANT_THREAD_H
#define _ANT_THREAD_H

/* GTK */
#include <gtk/gtk.h>

/*!
 * @brief Structure of thread handle.
 */
typedef struct {
  GThread *thread;        /*!< thread handle */
  unsigned int stop_flag; /*!< flag to stop the thread */
} thread_t;

/*!
 * @brief Remote call function.
 *
 * @param context call context as sent via remote_call_invoke().
 * @param data call data as sent via remote_call_invoke().
 */
typedef void (*remote_call_fnc)(void *, void *);

/*!
 * @brief Port for remote calling.
 */
typedef struct {
  int fd[2];            /*!< pipe for passing remote call requests, poll on fd[0] */
  GCond *condition;     /*!< condition to signal after call complete, may be NULL */
  GMutex *mutex;        /*!< mutex protecting the condition */
  GThread *owner;       /*!< owning thread of this port */
  guint gtk_input_tag;  /*!< GTK input tag for selecting on the pipe */
} remote_call_port_t;

/*!
 * @brief Event descriptor communicated over pipe to worker thread.
 */
typedef struct __remote_call {
  remote_call_fnc fnc;      /*!< function to call remotely in main thread */
  remote_call_port_t *port; /*!< port for remote call functions */
  void *context;            /*!< context of remote call */
  void *data;               /*!< data of remote call */
} remote_call_t;


/*!
 * @brief Initialize new thread handle (constructor).
 *
 * @param thread thread handle to initialize.
 */
void thread_init(thread_t *thread);

/*!
 * @brief Check if thread is still running.
 *
 * @param thread thread handle to check.
 * @return zero, if not running, nonzero otherwise.
 */
int thread_is_running(thread_t *thread);

/*!
 * @brief Check if thread is currently stopping.
 *
 * @param thread thread handle to check.
 * @return zero, if not stopping, nonzero otherwise.
 */
int thread_is_stopping(thread_t *thread);

/*!
 * @brief Start a thread, if not running yet.
 *
 * @param thread thread handle.
 * @param handler thread main routine.
 * @param param parameter for the main routine.
 * @return zero, if thread started, 1 if already running, negative on error.
 */
int thread_start(thread_t *thread, gpointer (*handler)(gpointer), gpointer param);

/*!
 * @brief Stop a thread.
 *
 * @param thread thread handle.
 */
void thread_stop(thread_t *thread);



/*!
 * @brief Initialize remote call port.
 *
 * @param port port to initialize.
 * @return 0 on success, -1 on error.
 */
int remote_call_init(remote_call_port_t *port);

/*!
 * @brief Close remote call port.
 *
 * @param port port to close.
 * @return 0 on success, -1 on error.
 */
int remote_call_close(remote_call_port_t *port);

/*!
 * @brief Invoke remote call.
 *
 * @param port port on which to invoke call.
 * @param func function to call remotely.
 * @param context function's context.
 * @param data data passed in addition to context.
 * @return 0 on success, -1 on error.
 */
int remote_call_invoke(remote_call_port_t *port, remote_call_fnc func, void *context, void *data);

/*!
 * @brief Register remote call processing on remote call port in GTK.
 *
 * @param port port.
 * @return 0 on success, -1 on error.
 */
int remote_call_register(remote_call_port_t *port);

#endif  /* _ANT_THREAD_H */
