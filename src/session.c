/*
 * definitions for runtime session specific data handling
 *
 * This file is part of ANT (Ant is Not a Telephone)
 *
 * Copyright 2002, 2003 Roland Stigge
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

/* regular GNU system includes */
#include <string.h>
#include <stdio.h>
#ifdef HAVE_STDLIB_H
  #include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#include <fcntl.h>
#include <math.h>
#include <time.h>

/* GTK */
#include <gtk/gtk.h>

/* libsndfile */
#include <sndfile.h>

/* own header files */
#include "globals.h"
#include "session.h"
#include "sound.h"
#include "isdn.h"
#include "mediation.h"
#include "gtk.h"
#include "util.h"
#include "callerid.h"
#include "llcheck.h"
#include "settings.h"
#include "fxgenerator.h"
#include "server.h"
#include "g711.h"

/*!
 * @brief This is our session. Currently just one globally.
 */
session_t session;

/*!
 * @brief State and button names for various session states.
 */
struct state_data_t state_data[STATE_NUMBER] = {
{N_("Ready"),         N_("Dial"),   1,N_("Hang up"),0},/* STATE_READY         */
{N_("RING"),          N_("Answer"), 1,N_("Reject"), 1},/* STATE_RINGING       */
{N_("RING"),          N_("Answer"), 1,N_("Reject"), 1},/* STATE_RINGING_QUIET */
{N_("Dialing"),       N_("Pick up"),0,N_("Cancel"), 1},/* STATE_DIALING       */
{N_("B-Channel open"),N_("Pick up"),0,N_("Hang up"),1},/* STATE_CONVERSATION  */
{N_("Setup"),         N_("Pick up"),0,N_("Hang up"),0},/* STATE_SERVICE       */
{N_("Playback"),      N_("Pick up"),0,N_("Stop")   ,1} /* STATE_PLAYBACK      */
};

/*!
 * @brief Callback executed in session thread after hang up.
 *
 * @param context session object.
 * @param error ISDN error code cast to pointer, 0 for no error.
 */
static void isdn_hangup_callback(void *context, void *error);

/*!
 * @brief Callback executed in session thread after ISDN connected.
 *
 * @param context session object.
 * @param number remote number which has been connected.
 */
static void isdn_connect_callback(void *context, void *number);

/*!
 * @brief Thread main routine for audio input thread.
 *
 * @param data session.
 */
static gpointer handler_audio_input(gpointer data);

/*!
 * @brief Stop conversation threads.
 *
 * @param session session.
 * @param self_hangup if nonzero, hung up by our side, otherwise by remote side.
 */
static void session_deinit_conversation(session_t *session, int self_hangup);

/*!
 * @brief Opens audio devices for specified session.
 *
 * @param session session.
 * @return 0 on success, -1 on error.
 */
static int session_audio_open(session_t *session);

/*!
 * @brief Closes audio devices for specified session.
 *
 * @param session session.
 * @return 0 on success, -1 on error.
 */
static int session_audio_close(session_t *session);

/*!
 * @brief Recover from audio error.
 *
 * @param session session.
 * @param audio PCM handle.
 * @param err ALSA error code.
 * @return 0 on success, ALSA error code on error.
 */
static int session_snd_pcm_recover(session_t *session _U_, snd_pcm_t *audio, int err);

/*!
 * @brief Callback when connection established (in ISDN thread).
 *
 * @param context session.
 * @param number remote party number (may be NULL).
 */
static void session_isdn_connected(void *context, char *number);

/*!
 * @brief Callback when ISDN data received (in ISDN thread).
 *
 * @param context session.
 * @param data pointer to received data.
 * @param length length of received data (in bytes).
 */
static void session_isdn_data(void *context, void *data, unsigned int length);

/*!
 * @brief Callback when connection disconnected (in ISDN thread).
 *
 * @param context session.
 */
static void session_isdn_disconnected(void *context);

/*!
 * @brief Callback when connection attempt fails with an error (in ISDN thread).
 *
 * @param context session.
 * @param error CAPI error number.
 */
static void session_isdn_error(void *context, unsigned int error);

/*!
 * @brief Callback called on RING from other side (in session thread).
 *
 * @param context session.
 * @param data structure with callee and called numbers.
 */
static void isdn_ring_callback(void *context, void *data);

/*!
 * @brief Callback called on RING from other side (in ISDN thread).
 *
 * @param context session.
 * @param callee remote party number (may be NULL).
 * @param called called (our) number (may be NULL).
 */
static void session_isdn_ring(void *context, char *callee, char *called);

/*!
 * @brief Init ISDN device for session.
 *
 * @param session session.
 * @return 0 on success, -1 otherwise.
 */
static int session_isdn_init(session_t *session);

/*!
 * @brief Init recording related things in session.
 *
 * @param session session.
 * @return 0 on success, -1 otherwise.
 */
static int session_recording_init(session_t *session);

/*!
 * @brief Clean up recording related things in session.
 *
 * @param session session.
 * @return 0 on success, -1 otherwise.
 */
static int session_recording_deinit(session_t *session);

/*!
 * @brief Close isdn device and clean up (deallocate buffers).
 *
 * @param session session.
 * @return 0 on success, -1 otherwise.
 */
static int session_isdn_deinit(session_t *session);

/*!
 * @brief Called to initialize conversation, just after connection established.
 *
 * Includes state transition.
 *
 * @param session session to use.
 */
static void session_start_conversation(session_t *session);

/*!
 * @brief Repeatedly called function to update various stuff.
 *
 * @param data session.
 * @return TRUE to be called again.
 */
static gboolean session_timer_func(gpointer data);

/*!
 * @brief Effect thread main function.
 *
 * @param data session.
 */
static gpointer handler_effect(gpointer data);

/*!
 * @brief Sets status bar for audio state (e.g. "AUDIO OFF").
 *
 * @param session session.
 * @param note text to show (hide status bar if note is "").
 */
static void session_audio_notify(session_t *session, char *note);

/*!
 * @brief Cut session->dial_number_history to specified size.
 *
 * Uses session->dial_number_history_maxlen for maximum size. Then redisplay
 * in session->dial_number_box.
 *
 * @param session session to use.
 */
static void session_history_normalize(session_t *session);

/*--------------------------------------------------------------------------*/

static int session_audio_open(session_t *session)
{
    dbgprintf(1, "SESSION: Opening audio device(s).\n");
  if (open_audio_devices(session->audio_device_name_in,
			 session->audio_device_name_out,
			 1,
			 session->format_priorities,
			 &session->audio_in,
			 &session->audio_out,
			 &session->fragment_size_in,
			 &session->fragment_size_out,
			 &session->audio_format_in,
			 &session->audio_format_out,
			 &session->audio_speed_in,
			 &session->audio_speed_out)) {
    return -1;
  }
  return 0;
}

/*--------------------------------------------------------------------------*/

static int session_audio_close(session_t *session)
{
  dbgprintf(1, "SESSION: Closing audio device(s)\n");
  thread_stop(&session->thread_audio_input);
  if (close_audio_devices(session->audio_in, session->audio_out)) {
    return -1;
  }
  return 0;
}

/*--------------------------------------------------------------------------*/

static int session_snd_pcm_recover(session_t *session _U_, snd_pcm_t *audio, int err)
{
  int err2;
  if (err == -EBADFD) {
    dbgprintf(1, "AUDIO: Preparing audio for I/O\n");
    return snd_pcm_prepare(audio);
  } else {
    err2 = snd_pcm_recover(audio, err, 1);
    if (err2 != 0)
      return err2;
    dbgprintf(2, "AUDIO: snd_pcm_recover from error %s on %s\n",
              snd_strerror(err),
              (audio == session->audio_out) ? "output" : "input");
    return 0;
  }
}

/*--------------------------------------------------------------------------*/

int session_set_audio_state(session_t *session, enum audio_t state)
{
  enum audio_t oldstate = session->audio_state;

  if (state == oldstate) {
    return 0;
  }

  if (oldstate == AUDIO_EFFECT) {
    // stop effect first
    thread_stop(&session->thread_effect);
  }

  if (session->audio_state == AUDIO_DISCONNECTED) {
    /* message: open audio device(s) */
    if (debug) {
      if (strcmp(session->audio_device_name_in, session->audio_device_name_out))
        {
          /* different devices */
          dbgprintf(1, "AUDIO: Initializing %s and %s...\n",
                  session->audio_device_name_in, session->audio_device_name_out);
        } else {
          dbgprintf(1, "AUDIO: Initializing %s ...\n",
                  session->audio_device_name_in);
        }
    }

    /* other options */
    session->audio_speed_in = ISDN_SPEED;  /* default audio speed */
    session->audio_speed_out = ISDN_SPEED;

    /* audio device buffer fragment sizes */
    session->fragment_size_in = DEFAULT_FRAGMENT_SIZE;
    session->fragment_size_out = DEFAULT_FRAGMENT_SIZE;

    session->format_priorities = default_audio_priorities;

    if (session_audio_open(session)) {
      errprintf("AUDIO: Error initializing audio device(s).\n");
      return -1;
    }

    session->ratio_in = (double)session->audio_speed_out / ISDN_SPEED;
    session->ratio_out = (double)ISDN_SPEED / session->audio_speed_in;

    if (mediation_makeLUT(session->audio_format_out, &session->audio_LUT_in,
                          session->audio_format_in, &session->audio_LUT_out,
                          &session->audio_LUT_generate,
                          &session->audio_LUT_analyze,
                          &session->audio_LUT_alaw2short)) {
      errprintf("AUDIO: Error building conversion look-up-table.\n");
      return -1;
    }

    session->audio_sample_size_in =
      sample_size_from_format(session->audio_format_in);
    session->audio_sample_size_out =
      sample_size_from_format(session->audio_format_out);
  } else if (state == AUDIO_DISCONNECTED) {
    /* close devices */

    /* free allocated buffers */
    free(session->audio_LUT_in);
    free(session->audio_LUT_out);
    free(session->audio_LUT_generate);
    free(session->audio_LUT_analyze);
    free(session->audio_LUT_alaw2short);

    /* close audio device(s) */
    if (session_audio_close(session)) {
      errprintf("AUDIO: Error closing sound device(s).\n");
      return -1;
    }
  }

  /* set new state on session */
  session->audio_state = state;

  if (state == AUDIO_CONVERSATION) {
    /* start thread handling audio input during conversation */
    if (!thread_is_running(&session->thread_audio_input)) {
      if (thread_start(&session->thread_audio_input, handler_audio_input, session) < 0) {
        errprintf("AUDIO: Cannot start audio input thread\n");
        session->audio_state = AUDIO_IDLE;
        return -1;
      }
    }
  } else {
    /* no conversation, shut down input, if any */
    thread_stop(&session->thread_audio_input);
  }

  return 0;
}

/*--------------------------------------------------------------------------*/

static void session_isdn_connected(void *context, char *number)
{
  session_t *session = (session_t*) context;

  remote_call_invoke(&session->rem_port, isdn_connect_callback, session, number);
}

/*--------------------------------------------------------------------------*/

static void session_isdn_data(void *context, void *data, unsigned int length)
{
  session_t *session = (session_t*) context;
  unsigned int ptr, outsize, framesize, size;
  unsigned char outbuffer[16384];
  short recbuffer[8192];
  int err;
#if 0
  double factor;
#endif

  convert_isdn_to_audio(session,
                        data, length,
                        outbuffer, &outsize,
                        recbuffer,
                        1);

  framesize = sample_size_from_format(session->audio_format_out);
  outsize /= framesize;

  /* dump the ISDN data to audio and/or recording */
  ptr = 0;
  while (ptr < outsize) {
    size = outsize - ptr;
    err = snd_pcm_writei(session->audio_out,
                         outbuffer + ptr * framesize,
                         size);
    if (err < 0) {
      if (err != -EAGAIN) {
        err = session_snd_pcm_recover(session, session->audio_out, err);
        if (err >= 0) {
          /* write one frame doubled to catch up */
          snd_pcm_writei(session->audio_out,
                         outbuffer + ptr * framesize,
                         size);
          continue; /* retry */
        }
        /* TODO: handle error better and/or stop audio */
        errprintf("AUDIO: Error writing to audio: %s\n", snd_strerror(err));
        break;
      } else {
        /* non-blocking write failed */
        dbgprintf(2, "AUDIO: Clock unsynchronized, skipping audio buffer\n");
        isdn_speed_addsamples(&session->audio_out_speed, size);
        break;
      }
    } else {
      /* some data written */
      ptr += err;
      isdn_speed_addsamples(&session->audio_out_speed, err);

      if (debug > 1) {
        isdn_speed_debug(&session->audio_out_speed, 2, "AUDIO: out");
      }

#if 0
      /* NOTE: even though ISDN and audio should both run at 8000Hz, they don't.
         At least not exactly. This code experiment tries to adjust audio speed
         to match ISDN speed to prevent underruns and frame drops. Someone
         might try to take it from here... */
      snd_pcm_sframes_t delay;
      if (snd_pcm_delay(session->audio_out, &delay) == 0) {
        /* reevaluate the audio speed */

        /* now compute factor to speed up/down the audio to meet optimum delay */
        factor = (4*session->fragment_size_out - delay) / 3000.0 + 1.0;

        /* normalize to ISDN speed */
        factor *= ISDN_SPEED / ((double)session->audio_speed_out);

        if (factor < 0.95)
          factor = 0.95;
        else if (factor > 1.05)
          factor = 1.05;

        factor *= ((double)session->audio_speed_out / ISDN_SPEED);

        session->ratio_in = (session->ratio_in + factor) / 2.0;

        dbgprintf(3, "AUDIO: out delay: %d, fragment %d, ratio %.3f\n",
                  (int) delay, (int) session->fragment_size_out,
                  session->ratio_in);
      }
#endif
    }
  }

}

/*--------------------------------------------------------------------------*/

static void session_isdn_disconnected(void *context)
{
  session_t *session = (session_t*) context;

  dbgprintf(1, "SESSION: Disconnected callback\n");

  remote_call_invoke(&session->rem_port, isdn_hangup_callback, session, NULL);
}

/*--------------------------------------------------------------------------*/

static void session_isdn_error(void *context, unsigned int error)
{
  session_t *session = (session_t*) context;

  dbgprintf(1, "SESSION: Error callback, 0x%x\n", error);

  remote_call_invoke(&session->rem_port, isdn_hangup_callback, session, (void*) (long) error);
}

/*--------------------------------------------------------------------------*/

static void isdn_ring_callback(void *context, void *data)
{
  session_t *session = (session_t*) context;
  struct msg {
    char *callee;
    char *called;
  } *numbers = (struct msg*) data;
  char *callee = numbers->callee;
  char *called = numbers->called;

  /* caller id update */
  session->ring_time = time(NULL);

  /* save callee's number */
  free(session->from);
  free(session->to);
  session->from = strdup(callee ? (char*) callee : "(no caller ID)");
  session->to = strdup(called ? (char*) called : "(no caller ID)");

  cid_add_line(session, CALL_IN, session->from, session->to);

  if (session_set_state(session, STATE_RINGING))
    session_set_state(session, STATE_RINGING_QUIET);
}

/*--------------------------------------------------------------------------*/

static void session_isdn_ring(void *context, char *callee, char *called)
{
  session_t *session = (session_t*) context;
  struct {
    char *callee;
    char *called;
  } msg;
  msg.callee = callee;
  msg.called = called;

  dbgprintf(1, "SESSION: Ring callback from '%s' to '%s'\n",
            callee ? callee : "(no number)",
            called ? called : "(no number)");

  remote_call_invoke(&session->rem_port, isdn_ring_callback, session, &msg);
}

/*--------------------------------------------------------------------------*/

static int session_isdn_init(session_t *session)
{
  static isdn_callback_t callbacks = {
    session_isdn_connected,
    session_isdn_data,
    session_isdn_disconnected,
    session_isdn_error,
    session_isdn_ring
  };

  /* open and init isdn device */
  dbgprintf(1, "SESSION: Initializing ISDN device...\n");

  if (open_isdn_device(&session->isdn, &callbacks, session) < 0) {
    errprintf("SESSION: Error opening isdn device.\n");
    return -1;
  }

  if (isdn_setMSN(&session->isdn, session->msn) ||
      isdn_setMSNs(&session->isdn, session->msns)) {
    errprintf("SESSION: Error setting MSN properties.\n");
    close_isdn_device(&session->isdn);
    return -1;
  }

  session->isdn_active = 1;
  return 0;
}

/*--------------------------------------------------------------------------*/

int session_activate_isdn(session_t *session, unsigned int activate)
{
  int result = 0;

  if (activate) {
    if (!session->isdn_active) {
      result = activate_isdn_device(&session->isdn, 1);
      if (result == 0)
        session->isdn_active = 1;
    }
  } else {
    if (session->isdn_active) {
      /* make sure the connection is closed */
      session->isdn_active = 0;
      if (session->isdn.state == ISDN_IDLE) {
        result = activate_isdn_device(&session->isdn, 0);
      } else {
        session->hangup_reason = _("(ABORTED)");
        isdn_hangup(&session->isdn);
      }
    }
  }

  return result;
}

/*--------------------------------------------------------------------------*/

static int session_recording_init(session_t *session)
{
  /* mediation recording stuff */

  if (!(session->recorder =
	(struct recorder_t *)malloc(sizeof(struct recorder_t)))) {
    return -1;
  }
  return recording_init(session->recorder);
}

/*--------------------------------------------------------------------------*/

static int session_recording_deinit(session_t *session)
{
  free(session->recorder);
  session->recorder = 0;
  return 0;
}

/*--------------------------------------------------------------------------*/

static int session_isdn_deinit(session_t *session)
{
  dbgprintf(1, "SESSION: Closing ISDN device...\n");
  if (close_isdn_device(&session->isdn) < 0) {
    errprintf("SESSION: Error closing ISDN device.\n");
  }
  session->isdn_active = 0;
  return 0;
}

/*--------------------------------------------------------------------------*/

int session_init(session_t *session,
		 char *audio_device_name_in,
		 char *audio_device_name_out,
		 char *msn, char *msns) {
  int i;

  /*
   * first: set all defaults possibly overridden by options file
  */

  session->dial_number_history = NULL;
  session->dial_number_history = g_list_append(session->dial_number_history,
  					       strdup(""));
  session->dial_number_history_maxlen = 10; /* config overrides this */
  session->cid_num_max = 100; /* 0 means no limit */

  /* options defaults */
  session->option_save_options = 1; /* save options automatically (on exit) */
  session->option_release_devices = 1;
  session->option_show_llcheck = 1;
  session->option_show_callerid = 1;
  session->option_show_controlpad = 1;
  session->option_muted = 0;
  session->option_record = 0;
  session->option_record_local = 1;
  session->option_record_remote = 1;
  session->option_recording_format =
    RECORDING_FORMAT_WAV | RECORDING_FORMAT_ULAW;
  session->option_popup = 1;

  session->option_calls_merge = 1;
  session->option_calls_merge_max_days = 10;

  session->exec_on_incoming = strdup("");

  for (i = 0; i < 4; i++) {
    asprintf(&session->preset_names[i], _("Preset %d"), i + 1);
    session->preset_numbers[i] = strdup("");
  }

  session->audio_device_name_in = strdup(audio_device_name_in);
  session->audio_device_name_out = strdup(audio_device_name_out);
  session->msn = strdup(msn);
  session->msns = strdup(msns);

  session->from = strdup("");
  session->to = strdup("");

  settings_options_read(session); /* override defaults analyzing options file */

  /* command line configurable parameters: set to hard coded defaults
     if no setting was made (either at command line or in options file) */
  if (!strcmp(session->audio_device_name_in, "")) {
    free(session->audio_device_name_in);
    session->audio_device_name_in = strdup(DEFAULT_AUDIO_DEVICE_NAME_IN);
  }
  if (!strcmp(session->audio_device_name_out, "")) {
    free(session->audio_device_name_out);
    session->audio_device_name_out = strdup(DEFAULT_AUDIO_DEVICE_NAME_OUT);
  }
  if (!strcmp(session->msn, "")) {
    free(session->msn);
    session->msn = strdup(DEFAULT_MSN);
  }
  if (!strcmp(session->msns, "")) {
    free(session->msns);
    session->msns = strdup(DEFAULT_MSNS);
  }

  /* other defaults */
  session->dial_number_history_pointer = 0;
  session->touchtone_countdown_isdn = 0;
  session->touchtone_countdown_audio = 0;
  session->touchtone_index = 0;

  session->ring_time = 0;
  session->unanswered = 0;

  session->gtk_local_input_tag = 0;
  session->gtk_updater_timer_tag = 0;

  /* create communication pipe for communicating events from thread to main */
  if (remote_call_init(&session->rem_port) < 0) {
    errprintf("SESSION: Cannot create remote call port\n");
    return -1;
  }

  /* setup audio and isdn */
  session->audio_state = AUDIO_DISCONNECTED;
  thread_init(&session->thread_audio_input);

  session->state = STATE_READY; /* initial state */
  thread_init(&session->thread_effect);
  session->effect = EFFECT_NONE;

  if (!session->option_release_devices)
    session_set_audio_state(session, AUDIO_IDLE);
  if (session_isdn_init(session) < 0)
    return -1;
  if (session_recording_init(session) < 0)
    return -1;

  /* init server functionality */
  if (server_init(session) < 0)
    return -1;

  /* register remote call with GTK */
  if (remote_call_register(&session->rem_port) < 0) {
    errprintf("SESSION: Cannot register remote call port with GTK\n");
    return -1;
  }

  return 0;
}

/*--------------------------------------------------------------------------*/

/*!
 * @brief Helper function to free single element data from g_list.
 *
 * Used by session_deinit.
 */
static void free_g_list_element(gpointer data, gpointer user_data _U_) {
  free(data);
}

/*--------------------------------------------------------------------------*/

int session_deinit(session_t *session) {
  int i;

  /* stop GTK handlers */
  session_io_handlers_stop(session);

  /* deinit server functionality */
  if (server_deinit(session) < 0)
    return -1;

  if (session->option_save_options)
    settings_options_write(session);

  /* stop communication request handler */
  remote_call_close(&session->rem_port);

  /* free dial_number_history */
  g_list_foreach(session->dial_number_history, free_g_list_element, NULL);
  g_list_free(session->dial_number_history);

  /* close devices and clean up (buffers) */
  if (session_isdn_deinit(session) < 0)
    return -1;
  if (session_set_audio_state(session, AUDIO_DISCONNECTED) < 0)
    return -1;

  if (session_recording_deinit(session) < 0) return -1;

  free(session->exec_on_incoming);

  /* clean up pre-set options */
  free(session->audio_device_name_in);
  free(session->audio_device_name_out);
  free(session->msn);
  free(session->msns);

  /* clean up rest */
  for (i = 0; i < 4; i++) {
    free(session->preset_names[i]);
    free(session->preset_numbers[i]);
  }
  free(session->from);
  free(session->to);

  return 0;
}

/*--------------------------------------------------------------------------*/

static gpointer handler_audio_input(gpointer data) {
  session_t *session = (session_t*) data;

  unsigned char inbuffer[16384];    /* audio input buffer */
  unsigned char outbuffer[16384];   /* ISDN output buffer */
  short recbuffer[16384];
  int err, bytes_per_frame;

  dbgprintf(1, "AUDIO: Starting audio input thread\n");

  /* set blocking mode for audio input */
  snd_pcm_nonblock(session->audio_in, 0);

  isdn_speed_init(&session->audio_out_speed);
  isdn_speed_init(&session->audio_in_speed);

  bytes_per_frame = sample_size_from_format(session->audio_format_in);

  while (!thread_is_stopping(&session->thread_audio_input)) {
    for (;;) {
      err = snd_pcm_readi(session->audio_in, inbuffer, session->fragment_size_in/*sizeof(inbuffer)*/);
      if (err < 0) {
        err = session_snd_pcm_recover(session, session->audio_in, err);
        if (err >= 0)
          continue;
        switch (err) {
        case -EAGAIN:
          break;
        default:
          errprintf("AUDIO: Unrecoverable PCM read error %s, terminating\n",
                    snd_strerror(err));
          // TODO: trigger hangup
          return (gpointer) 5;
        }
      } else {
        err *= bytes_per_frame;
        isdn_speed_addsamples(&session->audio_in_speed, err);

        /* process the data */
        unsigned int outsize;
        convert_audio_to_isdn(session,
                              inbuffer, err,
                              outbuffer, &outsize,
                              recbuffer);

        /* dump the audio to ISDN */
        isdn_send_data(&session->isdn, outbuffer, outsize);

        if (debug > 1) {
          isdn_speed_debug(&session->audio_in_speed, 1, "AUDIO: in");
        }
      }
      break;
    }
  }

  dbgprintf(1, "AUDIO: Stopping audio input thread\n");

  return 0;
}

/*--------------------------------------------------------------------------*/

int session_start_recording(session_t *session)
{
  char *digits = NULL;
  int result = 0;

  if ((digits = util_digitstime(&session->vcon_time))) {
    if (recording_open(session->recorder, digits,
        session->option_recording_format))
    {
      errprintf("SESSION: Error opening audio file for recording.\n");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
                                   session->record_checkbutton), FALSE);
      result = -1;
    } else {
      cid_row_mark_record(session, session->cid_num - 1);
    }
    free(digits);
  } else {
    errprintf("SESSION: Error generating audio filename for recording.\n");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
                                 session->record_checkbutton), FALSE);
    result = -1;
  }
  return result;
}

static void session_start_conversation(session_t *session)
{
  session->vcon_time = time(NULL); /* for caller id monitor */
  cid_set_date(session, session->vcon_time);
  session_effect_stop(session);
  session_set_state(session, STATE_CONVERSATION);
  if (session->option_record) {
    session_start_recording(session);
  }

  session_io_handlers_stop(session);
  if (session_set_audio_state(session, AUDIO_CONVERSATION) < 0) {
    /* TODO: stop conversation, as no audio possible */
    return;
  }

  /* set non-blocking mode for audio output */
  snd_pcm_nonblock(session->audio_out, 1);

  session_io_handlers_start(session);
}

/*--------------------------------------------------------------------------*/

static void isdn_hangup_callback(void *context, void *error)
{
  session_t *session = (session_t*) context;

  // NOTE: error contains no pointer, but error code from CAPI

  char *reason = session->hangup_reason;
  if (error) {
    unsigned long e = (unsigned long) error;
    reason = _("ERROR");
    switch (e) {
      case 0x3301:
      case 0x3302:
      case 0x3303:

      case 0x34EF:  /* Protocol error, unspecified */
        reason = _("PROTOCOL ERROR");
        break;

      case 0x3481:  /* Unallocated (unassigned) number */
      case 0x349C:  /* Invalid number format */
        reason = _("WRONG NUMBER");
        break;

      case 0x3482:  /* No route to specified transit network */
      case 0x3483:  /* No route to destination */
        reason = _("NO ROUTE");
        break;

      case 0x3486:  /* Channel unacceptable */
      case 0x3487:  /* Call awarded and being delivered in an established channel */
      case 0x34A2:  /* No circuit / channel available */
      case 0x34A9:  /* Temporary failure */
      case 0x34AA:  /* Switching equipment congestion */
      case 0x34AC:  /* Requested circuit / channel not available */
      case 0x34AF:  /* Resources unavailable, unspecified */
        reason = _("CHANNEL UNAVAILABLE");
        break;

      case 0x3491:  /* User busy */
        reason = _("BUSY");
        break;

      case 0x3492:  /* No user responding (wrong MSN?) */
      case 0x3493:  /* No answer from user (user alerted) */
        reason = _("NO ANSWER");
        break;

      case 0x3495:  /* Call rejected */
        reason = _("REJECTED");
        break;

      case 0x3496:  /* Number changed */
        reason = _("NUMBER CHANGED");
        break;

      case 0x349A:  /* Non-selected user clearing */
        reason = _("DISCONNECT");
        break;

      case 0x349B:  /* Destination out of order */
        reason = _("REMOTE FAILURE");
        break;

      case 0x34A6:  /* Network out of order */
      case 0x34DB:  /* Invalid transit network selection */
        reason = _("NETWORK ERROR");
        break;

      case 0x34B1:  /* Quality of service unavailable */
      case 0x34BA:  /* Bearer capability not presently available */
      case 0x34BF:  /* Service or option not available, unspecified */
        reason = _("SERVICE UNAVAILABLE");
        break;

      case 0x34B2:  /* Requested facility not subscribed */
      case 0x34B9:  /* Bearer capability not authorized */
        reason = _("NOT SUBSCRIBED");
        break;

      case 0x34C1:  /* Bearer capability not implemented */
      case 0x34C2:  /* Channel type not implemented */
      case 0x34C5:  /* Requested facility not implemented */
      case 0x34C6:  /* Only restricted digital information bearer capability is available */
      case 0x34CF:  /* Service or option not implemented, unspecified */
        reason = _("NOT IMPLEMENTED");
        break;

      case 0x34D8:  /* Incompatible destination */
        reason = _("NOT COMPATIBLE");
        break;

      /* Other CAPI network error codes:
      0x349D Facility rejected
      0x349E Response to STATUS ENQUIRY
      0x34AB Access information discarded
      0x34D1 Invalid call reference value
      0x34D2 Identified channel does not exist
      0x34D3 A suspended call exists, but this call identity does not
      0x34D4 Call identity in use
      0x34D5 No call suspended
      0x34D6 Call having the requested call identity has been cleared
      0x34DF Invalid message, unspecified
      0x34E0 Mandatory information element is missing
      0x34E1 Message type non-existent or not implemented
      0x34E2 Message not compatible with call state or message type non-existent or not implemented
      0x34E3 Information element non-existent or not implemented
      0x34E4 Invalid information element contents
      0x34E5 Message not compatible with call state
      0x34E6 Recovery on timer expiry
      0x34FF Interworking, unspecified */
    }
  }

  if (session->state == STATE_CONVERSATION) {
    session_deinit_conversation(session, error ? 0 : 1);
  } else if (session->state == STATE_RINGING ||
             session->state == STATE_RINGING_QUIET) {
    reason = _("(MISSED)");
    cid_mark_row(session, session->cid_num - 1, 1);
  }
  session_set_state(session, STATE_READY);
  cid_set_duration(session, reason);

  if (!session->isdn_active) {
    /* we were asked to deactivate ISDN */
    activate_isdn_device(&session->isdn, 0);
  }
}

/*--------------------------------------------------------------------------*/

static void isdn_connect_callback(void *context, void *number)
{
  session_t *session = (session_t*) context;

  if (session->state == STATE_RINGING || session->state == STATE_RINGING_QUIET) {
    char *old = session->from;
    session->from = strdup((char*) number);
    free(old);
  }

  session_start_conversation(session); /* including state transition */
}

/*--------------------------------------------------------------------------*/

static gboolean session_timer_func(gpointer data)
{
  session_t *session = (session_t *) data;

  switch (session->state) {
    case STATE_CONVERSATION:
      /* flush any recording buffers */
      recording_flush(session->recorder, 0);
      /* fall through */

    case STATE_RINGING:
    case STATE_PLAYBACK:
    case STATE_DIALING:
    case STATE_SERVICE:
      /* update line level bars */
      llcheck_bar_set(session->llcheck_in, log10(1.0 + 9.0 * session->llcheck_in_state));
      llcheck_bar_set(session->llcheck_out, log10(1.0 + 9.0 * session->llcheck_out_state));

      if (session->state == STATE_PLAYBACK && session->effect == EFFECT_NONE) {
        /* playback finished, set ready state */
        session_set_state(session, STATE_READY);
      }
      break;

    default:
      /* line level is at 0 when no audio */
      llcheck_bar_set(session->llcheck_in, 0.0);
      llcheck_bar_set(session->llcheck_out, 0.0);
      break;
  }

  return TRUE;
}

/*--------------------------------------------------------------------------*/

void session_io_handlers_start(session_t *session)
{
  /* stop old handlers first, if any */
  session_io_handlers_stop(session);

  /* server functionality */
  session->gtk_local_input_tag = gtk_input_add_full(session->local_sock,
					            GDK_INPUT_READ,
					            server_handle_local_input,
						    NULL,
					            (gpointer) session,
						    NULL);
  session->gtk_updater_timer_tag = gtk_timeout_add(100,
                                                    session_timer_func,
                                                    (gpointer) session);
}

/*--------------------------------------------------------------------------*/

void session_io_handlers_stop(session_t *session)
{
  if (session->gtk_local_input_tag) {
    gtk_input_remove(session->gtk_local_input_tag);
    session->gtk_local_input_tag = 0;
  }
  if (session->gtk_updater_timer_tag) {
    gtk_timeout_remove(session->gtk_updater_timer_tag);
    session->gtk_updater_timer_tag = 0;
    llcheck_bar_reset(session->llcheck_in);
    llcheck_bar_reset(session->llcheck_out);
  }
}

/*--------------------------------------------------------------------------*/

int session_reset_audio(session_t *session)
{
  int result = 0;
  
  if (!(session->option_release_devices &&
	(session->state == STATE_READY ||
	 session->state == STATE_RINGING_QUIET))) {
    if (session_audio_close(session)) {
      errprintf("Error closing audio device(s) while resetting.\n");
      result = -1;
    }
    if (session_audio_open(session)) {
      errprintf("Error reopening audio device(s) while resetting.\n");
      result = -1;
    }
  }
  return result;
}

/*--------------------------------------------------------------------------*/

static void session_deinit_conversation(session_t *session, int self_hangup _U_)
{
  /* stop audio thread */
  session_set_audio_state(session, AUDIO_IDLE);
  /* stop recording, if used */
  recording_close(session->recorder);

  session_io_handlers_stop(session);
  session_reset_audio(session);
  session_io_handlers_start(session);
}

/*--------------------------------------------------------------------------*/

static gpointer handler_effect(gpointer data)
{
  session_t *session = (session_t *) data;
  unsigned int i;
  int err;
  unsigned long effectpos = 0;        /* position within effect in frames */
  short buffer[2048];                 /* buffer for sound file samples */
  int just_read;                      /* read count from sndfile */
  int sample;                         /* linear sample to convert to A-law */
  unsigned char alawbuffer[4096];     /* buffer for alaw samples */
  unsigned int alawcount;             /* count of samples to play */
  unsigned char sndbuffer[12*4096];   /* sound data buffer */
  short recbuffer[4096];              /* temporary */
  unsigned int sndcount;              /* count of bytes in sound buffer */
  unsigned int ptr;                   /* playback pointer */
  unsigned int size;                  /* playback frame size */
  unsigned int framesize;             /* frame/sample size (1 or 2B) */
  int term_retry;                     /* retry count on termination */

  dbgprintf(1, "EFFECT: Starting effect thread\n");

  /* set blocking mode */
  snd_pcm_nonblock(session->audio_out, 0);
  /* set non-blocking mode */
  snd_pcm_nonblock(session->audio_in, 1);

  framesize = sample_size_from_format(session->audio_format_out);

  while (!thread_is_stopping(&session->thread_effect)) {
    switch (session->effect) {
    case EFFECT_SOUNDFILE:
      just_read = sf_readf_short(session->effect_sndfile,
                                 buffer,
                                 sizeof(buffer) / 4);
      if (just_read > 0) {
        /* convert samples to alaw */
        for (i = 0; i < (unsigned int) just_read; ++i) {
          /* convert to ALAW */
          sample = ((int) buffer[2*i]) + ((int) buffer[2*i+1]);
          if (sample < -32768)
            sample = -32768;
          else if (sample > 32767)
            sample = 32767;
          alawbuffer[i] = linear2alaw(sample);
        }
        alawcount = just_read;
      } else {
        alawcount = 0;
      }
      break;

    case EFFECT_RING:     /* somebody's calling */
    case EFFECT_RINGING:  /* waiting for the other end to pick up the phone */
    case EFFECT_TEST:     /* play test sound (e.g. line level check) */
    case EFFECT_TOUCHTONE:/* play a touchtone */
    case EFFECT_EMPTY:    /* silence for llcheck */
      for (i = 0; i < sizeof(alawbuffer) / 4; ++i)
        alawbuffer[i] = fxgenerate(session,
                                   session->effect,
                                   session->touchtone_index,
                                   effectpos++ / 8000.0);
      alawcount = sizeof(alawbuffer) / 4;
      break;

    default:
      errprintf("EFFECT: Unknown effect %d to play, exiting thread\n",
              session->effect);
      return (gpointer) 1;
    }

    if (alawcount == 0) {
      /* end of file */
      dbgprintf(1 ,"EFFECT: End-of-file reached, stopping playback\n");

      /* set non-blocking mode */
      snd_pcm_nonblock(session->audio_out, 1);
      /* drain output buffer in order not to cut last seconds of playback */
      snd_pcm_drain(session->audio_out);
      term_retry = 15;
      while (!thread_is_stopping(&session->thread_effect) && term_retry--) {
        usleep(20);
      }
      /* drop the rest, if any, and quit effect thread */
      snd_pcm_drop(session->audio_out);
      break;
    }

    /* convert A-law to audio */
    convert_isdn_to_audio(session,
                          alawbuffer, alawcount,
                          sndbuffer, &sndcount,
                          recbuffer, 0);
    sndcount /= framesize;

    /* play it! */
    ptr = 0;
    err = 0;
    while (ptr < sndcount && !thread_is_stopping(&session->thread_effect)) {
      size = sndcount - ptr;
      if (size > 512) /* limit to 512B/syscall to allow timely stopping */
        size = 512;
      if ((err = snd_pcm_writei(session->audio_out,
                                sndbuffer + ptr * framesize,
                                size)) < 0) {
        err = session_snd_pcm_recover(session, session->audio_out, err);
        if (err >= 0)
          continue; /* retry */
        errprintf("EFFECT: Error writing effect to audio device: %s\n",
                snd_strerror(err));
        break;
      } else {
        ptr += err;

        /* try to read some audio to adjust line level for input */
        err = snd_pcm_readi(session->audio_in, sndbuffer, size);
        if (err < 0) {
          /* restart audio input (it slipped) */
          err = session_snd_pcm_recover(session, session->audio_in, err);
        } else if (err > 0) {
          /* convert read data, this updates llcheck */
          convert_audio_to_isdn(session, sndbuffer, err, alawbuffer, &alawcount, recbuffer);
        }
        err = 0;
      }
    }
    if (err < 0)
      break;
  }

  if (session->effect == EFFECT_SOUNDFILE) {
    /* signalise we have stopped playback */
    sf_close(session->effect_sndfile);
    session->effect_sndfile = 0;
  }
  session->effect = EFFECT_NONE;

  /* set non-blocking mode */
  snd_pcm_nonblock(session->audio_out, 1);

  /* playback stopped, reset audio (devices closed elsewhere) */
  audio_stop(session->audio_in, session->audio_out);

  dbgprintf(1, "EFFECT: Stopping effect thread\n");

  return (gpointer) 0;
}

/*--------------------------------------------------------------------------*/

void session_effect_start(session_t *session, enum effect_t kind)
{
  session_effect_stop(session);

  if (session_set_audio_state(session, AUDIO_EFFECT) < 0) {
    /* cannot open audio */
    return;
  }

  if (kind == EFFECT_SOUNDFILE) {
    session->effect_sfinfo.format = 0;
    if (!(session->effect_sndfile =
      sf_open(session->effect_filename, SFM_READ, &session->effect_sfinfo)))
    {
      errprintf("EFFECT: Error opening sound file '%s' for playback.\n",
               session->effect_filename);
      return;
    }
  }

  session->effect = kind;
  session->effect_pos = 0;
  thread_start(&session->thread_effect, handler_effect, session);
}

/*--------------------------------------------------------------------------*/

void session_effect_stop(session_t *session)
{
  thread_stop(&session->thread_effect);
  if (session->effect != EFFECT_NONE) { /* stop only if already playing */
    session->effect = EFFECT_NONE;
  }
}

/*--------------------------------------------------------------------------*/

static void session_audio_notify(session_t *session, char *note)
{
  GtkWidget *dummy_label; /* needed to adjust size of sub-statusbar */
  GtkRequisition requisition;
  
  gtk_widget_hide(session->audio_warning);
  if (*note) {
    gtk_statusbar_pop(GTK_STATUSBAR(session->audio_warning),
		      session->audio_context_id);
    gtk_statusbar_push(GTK_STATUSBAR(session->audio_warning),
		       session->audio_context_id, note);
    
    dummy_label = gtk_label_new(note);
    gtk_widget_show(dummy_label);
    gtk_widget_size_request(dummy_label, &requisition);
    gtk_widget_set_size_request(session->audio_warning,
        requisition.width + 4, -1);

    gtk_widget_show(session->audio_warning);
  }
}

/*--------------------------------------------------------------------------*/

int session_set_state(session_t *session, enum state_t state)
{
  int result = 0;

  /* open / close audio when needed, set state */
  session_io_handlers_stop(session);
  if (state == STATE_READY && state != session->state && session->state != STATE_RINGING_QUIET) {
    /* release audio if going to idle state */
    session_effect_stop(session);
    if (session->option_release_devices) {
      session_set_audio_state(session, AUDIO_DISCONNECTED);
    } else {
      session_set_audio_state(session, AUDIO_IDLE);
    }
  }
  session->state = state;
  session_io_handlers_start(session);

  /* some menu items are selected only in STATE_READY */
  gtk_widget_set_sensitive(session->menuitem_settings, state == STATE_READY);
  gtk_widget_set_sensitive(session->menuitem_line_check, state == STATE_READY);

  /* start / stop effects when needed */
  switch (state) {
  case STATE_DIALING:
    if (session->effect == EFFECT_NONE)
      session_effect_start(session, EFFECT_RINGING);
    dbgprintf(1, "SESSION: New state: STATE_DIALING\n");
    break;
  case STATE_RINGING:
    if (session->option_popup) {
      gtk_window_present(GTK_WINDOW(session->main_window));
    }
    if (session->effect == EFFECT_NONE)
      session_effect_start(session, EFFECT_RING);
    dbgprintf(1, "SESSION: New state: STATE_RINGING\n");
    break;
  case STATE_RINGING_QUIET:
    if (session->option_popup) {
      gtk_window_present(GTK_WINDOW(session->main_window));
    }
    dbgprintf(1, "SESSION: New state: STATE_RINGING_QUIET\n");
    break;
  case STATE_READY:
    gtk_widget_grab_focus(GTK_WIDGET(GTK_COMBO(session->dial_number_box)
				     ->entry));
    dbgprintf(1, "SESSION: New state: STATE_READY\n");
    break;
  case STATE_CONVERSATION:
    session->touchtone_countdown_isdn = 0;
    session->touchtone_countdown_audio = 0;
    dbgprintf(1, "SESSION: New state: STATE_CONVERSATION\n");
    break;
  case STATE_SERVICE:
    dbgprintf(1, "SESSION: New state: STATE_SERVICE\n");
    break;
  case STATE_PLAYBACK:
    if (session->effect == EFFECT_NONE)
      session_effect_start(session, EFFECT_SOUNDFILE);
    dbgprintf(1, "SESSION: New state: STATE_PLAYBACK\n");
    break;
  default:
    errprintf("SESSION: session_set_state: Unknown state %d.\n", state);
    result = -1;
    break;
  }

  /* audio on / off notify */
  if (session->option_release_devices) {
    session_audio_notify(session,
			 state == STATE_READY || state == STATE_RINGING_QUIET ?
			 _("Audio OFF") : _("Audio ON"));
  } else {
    session_audio_notify(session, "");
  }

  /* status line */
  gtk_statusbar_pop(GTK_STATUSBAR(session->status_bar),
		    session->phone_context_id);
  gtk_statusbar_push(GTK_STATUSBAR(session->status_bar),
                     session->phone_context_id,
		     _(state_data[state].status_bar));

  gtk_label_set_text(GTK_LABEL(session->pick_up_label),
		     _(state_data[state].pick_up_label));
  gtk_widget_set_sensitive(session->pick_up_button,
			   state_data[state].pick_up_state);

  gtk_label_set_text(GTK_LABEL(session->hang_up_label),
		     _(state_data[state].hang_up_label));
  gtk_widget_set_sensitive(session->hang_up_button,
			   state_data[state].hang_up_state);

  if (state == STATE_READY) {
    llcheck_bar_reset(session->llcheck_in);
    llcheck_bar_reset(session->llcheck_out);
  }

  return result;
}

/*--------------------------------------------------------------------------*/

void session_make_call(session_t *session, char *number)
{
  if (session->state == STATE_READY || session->state == STATE_PLAYBACK) {
    gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(session->dial_number_box)->entry),
                      number);
    gtk_button_clicked(GTK_BUTTON(session->pick_up_button));
  }
}

/*--------------------------------------------------------------------------*/

void gtk_handle_pick_up_button(GtkWidget *widget _U_, gpointer data)
{
  session_t *session = (session_t *) data;
  const char *number; /* the number to dial "inside" gtk (entry) */
  char *clear_number; /* number after un_vanity() */
  
  switch (session->state) {
  case STATE_READY: /* we are in command mode and want to dial */
    session_activate_isdn(session, 1);
    number = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(session->dial_number_box)
					  ->entry));
    /* replace letters with numbers ("Vanity" Numbers) */
    clear_number = un_vanity(strdup(number));
    if (strcmp(clear_number, "") != 0 && session->isdn_active) {
      if (!session_set_state(session, STATE_DIALING)) {
	/* dial only if audio is on etc. */
        if (isdn_dial(&session->isdn, 0, clear_number) < 0) {
          errprintf("SESSION: Error dialing number '%s'.\n", clear_number);
	} else {
	  /* update dial combo box */
	  session_history_add(session, number);

	  /* caller id update */
	  session->ring_time = time(NULL);
	  cid_add_line(session, CALL_OUT, session->msn, clear_number);

	  /* save caller's and callee's number */
	  free(session->from);
	  session->from = strdup(session->msn);
	  free(session->to);
	  session->to = strdup(clear_number);
	}
      } else {
	show_audio_error_dialog();
      }
    }

    free(clear_number);
    break;

  case STATE_DIALING: /* already dialing! */
    break;
  case STATE_RINGING: /* we want to pick up the phone while it rings */
    if (isdn_pickup(&session->isdn) < 0) {
      errprintf("SESSION: Error answering call.\n");
      session_set_state(session, STATE_READY);
    }
    break;
  case STATE_RINGING_QUIET:
    if (!session_set_state(session, STATE_RINGING)) {
      if (isdn_pickup(&session->isdn) < 0) {
        errprintf("SESSION: Error answering call.\n");
        session_set_state(session, STATE_READY);
      }
    } else {
      if (isdn_pickup(&session->isdn) < 0) {
        errprintf("SESSION: Error rejecting call due to audio problems.\n");
        session_set_state(session, STATE_READY);
      }
      show_audio_error_dialog();
    }
    break;
  case STATE_CONVERSATION: /* channel already working */
    errprintf("SESSION: Non-sense warning: Pick up button pressed in conversation mode\n");
    break;
  case STATE_SERVICE:
    errprintf("SESSION: Non-sense warning: Pick up button pressed in service mode\n");
    break;
  case STATE_PLAYBACK:
    errprintf("SESSION: Non-sense warning: Pick up button pressed in playback mode\n");
    break;
  default:
    errprintf("SESSION: Warning: gtk_handle_pick_up_button: Unknown session state.\n");
  }
}

/*--------------------------------------------------------------------------*/

void gtk_handle_hang_up_button(GtkWidget *widget _U_, gpointer data)
{
  session_t *session = (session_t *) data;

  switch (session->state) {
  case STATE_READY: /* we are already in command mode */
    break;
  case STATE_DIALING:/* abort dialing */
    session->hangup_reason = _("(ABORTED)");
    isdn_hangup(&session->isdn);
    break;
  case STATE_RINGING: /* reject call */
  case STATE_RINGING_QUIET: /* reject call */
    session->hangup_reason = _("(REJECTED)");
    isdn_hangup(&session->isdn);
    break;
  case STATE_CONVERSATION: /* hang up (while b-channel is open) */
    session->hangup_reason = NULL;
    isdn_hangup(&session->isdn);
    break;
  case STATE_SERVICE:
    errprintf("SESSION: Non-sense warning: Hang up button pressed in service mode\n");
    break;
  case STATE_PLAYBACK:
    session_set_state(session, STATE_READY);
    break;
  default:
    errprintf("SESSION: Warning: gtk_handle_hang_up_button: Unknown session state.\n");
    break;
  }
}

/*--------------------------------------------------------------------------*/

static void session_history_normalize(session_t *session)
{
  /* cut size if needed */
  while (g_list_length(session->dial_number_history) >
	 session->dial_number_history_maxlen + 1) {
    free(g_list_nth_data(session->dial_number_history,
			 g_list_length(session->dial_number_history) - 1));
    session->dial_number_history = g_list_remove_link(
	    session->dial_number_history,
	    g_list_last(session->dial_number_history));
  }
  gtk_combo_set_popdown_strings(GTK_COMBO(session->dial_number_box),
				session->dial_number_history);
}

/*--------------------------------------------------------------------------*/

void session_history_add(session_t *session, const char *number) {
  char *temp = strdup(number);

  session->dial_number_history = g_list_insert(
				       session->dial_number_history, temp, 1);
  session_history_normalize(session);
}

/*--------------------------------------------------------------------------*/

void session_history_append(session_t *session, char *number) {
  char *temp = strdup(number);

  session->dial_number_history = g_list_append(
					  session->dial_number_history, temp);
  session_history_normalize(session);
}

/*--------------------------------------------------------------------------*/
