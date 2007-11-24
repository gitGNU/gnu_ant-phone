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

#include "config.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "thread.h"
#include "globals.h"

/*--------------------------------------------------------------------------*/

/*!
 * @brief Handle one remote event.
 *
 * @param port remote call port.
 * @param event event to handle.
 * @return 0 on success, -1 otherwise.
 */
static int remote_call_process(remote_call_port_t *port, remote_call_t *event);

/*!
 * @brief Handle event on communication pipe.
 *
 * @param data remote event port to service.
 */
static void handle_remote_call(gpointer data,
                               gint fd _U_,
                               GdkInputCondition condition _U_);

/*--------------------------------------------------------------------------*/

void thread_init(thread_t *thread)
{
  /* make sure threading is initialized */
  if (!g_thread_supported ())
    g_thread_init (NULL);

  thread->thread = NULL;
  thread->stop_flag = 0;
}

/*--------------------------------------------------------------------------*/

int thread_is_running(thread_t *thread)
{
  return thread->thread != 0;
}

/*--------------------------------------------------------------------------*/

int thread_is_stopping(thread_t *thread)
{
  return thread->stop_flag;
}

/*--------------------------------------------------------------------------*/

int thread_start(thread_t *thread, gpointer (*handler)(gpointer), gpointer param)
{
  /* make sure threading is initialized */
  if (!g_thread_supported ())
    g_thread_init (NULL);

  if (thread->thread == NULL) {
    thread->stop_flag = 0;
    thread->thread = g_thread_create(handler, param, TRUE, NULL);
    if (thread->thread) {
      return 0;
    } else {
      return -1;
    }
  } else {
    // already running
    return 1;
  }
}

/*--------------------------------------------------------------------------*/

void thread_stop(thread_t *thread)
{
  GThread *tostop = thread->thread;
  if (tostop != NULL)
  {
    thread->stop_flag = 1;
    thread->thread = NULL;
    g_thread_join(tostop);
    thread->stop_flag = 0;
  }
}

/*--------------------------------------------------------------------------*/

int remote_call_init(remote_call_port_t *port)
{
  /* make sure threading is initialized */
  if (!g_thread_supported ())
    g_thread_init (NULL);

  if (pipe(port->fd) < 0) {
    return -1;
  }
  port->condition = g_cond_new();
  port->mutex = g_mutex_new();
  port->gtk_input_tag = 0;

  if (!port->condition || !port->mutex) {
    if (port->condition)
      g_cond_free(port->condition);
    if (port->mutex)
      g_mutex_free(port->mutex);
    return -1;
  }
  port->owner = g_thread_self();

  return 0;
}

/*--------------------------------------------------------------------------*/

int remote_call_close(remote_call_port_t *port)
{
  if (port->fd[0]) {
    close(port->fd[0]);
    port->fd[0] = 0;
  }
  if (port->fd[1]) {
    close(port->fd[1]);
    port->fd[1] = 0;
  }
  if (port->condition) {
    g_cond_free(port->condition);
    port->condition = 0;
  }
  if (port->mutex) {
    g_mutex_free(port->mutex);
    port->mutex = 0;
  }
  if (port->gtk_input_tag) {
    gtk_input_remove(port->gtk_input_tag);
  }
  return 0;
}

/*--------------------------------------------------------------------------*/

int remote_call_invoke(remote_call_port_t *port, remote_call_fnc func, void *context, void *data)
{
  if (g_thread_self() == port->owner) {
    /* call from within itself */
    func(context, data);
    return 0;
  }

  remote_call_t event;
  event.fnc = func;
  event.port = port;
  event.context = context;
  event.data = data;

  if (write(port->fd[1], &event, sizeof(event)) == sizeof(event)) {
    if (port->condition) {
      /* wait for reply from thread */
      g_mutex_lock(port->mutex);
      g_cond_wait(port->condition, port->mutex);
      g_mutex_unlock(port->mutex);
    }
    return 0;
  } else {
    /* cannot send event */
    return -1;
  }
}

/*--------------------------------------------------------------------------*/

static int remote_call_process(remote_call_port_t *port, remote_call_t *event)
{
  event->fnc(event->context, event->data);
  if (port->condition) {
    /* wake up the thread which signalled main */
    g_mutex_lock(port->mutex);
    g_cond_signal(port->condition);
    g_mutex_unlock(port->mutex);
  }
  return 0;
}

/*--------------------------------------------------------------------------*/

static void handle_remote_call(gpointer data,
                               gint fd _U_,
                               GdkInputCondition condition _U_)
{
  remote_call_port_t *port = (remote_call_port_t*) data;
  remote_call_t event;

  /* read one remote call item from pipe */
  if (read(port->fd[0], &event, sizeof(event)) < 0) {
    /* error reading */
    errprintf("Error reading data from remote call pipe\n");
    return;
  }
  remote_call_process(port, &event);
}

/*--------------------------------------------------------------------------*/

int remote_call_register(remote_call_port_t *port)
{
  if (port->gtk_input_tag != 0)
    return 0;

  port->gtk_input_tag = gtk_input_add_full(port->fd[0],
                                           GDK_INPUT_READ,
                                           handle_remote_call,
                                           NULL,
                                           (gpointer) port,
                                           NULL);
  if (port->gtk_input_tag != 0)
    return 0;
  else
    return -1;
}

/*--------------------------------------------------------------------------*/
