/*
 * ISDN handling functions
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

#ifndef _ANT_ISDN_H
#define _ANT_ISDN_H

#include "config.h"
#include "thread.h"
#include "util.h"

/*!
 * @brief 0 (master MSN) or MSN to use as identification on outgoing calls.
 */
#define DEFAULT_MSN "0"

/*!
 * @brief Comma-separated set of MSNs to listen on.
 *
 * "*" (wildcard) to listen on all MSNs
 */
#define DEFAULT_MSNS "*"

#define ISDN_SPEED 8000

/*!
 * @brief Fragment size to send to ISDN device.
 */
#define ISDN_FRAGMENT_SIZE  128

#define ISDN_CONFIG_FILENAME "/etc/isdn/isdn.conf"

extern char* isdn_calls_filename_from_config;

/*!
 * @brief ISDN callbacks to the application.
 *
 * The callbacks described in this structure run within the context
 * of the calling thread. They need to send messages to other threads
 * properly synchronized.
 */
typedef struct {
  /*!
   * @brief Callback when connection established.
   *
   * @param context context given at initialization time.
   * @param number remote party number (may be NULL).
   */
  void (*info_connected)(void *context, char *number);

  /*!
   * @brief Callback when ISDN data received.
   *
   * @param context context given at initialization time.
   * @param data pointer to received data.
   * @param length length of received data (in bytes).
   */
  void (*info_data)(void *context, void *data, unsigned int length);

  /*!
   * @brief Callback when connection disconnected.
   *
   * @param context context given at initialization time.
   */
  void (*info_disconnected)(void *context);

  /*!
   * @brief Callback when connection attempt fails with error.
   *
   * @param context context given at initialization time.
   * @param error CAPI error number.
   */
  void (*info_error)(void *context, unsigned int error);

  /*!
   * @brief Callback called on RING from other side.
   *
   * @param context context given at initialization time.
   * @param callee remote party number (may be NULL).
   * @param called called (our) number (may be NULL).
   */
  void (*info_ring)(void *context, char *callee, char *called);

} isdn_callback_t;

/*!
 * @brief Speed measurmenets.
 */
typedef struct {
  unsigned long samples;/*!< total sample count (after first frame) */
  uint64_t delta;       /*!< how long did it take to send/receive samples */

  uint64_t start;       /*!< input start time in microseconds (for delta computation) */
  uint64_t debug;       /*!< debug timepoint */
} isdn_speed_t;

/*!
 * @brief ISDN connection state.
 */
typedef enum {
  ISDN_IDLE = 0,            /*!< no connection active */
  ISDN_CONNECT_REQ,         /*!< connection request has been sent */
  ISDN_CONNECT_WAIT,        /*!< connection request acknowledged, waiting for connect */
  ISDN_CONNECT_ACTIVE,      /*!< connection active, no data channel (but requested) */
  ISDN_CONNECT_B3_WAIT,     /*!< data channel request confirmed, waiting for connect */
  ISDN_CONNECTED,           /*!< connection is completely established */
  ISDN_DISCONNECT_B3_REQ,   /*!< data disconnect has been requested */
  ISDN_DISCONNECT_B3_WAIT,  /*!< data disconnect confirmed, waiting for actual disconnect */
  ISDN_DISCONNECT_ACTIVE,   /*!< data disconnect done, sent physical channel disconnect req */
  ISDN_DISCONNECT_WAIT,     /*!< channel disconnect req confirmed, waiting for actual disconnect */
  ISDN_RINGING,             /*!< ringing */
  ISDN_INCOMING_WAIT,       /*!< waiting for incoming connect indication */
  ISDN_MAXSTATE
} isdn_state_t;

/*!
 * @brief ISDN handle wrapping CAPI interface.
 */
typedef struct {
  /* NOTE: all parts private! Do not access them directly! */

  unsigned int appl_id;     /*!< CAPI application ID */
  unsigned int msg_no;      /*!< CAPI message serial number */

  isdn_state_t state;       /*!< current connection state */

  unsigned int ctrl_count;  /*!< controller count */
  char *own_msn;            /*!< own MSN (for originating calls) */

  unsigned int info_mask;   /*!< info mask for received info from CAPI */
  unsigned int cip_mask;    /*!< CIP mask for listening on services */

  unsigned int active_plci; /*!< active physical connection PLCI (if off-hook) */
  unsigned int active_ncci; /*!< active logical connection NCCI (if off-hook) */
  char *remote_number;      /*!< remote party number */
  char *local_number;       /*!< local number (currently only for ring) */

  GMutex *lock;             /*!< lock protecting this structure */
  thread_t reply_thread;    /*!< thread for processing ISDN replies */

  isdn_callback_t *callback;/*!< set of callbacks for ISDN replies */
  void *cb_context;         /*!< context to use for callbacks */

  GMutex *data_lock;        /*!< ISDN request/reply lock */

  isdn_speed_t in_speed;    /*!< ISDN data input speed */
} isdn_t;

/*!
 * @brief Open ISDN device via CAPI interface.
 *
 * @param isdn handle to fill in.
 * @param callbacks ISDN callbacks to call for various ISDN events.
 * @param context context for ISDN callbacks.
 * @return 0 on success, less than 0 on error.
 */
int open_isdn_device(isdn_t *isdn, isdn_callback_t *callbacks, void *context);

/*!
 * @brief Close ISDN device.
 *
 * @param isdn handle to close.
 * @return 0 on success, less than 0 on error.
 */
int close_isdn_device(isdn_t *isdn);

/*!
 * @brief Activate/deactivate ISDN connection.
 *
 * @param isdn device handle.
 * @param activate if nonzero, activate ISDN, otherwise deactivate.
 * @return 0 on success, -1 otherwise (e.g., can't open ISDN device).
 */
int activate_isdn_device(isdn_t *isdn, unsigned int active);

/*!
 * @brief Initiate voice call on ISDN device.
 *
 * @param isdn device handle.
 * @param controller ISDN controller number or 0 to use first available.
 * @param number number to call.
 * @return 0 on success, less than 0 on error.
 */
int isdn_dial(isdn_t *isdn, unsigned int controller, char *number);

/*!
 * @brief Hang up current call on ISDN device.
 *
 * @param isdn device handle.
 * @return 0 on success, less than 0 on error.
 */
int isdn_hangup(isdn_t *isdn);

/*!
 * @brief Pick up pending call on ISDN device.
 *
 * @param isdn device handle.
 * @return 0 on success, less than 0 on error.
 */
int isdn_pickup(isdn_t *isdn);

/*!
 * @brief Send data over ISDN connection, after it's established.
 *
 * @param isdn device handle.
 * @param data pointer to data.
 * @param datalen data length.
 */
int isdn_send_data(isdn_t *isdn, unsigned char *data, unsigned int datalen);

/*!
 * @brief Sets originating MSN for the specified ISDN device.
 *
 * @param isdn device handle.
 * @param msn MSN to set ('0' for default).
 * @return 0 on success, -1 otherwise.
 */
int isdn_setMSN(isdn_t *isdn, char *msn);

/*!
 * @brief Sets MSNs to listen on for the specified ISDN device.
 *
 * @param isdn device handle.
 * @param msns comma-separated set of MSNs to listen on ('*' for any).
 * @return 0 on success, -1 otherwise.
 */
int isdn_setMSNs(isdn_t *isdn, char *msns);

/*!
 * @brief Get the name of the calls file from isdnlog.
 *
 * @return file name or NULL on error.
 */
char* isdn_get_calls_filename(void);

/*!
 * @brief Initialize speed measurement structure.
 *
 * @param speed structure to initialize.
 */
void isdn_speed_init(isdn_speed_t *speed);

/*!
 * @brief Add bytes sent/received to measurement structure.
 *
 * @param speed structure to modify.
 * @param samples sample count.
 */
void isdn_speed_addsamples(isdn_speed_t *speed, unsigned int samples);

/*!
 * @brief Debug message for speed.
 *
 * @param speed structure to print out.
 * @param level debug level.
 * @param prefix prefix for message.
 */
void isdn_speed_debug(isdn_speed_t *speed, int level, char *prefix);

#endif /* _ANT_ISDN_H */
