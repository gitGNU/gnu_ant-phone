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

#ifndef _ANT_SESSION_H
#define _ANT_SESSION_H

#include "config.h"

/* regular GNU system includes */
#ifdef HAVE_TERMIOS_H
  #include <termios.h>
#endif
#include <time.h>

/* GTK */
#include <gtk/gtk.h>

#include <alsa/asoundlib.h>

/* own header files */
#include "recording.h"
#include "isdn.h"
#include "thread.h"

#define SESSION_PRESET_SIZE 4


/*!
 * @brief Session states.
 */
enum state_t {
  STATE_READY,          /*!< completely idle */
  STATE_RINGING,        /*!< somebody's calling  */
  STATE_RINGING_QUIET,  /*!< same as above, audio off (device blocked) */
  STATE_DIALING,        /*!< we are dialing out */
  STATE_CONVERSATION,   /*!< we are talking */
  STATE_SERVICE,        /*!< special mode (llcheck) */
  STATE_PLAYBACK,       /*!< sound playback, usually recorded conversation */

  STATE_NUMBER          /*!< dummy to calculate size */
};

/*!
 * @brief Known audio effects.
 */
enum effect_t {
  EFFECT_NONE,     /*!< nothing is played currently */
  EFFECT_RING,     /*!< somebody's calling */
  EFFECT_RINGING,  /*!< waiting for the other end to pick up the phone */
  EFFECT_TEST,     /*!< play test sound (e.g. line level check) */
  EFFECT_TOUCHTONE,/*!< play a touchtone */
  EFFECT_EMPTY,    /*!< don't play anything */
  EFFECT_SOUNDFILE /*!< play sound from file */
};

/*!
 * @brief Audio states.
 */
enum audio_t {
  AUDIO_DISCONNECTED,   /*!< audio is disconnected */
  AUDIO_IDLE,           /*!< audio is connected, but idle */
  AUDIO_EFFECT,         /*!< audio is playing an effect */
  AUDIO_CONVERSATION    /*!< audio is used for conversation */
};

/*!
 * @brief Data needed for setting the session state (the state is the index).
 */
struct state_data_t {
  char *status_bar;     /*!< what to display in status bar */
  char *pick_up_label;  /*!< label for pick-up button */
  int pick_up_state;    /*!< state for pick-up button */
  char *hang_up_label;  /*!< label for hang-up button */
  int hang_up_state;    /*!< state for hang-up button */
};

/*!
 * @brief GUI state data for various session states.
 */
extern struct state_data_t state_data[STATE_NUMBER];

/*!
 * @brief Session data.
 */
typedef struct {
  /* audio device data */
  char *audio_device_name_in;         /*!< name of input audio device */
  char *audio_device_name_out;        /*!< name of output audio device */
  snd_pcm_t *audio_in;                /*!< input audio device PCM handle */
  snd_pcm_t *audio_out;               /*!< output audio device PCM handle */
  enum audio_t audio_state;           /*!< current audio state */
  unsigned int audio_speed_in;        /*!< audio device recording speed */
  unsigned int audio_speed_out;       /*!< audio device playback speed */
  int fragment_size_in;               /*!< audio input fragment sizes in bytes */
  int fragment_size_out;              /*!< audio output fragment sizes in bytes */
  int *format_priorities;             /*!< 0-terminated sorted list of preferred audio formats (ALSA constants) */
  int audio_format_in;                /*!< used audio in format */
  int audio_format_out;               /*!< used audio out format */
  int audio_sample_size_in;           /*!< number of bytes of an input audio sample */
  int audio_sample_size_out;          /*!< number of bytes of an output audio sample */
  isdn_speed_t audio_out_speed;       /*!< actual audio out speed */
  isdn_speed_t audio_in_speed;        /*!< actual audio in speed */
  thread_t thread_audio_input;        /*!< audio data input thread */

  /* ISDN data */
  isdn_t isdn;                        /*!< ISDN handle */
  unsigned int isdn_active;           /*!< flag for active CAPI connection */

  char *from;                         /*!< caller's number */
  char *to;                           /*!< callee's number */
  char *hangup_reason;                /*!< reason for hangup */

  /* mediation data */
  /* Look-up-tables for audio <-> isdn conversion: */
  unsigned char *audio_LUT_in;        /*!< lookup table ISDN -> audio */
  unsigned char *audio_LUT_out;       /*!< lookup table audio -> ISDN */
  unsigned char *audio_LUT_generate;  /*!< lookup table 8 bit unsigned -> ISDN */
  unsigned char *audio_LUT_analyze;   /*!< lookup table ISDN -> 8 bit unsigned */
  short *audio_LUT_alaw2short;        /*!< lookup table unsigned char (alaw) -> short */
  double ratio_in;                    /*!< ratio: audio output rate / ISDN input rate */
  double ratio_out;                   /*!< ratio: ISDN output rate / audio input rate */

  /* recording data */
  struct recorder_t *recorder;        /*!< recorder internal data */

  /* level check data */
  double llcheck_in_state;            /*!< current input value for level check */
  double llcheck_out_state;           /*!< current output value for level check */
  guint gtk_updater_timer_tag;        /*!< GTK timer tag for updating levels */

  remote_call_port_t rem_port;        /*!< remote call port to call functions in session thread */

  /* GUI elements in this session (GTK specific) */
  GtkWidget *main_window;             /*!< the main window (with style ...) */
  GtkWidget *pick_up_button;          /*!< the pick up button to enable / disable */
  GtkWidget *pick_up_label;           /*!< the label on the pick up button */
  GtkWidget *hang_up_button;          /*!< the hang up button to enable / disable */
  GtkWidget *hang_up_label;           /*!< the label on the hang up button */
  GtkWidget *dial_number_box;         /*!< the dial number combo box */
  GList *dial_number_history;         /*!< the last called numbers */
  unsigned int dial_number_history_maxlen;  /*!< how many numbers to remember */
  unsigned int dial_number_history_pointer; /*!< which one to use next if req */
  GtkWidget *status_bar;              /*!< the status bar */
  gint phone_context_id;              /*!< a context for the status bar */
  GtkWidget *audio_warning;           /*!< inside status bar */
  gint audio_context_id;              /*!< a context for audio_warning */

  GtkWidget *llcheck;                 /*!< line level check widget inside status bar */
  GtkWidget *llcheck_in;              /*!< input level meter */
  GtkWidget *llcheck_out;             /*!< output level meter */
  GtkWidget *llcheck_check_menu_item; /*!< state of line levels (status bar) */

  GtkWidget *controlpad;              /*!< key pad etc. */
  GtkWidget *controlpad_check_menu_item; /*!< display state of control pad */
  GtkWidget *mute_button;             /*!< mute toggle button */
  GtkWidget *muted_warning;           /*!< show in status bar if muted */
  gint muted_context_id;              /*!< a context for mute in the status bar */

  GtkWidget *record_checkbutton;      /*!< recording checkbutton */
  GtkWidget *record_checkbutton_local;  /*!< local recording checkbutton */
  GtkWidget *record_checkbutton_remote; /*!< remote recording checkbutton */
  
  /* caller id related */
  GtkWidget *cid;                     /*!< the caller id widget itself (to show/hide) */
  GtkWidget *cid_check_menu_item;     /*!< to handle state of cid monitor (show?) */
  GtkWidget *cid_list;                /*!< the list to hold the individual call data */
  GtkWidget *cid_scrolled_window;     /*!< the home of the clist with adjustments */
  gint cid_num;                       /*!< number of rows in list */
  gint cid_num_max;                   /*!< maximum number of rows in list */
  time_t vcon_time;                   /*!< the start of conversation mode (for duration calc.) */
  time_t ring_time;                   /*!< the first sign of the conversation (dial/ring) */
  /* the symbols for the CList */
  GdkPixmap *symbol_in_pixmap;
  GdkBitmap *symbol_in_bitmap;
  GdkPixmap *symbol_out_pixmap;
  GdkBitmap *symbol_out_bitmap;
  GdkPixmap *symbol_record_pixmap;
  GdkBitmap *symbol_record_bitmap;

  GtkWidget *menuitem_settings;       /*!< Menu items to select / deselect */
  GtkWidget *menuitem_line_check;

  /* ringing etc. */
  thread_t thread_effect;             /*!< effect thread, e.g., for ringing */
  enum effect_t effect;               /*!< which effect is currently been played? */
  unsigned int effect_pos;            /*!< sample position in effect */
  char* effect_filename;              /*!< the file to play back */
  SNDFILE* effect_sndfile;            /*!< sound file handle */
  SF_INFO effect_sfinfo;              /*!< info struct about effect_sndfile */
  time_t effect_playback_start_time;  /*!< start time of playback */
  int touchtone_countdown_isdn;       /*!< number of samples yet to play */
  int touchtone_countdown_audio;      /*!< number of samples yet to play */
  int touchtone_index;                /*!< which touchtone */

  /* phone specific */
  enum state_t state;                 /*!< which state we are currently in */

  char* msn;                          /*!< originating msn, allocated memory! */
  char* msns;                         /*!< comma-separated list of msns to listen on, allocated memory!*/

  int unanswered;                     /*!< unanswered calls for this session */

  /* some options (useful for options file handling) */
  int option_save_options;            /*!< save options on exit */
  int option_release_devices;         /*!< close sound devices while not needed */
  int option_show_llcheck;            /*!< show line level checks in main window  */
  int option_show_callerid;           /*!< show callerid part in main window */
  int option_show_controlpad;         /*!< show control pad (key pad etc.) */
  int option_muted;                   /*!< mute microphone (other party gets zeros) */
  int option_record;                  /*!< record to file */
  int option_record_local;            /*!< record local channel */
  int option_record_remote;           /*!< record remote channel */
  enum recording_format_t option_recording_format; /*!< recording file format */

  int option_calls_merge;             /*!< merge isdnlog */
  int option_calls_merge_max_days;

  char *exec_on_incoming;             /*!< string with command to execute on incoming call */
  int option_popup;                   /*!< push main window to foreground on incoming call */

  char* preset_names[SESSION_PRESET_SIZE];    /*!< names for preset buttons */
  char* preset_numbers[SESSION_PRESET_SIZE];  /*!< numbers for preset buttons */

  int local_sock;                     /*!< unix domain socket for local server functionality */
  guint gtk_local_input_tag;          /*!< GTK tag for GTK main loop select */

} session_t;



/*!
 * @brief Default session.
 */
extern session_t session;



/*!
 * @brief Sets new state in session and GUI (also handles audio state).
 *
 * @param session session to use.
 * @param state new session state (@see state_t).
 * @return 0 on success, -1 otherwise (e.g., can't open audio device).
 */
int session_set_state(session_t *session, enum state_t state);

/*!
 * @brief Activate/deactivate ISDN connection.
 *
 * @param session session to use.
 * @param activate if nonzero, activate ISDN, otherwise deactivate.
 * @return 0 on success, -1 otherwise (e.g., can't open ISDN device).
 */
int session_activate_isdn(session_t *session, unsigned int activate);



/*!
 * @brief Set up GTK I/O handlers.
 *
 * @param session session to use.
 */
void session_io_handlers_start(session_t *session);

/*!
 * @brief Remove GTK handlers.
 *
 * @param session session to use.
 */
void session_io_handlers_stop(session_t *session);



/*!
 * @brief Set audio device state in session.
 *
 * @param session session.
 * @param state requested audio state (@see audio_t).
 * @return 0 on success, -1 otherwise.
 */
int session_set_audio_state(session_t *session, enum audio_t state);

/*!
 * @brief Resets audio devices by closing and reopening.
 *
 * @param session session.
 * @return 0 on success, -1 otherwise.
 *
 * @note Stop I/O handlers first!
 *        Only resets currently used device(s). To use another device,
 *        use session_set_audio_state() with AUDIO_DISCONNECTED to
 *        disconnect the old device first.
 */
int session_reset_audio(session_t *session);

/*!
 * @brief Initialize a session (ISDN and audio devices) and read options file.
 *
 * @param session session, empty, to be filled.
 * @param audio_device_name_in default name of input audio device.
 * @param audio_device_name_out default name of output audio device.
 * @param msn default MSN to use.
 * @param msns default set of MSNs to listen on.
 * @return 0 on success, -1 otherwise.
 *
 * @note The latter 4 parameters are only the defaults. They are normally
 *       overridden by the options file.
 */
int session_init(session_t *session,
		 char *audio_device_name_in,
		 char *audio_device_name_out,
		 char *msn, char *msns);

/*!
 * @brief Clean up a session (ISDN and audio devices).
 *
 * @param session session to clean up.
 * @return 0 on success, -1 otherwise.
 */
int session_deinit(session_t *session);



/*!
 * @brief Start an effect (effect_t) playing on the sound device.
 *
 * If kind == EFFECT_SOUNDFILE, session->effect_filename should be
 * initialized. session_effect_stop() will free() it afterwards.
 *
 * @param session session.
 * @param kind effect kind.
 */
void session_effect_start(session_t *session, enum effect_t kind);

/*!
 * @brief Stop playing an effect.
 *
 * @param session session.
 */
void session_effect_stop(session_t *session);



/*!
 * @brief Start recording on a session.
 *
 * @param session session on which to record.
 * @return 0 on success, -1 otherwise.
 */
int session_start_recording(session_t *session);



/*!
 * @brief Initiates dialing to specified number.
 *
 * Changes contents of dial entry and simulates pick up button.
 *
 * @param session session.
 * @param number number to dial.
 */
void session_make_call(session_t *session, char *number);

/*!
 * @brief Callback from GTK on pick up button clicked.
 *
 * @param widget the button.
 * @param data session.
 */
void gtk_handle_pick_up_button(GtkWidget *widget, gpointer data);

/*!
 * @brief Callback from GTK on hang up button clicked.
 *
 * @note Also called on exit.
 *
 * @param widget the button, NULL when called directly (on exit).
 * @param data session.
 */
void gtk_handle_hang_up_button(GtkWidget *widget, gpointer data);



/*!
 * @brief Add line to history of dial number combo box as first row.
 *
 * Also check maximum size of history.
 *
 * @param session session.
 * @param number number to add (will be copied).
 */
void session_history_add(session_t *session, const char *number);

/*!
 * @brief Add line to history of dial number combo box as last row.
 *
 * Also check maximum size of history.
 *
 * @param session session.
 * @param number number to add (will be copied).
 */
void session_history_append(session_t *session, char *number);

#endif /* session.h */
