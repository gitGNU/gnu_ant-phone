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

#include "config.h"

/* GNU headers */
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ISDN CAPI header */
#include <capi20.h>

/* own header files */
#include "globals.h"
#include "isdn.h"

static char* calls_filenames[] =
{ "/var/lib/isdn/calls", "/var/log/isdn/calls", "/var/log/isdn.log" };
char* isdn_calls_filename_from_config = NULL;


/*!
 * @brief Initiate listen on ISDN device.
 *
 * @param isdn ISDN device structure.
 * @param controller controller number.
 * @return 0 on success, -1 on error.
 */
static int isdn_listen(isdn_t *isdn, unsigned int controller);

/*!
 * @brief Check if listening on MSN.
 *
 * @param isdn ISDN device structure.
 * @param msn local MSN to check.
 * @return TRUE if listening, FALSE otherwise.
 */
static int isdn_is_listening(isdn_t *isdn, char *msn);

/*!
 * @brief Set local number on ISDN connection object.
 *
 * @param isdn ISDN device structure.
 * @param number local number (in ISDN format).
 */
static void isdn_set_local_number(isdn_t *isdn, char *number);

/*!
 * @brief Set remote number on ISDN connection object.
 *
 * @param isdn ISDN device structure.
 * @param number remote number (in ISDN format).
 */
static void isdn_set_remote_number(isdn_t *isdn, char *number);

/*!
 * @brief Trigger disconnect on ISDN connection.
 *
 * @param isdn ISDN device structure.
 * @return 0 on success, -1 on error.
 */
static int isdn_trigger_disconnect(isdn_t *isdn);

/*!
 * @brief Handle CAPI confirmation message.
 *
 * @param isdn ISDN device structure.
 * @param msg message to process.
 */
static void isdn_handle_confirmation(isdn_t *isdn, _cmsg *msg);

/*!
 * @brief Handle CAPI indication message.
 *
 * @param isdn ISDN device structure.
 * @param msg message to process.
 */
static void isdn_handle_indication(isdn_t *isdn, _cmsg *msg);

/*!
 * @brief ISDN processing thread.
 *
 * @param param ISDN device structure.
 */
static gpointer isdn_reply_thread(gpointer param);

/*--------------------------------------------------------------------------*/

static int isdn_listen(isdn_t *isdn, unsigned int controller)
{
  _cmsg CMSG;  /* structure for the message */
  unsigned int info;

  dbgprintf(2, "CAPI 2.0: LISTEN_REQ ApplID %d msg %d ctrl %d infomsk 0x%x CIPmsk 0x%x\n",
          isdn->appl_id, isdn->msg_no, controller,
          isdn->info_mask, isdn->cip_mask);

  g_mutex_lock(isdn->data_lock);
  info = LISTEN_REQ(&CMSG, isdn->appl_id, isdn->msg_no++, controller,
                     isdn->info_mask, isdn->cip_mask, 0, NULL, NULL);
  g_mutex_unlock(isdn->data_lock);

  if (info != 0) {
    errprintf("CAPI 2.0: LISTEN_REQ failed, RC=0x%x\n", info);
    return -1;
  }
  return 0;
}

/*--------------------------------------------------------------------------*/

static void isdn_set_remote_number(isdn_t *isdn, char *number)
{
  char *tmp, *tofree = isdn->remote_number;

  if (number) {
    /* Number format:
     * Byte 0: length of structure
     * Byte 1: numbering plan
     * Byte 2: presentation indicator (0x80 standard, 0xA0 for CLIR)
     * Byte 3..n: number digits
     */
    int len = number[0] - 2;
    if (len <= 0) {
      isdn->remote_number = 0;
    } else {
      tmp = (char*) malloc(len + 1);
      memcpy(tmp, number + 3, len);
      tmp[len] = 0;
      isdn->remote_number = tmp;
    }
  } else {
    isdn->remote_number = 0;
  }

  if (tofree)
    free(tofree);
}

/*--------------------------------------------------------------------------*/

static void isdn_set_local_number(isdn_t *isdn, char *number)
{
  char *tmp, *tofree = isdn->local_number;

  if (number) {
    /* Number format:
     * Byte 0: length of structure
     * Byte 1: numbering plan
     * Byte 2..n: number digits
     */
    int len = number[0] - 1;
    if (len <= 0) {
      isdn->local_number = 0;
    } else {
      tmp = (char*) malloc(len + 1);
      memcpy(tmp, number + 2, len);
      tmp[len] = 0;
      isdn->local_number = tmp;
    }
  } else {
    isdn->local_number = 0;
  }

  if (tofree)
    free(tofree);
}

/*--------------------------------------------------------------------------*/

static int isdn_is_listening(isdn_t *isdn, char *msn)
{
  char *cur, *p, *end, *tocheck;
  int wildcard, msnlen;

  if (!msn || !*msn || strcmp(msn, "0") == 0 || !isdn->listen_msns) {
    /* empty MSN or no MSNS set, accept */
    return TRUE;
  }

  p = isdn->listen_msns;
  tocheck = isdn->local_number;
  msnlen = strlen(tocheck);

  dbgprintf(1, "Checking MSN '%s' against listen set '%s'\n", tocheck, p);

  for (;;) {
    /* process next entry */

    /* position to next MSN */
    while (*p && (*p == ',' || *p == ';' || isspace(*p)))
      ++p;
    if (!*p)
      break;

    cur = p;

    /* find end of MSN in listen set */
    while (*p && *p != ',' && *p != ';')
      ++p;
    end = p;

    while (end > cur && isspace(*(end-1)))
      --end;

    if (end == cur)
      continue;   /* empty entry */

    /* OK, entry is between cur and end */
    if (*(end-1) == '*') {
      wildcard = 1;
      --end;
    } else {
      wildcard = 0;
    }

    /* check prefix */
    if (msnlen < (end - cur))
      continue;     /* MSN too short, cannot match */
    if (strncmp(tocheck, cur, (end-cur)) != 0)
      continue;     /* prefix doesn't match */

    if (wildcard)
      return TRUE;  /* prefix matched, wildcard allowed */
    if (msnlen == (end-cur))
      return TRUE;  /* exact match */
  }

  /* no matching entry found */
  return FALSE;
}

/*--------------------------------------------------------------------------*/

static int isdn_trigger_disconnect(isdn_t *isdn)
{
  unsigned int info;
  int result = 0;
  _cmsg CMSG;  /* structure for the message */

  switch (isdn->state) {
    case ISDN_CONNECT_WAIT:
    case ISDN_CONNECT_ACTIVE:
    case ISDN_DISCONNECT_B3_REQ:
    case ISDN_DISCONNECT_B3_WAIT:
    case ISDN_INCOMING_WAIT:
      /* no data channel yet or no reply to data disconnect, do physical disconnect */
      {
        dbgprintf(1, "CAPI 2.0: DISCONNECT_REQ ApplID %d plci 0x%x\n",
                  isdn->appl_id, isdn->active_plci);

        g_mutex_lock(isdn->data_lock);
        info = DISCONNECT_REQ(&CMSG, isdn->appl_id, isdn->msg_no++,
                              isdn->active_plci,  /* physical connection ID */
                              0, 0, 0, 0 /* additional info */);
        g_mutex_unlock(isdn->data_lock);

        if (info != 0) {
          errprintf("CAPI 2.0: DISCONNECT_REQ failed, RC=0x%x\n", info);
          isdn->state = ISDN_IDLE;
          isdn->callback->info_error(isdn->cb_context, info);
          result = -1;
        } else {
          isdn->state = ISDN_DISCONNECT_ACTIVE;
        }
      }
      break;

    case ISDN_CONNECT_B3_WAIT:
    case ISDN_CONNECTED:
      /* both data and physical connection active, tear down data channel */
      {
        dbgprintf(1, "CAPI 2.0: DISCONNECT_B3_REQ ApplID %d ncci 0x%x\n",
                  isdn->appl_id, isdn->active_ncci);

        g_mutex_lock(isdn->data_lock);
        info = DISCONNECT_B3_REQ(&CMSG, isdn->appl_id, isdn->msg_no++,
                                 isdn->active_ncci,  /* logical connection ID */
                                 NULL /* NCPI */);
        g_mutex_unlock(isdn->data_lock);

        if (info != 0) {
          errprintf("CAPI 2.0: DISCONNECT_B3_REQ failed, RC=0x%x\n", info);

          /* retry with disconnect on whole connection */
          dbgprintf(1, "CAPI 2.0: DISCONNECT_REQ ApplID %d plci 0x%x\n",
                    isdn->appl_id, isdn->active_plci);

          g_mutex_lock(isdn->data_lock);
          info = DISCONNECT_REQ(&CMSG, isdn->appl_id, isdn->msg_no++,
                                 isdn->active_plci,  /* physical connection ID */
                                 0, 0, 0, 0 /* additional info */);
          g_mutex_unlock(isdn->data_lock);

          if (info != 0) {
            errprintf("CAPI 2.0: DISCONNECT_REQ failed, RC=0x%x\n", info);
            isdn->state = ISDN_IDLE;
            isdn->callback->info_error(isdn->cb_context, info);
            result = -1;
          } else {
            isdn->state = ISDN_DISCONNECT_ACTIVE;
          }
        } else {
          isdn->state = ISDN_DISCONNECT_B3_REQ;
        }
      }
      break;

    case ISDN_RINGING:
      /* reject the call */
      {
        dbgprintf(2, "CAPI 2.0: CONNECT_RESP ApplID %d msgno %d plci 0x%x reject %d\n",
                  isdn->appl_id, isdn->msg_no, isdn->active_plci, 3);

        g_mutex_lock(isdn->data_lock);
        info = CONNECT_RESP(&CMSG, isdn->appl_id, isdn->msg_no++,
                            isdn->active_plci, 3 /* reject */,
                            0 /* B1protocol: default */,
                            0 /* B2protocol: default */,
                            0 /* default B3protocol */,
                            0 /* default B1configuration */,
                            0 /* default B2configuration */,
                            0 /* default B3configuration */,
                            NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL /* additional info */);
        g_mutex_unlock(isdn->data_lock);

        isdn->state = ISDN_IDLE;
        if (info != 0) {
          errprintf("CAPI 2.0: CONNECT_RESP failed, RC=0x%x\n", info);
          isdn->callback->info_error(isdn->cb_context, info);
        } else {
          isdn->callback->info_disconnected(isdn->cb_context);
        }
      }
      break;

    default:
      errprintf("ISDN in unexpected state %d on disconnect\n", isdn->state);
      result = -1;
      break;
  }

  return result;
}

/*--------------------------------------------------------------------------*/

static void isdn_handle_confirmation(isdn_t *isdn, _cmsg *msg)
{
  unsigned int info, plci, ncci, controller;

  switch (msg->Command) {

    case CAPI_ALERT:
      /* ALERT message */
      {
        plci = ALERT_CONF_PLCI(msg);
        info = ALERT_CONF_INFO(msg);

        dbgprintf(2, "CAPI 2.0: ALERT_CONF ApplID %d plci 0x%x info 0x%x\n",
                  isdn->appl_id, plci, info);

        if (info != 0) {
          /* connection error */
          isdn->state = ISDN_IDLE;
        } else {
          /* may ring now */
          isdn->callback->info_ring(isdn->cb_context, isdn->remote_number, isdn->local_number);
        }
      }
      break;

    case CAPI_CONNECT:
      /* physical channel connection is being established */
      {
        plci = CONNECT_CONF_PLCI(msg);
        info = CONNECT_CONF_INFO(msg);

        dbgprintf(2, "CAPI 2.0: CONNECT_CONF ApplID %d plci 0x%x info 0x%x\n",
                  isdn->appl_id, plci, info);

        if (info != 0) {
          /* connection error */
          isdn->state = ISDN_IDLE;
          isdn->callback->info_error(isdn->cb_context, info);
        } else {
          /* CONNECT_ACTIVE_IND comes later, when connection actually established */
          isdn->state = ISDN_CONNECT_WAIT;
          isdn->active_plci = plci;
        }
      }
      break;

    case CAPI_CONNECT_B3:
      /* logical connection is being established */
      {
        ncci = CONNECT_B3_CONF_NCCI(msg);
        info = CONNECT_B3_CONF_INFO(msg);

        dbgprintf(2, "CAPI 2.0: CONNECT_B3_CONF ApplID %d ncci 0x%x info 0x%x\n",
                  isdn->appl_id, ncci, info);

        if (isdn->state == ISDN_CONNECT_ACTIVE) {
          if (info != 0) {
            /* connection error */
            isdn->callback->info_error(isdn->cb_context, info);
            isdn_trigger_disconnect(isdn);
          } else {
            /* CONNECT_B3_ACTIVE_IND comes later, when connection actually established */
            isdn->active_ncci = ncci;
            isdn->state = ISDN_CONNECT_B3_WAIT;
          }
        } else {
          /* wrong connection state for B3 connect, trigger disconnect */
          isdn_trigger_disconnect(isdn);
        }
      }
      break;

    case CAPI_SELECT_B_PROTOCOL:
      /* currently unused, response to SELECT_B_PROTOCOL_REQ while connection established */
      break;

    case CAPI_LISTEN:
      /* LISTEN confirmation */
      {
        info = LISTEN_CONF_INFO(msg);
        controller = LISTEN_CONF_CONTROLLER(msg);

        dbgprintf(2, "CAPI 2.0: LISTEN_CONF ApplID %d controller %d info 0x%x\n",
                  isdn->appl_id, controller, info);
      }
      break;

    case CAPI_DISCONNECT_B3:
      /* data channel disconnect initiated */
      {
        ncci = DISCONNECT_B3_CONF_NCCI(msg);
        info = DISCONNECT_B3_CONF_INFO(msg);

        dbgprintf(2, "CAPI 2.0: DISCONNECT_B3_CONF ApplID %d ncci 0x%x info 0x%x\n",
                  isdn->appl_id, ncci, info);

        if (info != 0) {
          /* error, most probably NCCI not known */
          isdn->callback->info_error(isdn->cb_context, info);
          isdn->state = ISDN_DISCONNECT_B3_WAIT;
          isdn_trigger_disconnect(isdn);
        } else {
          /* DISCONNECT_B3_ACTIVE_IND comes later, when connection actually closed */
          isdn->state = ISDN_DISCONNECT_B3_WAIT;
        }
      }
      break;

    case CAPI_DISCONNECT:
      /* physical channel disconnect initiated */
      {
        plci = DISCONNECT_CONF_PLCI(msg);
        info = DISCONNECT_CONF_INFO(msg);

        dbgprintf(2, "CAPI 2.0: DISCONNECT_CONF ApplID %d plci 0x%x info 0x%x\n",
                  isdn->appl_id, plci, info);

        if (info != 0) {
          /* connection error */
          isdn->state = ISDN_IDLE;
          isdn->callback->info_error(isdn->cb_context, info);
        } else {
          /* DISCONNECT_ACTIVE_IND comes later, when connection actually closed */
          isdn->state = ISDN_DISCONNECT_WAIT;
        }
      }
      break;

    case CAPI_DATA_B3:
      /* sent data acknowledged, NOP */
      break;

    case CAPI_FACILITY:
      /* TODO */
      break;
  }
}

/*--------------------------------------------------------------------------*/

static void isdn_handle_indication(isdn_t *isdn, _cmsg *msg)
{
  unsigned int info, plci, ncci, flags, datalen, datahandle, cip, reject;
  char *number, *called;
  _cstruct ncpi;
  void *data;

  switch (msg->Command) {
    case CAPI_CONNECT:
      /* connect indication when called from remote phone */
      {
        plci = CONNECT_IND_PLCI(msg);
        cip = CONNECT_IND_CIPVALUE(msg);

        number = (char*) CONNECT_IND_CALLINGPARTYNUMBER(msg);
        called = (char*) CONNECT_IND_CALLEDPARTYNUMBER(msg);

        dbgprintf(2, "CAPI 2.0: CONNECT_IND ApplID %d plci 0x%x cip %d\n",
                  isdn->appl_id, plci, cip);

        reject = 0;
        if (cip != 16 && cip != 1 && cip != 4) {
          /* not telephony */
          reject = 1; /* ignore */
        } else if (isdn->state != ISDN_IDLE) {
          reject = 3; /* user busy */
        } else {
          /* check called number, if in listening MSN set */
          isdn_set_remote_number(isdn, number);
          isdn_set_local_number(isdn, called);
          if (!isdn_is_listening(isdn, isdn->local_number))
            reject = 1; /* ignore here */
        }

        if (!reject) {
          /* may ring now */
          isdn->active_plci = plci;

          /* tell the network, we are interested in the call and ring */
          dbgprintf(2, "CAPI 2.0: ALERT_REQ ApplID %d msgno %d plci 0x%x\n",
                    isdn->appl_id, isdn->msg_no, plci);

          g_mutex_lock(isdn->data_lock);
          info = ALERT_REQ(msg, isdn->appl_id, isdn->msg_no++, plci,
                           NULL, NULL, NULL, NULL, NULL);
          g_mutex_unlock(isdn->data_lock);

          if (info == 0) {
            isdn->state = ISDN_RINGING;
            isdn->active_plci = plci;
          } else {
            errprintf("CAPI 2.0: ALERT_REQ failed, RC=0x%x, rejecting call\n", info);
            reject = 3;
          }
        }

        if (reject) {
          /* answer the info message immediately */
          dbgprintf(2, "CAPI 2.0: CONNECT_RESP ApplID %d msgno %d plci 0x%x reject %d\n",
                    isdn->appl_id, isdn->msg_no, plci, reject);

          g_mutex_lock(isdn->data_lock);
          CONNECT_RESP(msg, isdn->appl_id, isdn->msg_no++,
                      plci, reject,
                      0 /* B1protocol: default */,
                      0 /* B2protocol: default */,
                      0 /* default B3protocol */,
                      0 /* default B1configuration */,
                      0 /* default B2configuration */,
                      0 /* default B3configuration */,
                      NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL /* additional info */);
          g_mutex_unlock(isdn->data_lock);
        }
      }
      break;

    case CAPI_CONNECT_ACTIVE:
      /* connection is now active */
      {
        plci = CONNECT_ACTIVE_IND_PLCI(msg);
        number = (char*) CONNECT_ACTIVE_IND_CONNECTEDNUMBER(msg);

        dbgprintf(2, "CAPI 2.0: CONNECT_ACTIVE_IND ApplID %d plci 0x%x\n",
                  isdn->appl_id, plci);

        if (plci != isdn->active_plci) {
          /* connect on wrong PLCI??? */
          errprintf("CAPI 2.0: CONNECT_ACTIVE_IND wrong plci 0x%x, expected 0x%x\n",
                    plci, isdn->active_plci);

          g_mutex_lock(isdn->data_lock);
          CONNECT_ACTIVE_RESP(msg, isdn->appl_id, isdn->msg_no++, plci);
          g_mutex_unlock(isdn->data_lock);
        } else {
          if (isdn->state != ISDN_INCOMING_WAIT) {
            isdn_set_remote_number(isdn, number);
          }

          /* answer the info message */
          g_mutex_lock(isdn->data_lock);
          CONNECT_ACTIVE_RESP(msg, isdn->appl_id, isdn->msg_no++, plci);
          g_mutex_unlock(isdn->data_lock);

          if (isdn->state == ISDN_INCOMING_WAIT) {
            /* B-channel will be established by remote side */
            isdn->state = ISDN_CONNECT_ACTIVE;
          } else {
            /* request connection for B-channel */
            dbgprintf(2, "CAPI 2.0: CONNECT_B3_REQ ApplID %d msgno %d plci 0x%x\n",
                      isdn->appl_id, isdn->msg_no, isdn->active_plci);

            g_mutex_lock(isdn->data_lock);
            info = CONNECT_B3_REQ(msg, isdn->appl_id, isdn->msg_no++, isdn->active_plci, NULL);
            g_mutex_unlock(isdn->data_lock);

            if (info != 0) {
              /* connection error */
              errprintf("CAPI 2.0: CONNECT_B3_REQ failed, RC=0x%x\n", info);
              isdn->callback->info_error(isdn->cb_context, info);
              isdn_set_remote_number(isdn, 0);
              /* initiate hangup on PLCI */
              isdn_trigger_disconnect(isdn);
            } else {
              /* wait for CONNECT_B3, then announce result to application via callback */
              isdn->state = ISDN_CONNECT_ACTIVE;
            }
          }
        }
      }
      break;

    case CAPI_CONNECT_B3_ACTIVE:
      /* B-channel connection is now active, connection complete */
      {
        ncci = CONNECT_B3_ACTIVE_IND_NCCI(msg);

        if (ncci != isdn->active_ncci) {
          /* connect on wrong NCCI??? */
          errprintf("CAPI 2.0: CONNECT_B3_ACTIVE_IND wrong ncci 0x%x, expected %d\n",
                    ncci, isdn->active_ncci);
        } else {
          dbgprintf(2, "CAPI 2.0: CONNECT_B3_ACTIVE_IND ApplID %d msgno %d ncci 0x%x\n",
                    isdn->appl_id, isdn->msg_no, ncci);

          /* answer the info message */
          g_mutex_lock(isdn->data_lock);
          CONNECT_B3_ACTIVE_RESP(msg, isdn->appl_id, isdn->msg_no++, ncci);
          g_mutex_unlock(isdn->data_lock);
          isdn->state = ISDN_CONNECTED;

          /* notify application about successful call establishment */
          isdn_speed_init(&isdn->in_speed);
          isdn->callback->info_connected(isdn->cb_context, isdn->remote_number);
        }
      }
      break;

    case CAPI_DISCONNECT:
      /* connection completely released */
      {
        plci = DISCONNECT_IND_PLCI(msg);
        info = DISCONNECT_IND_REASON(msg);

        dbgprintf(2, "CAPI 2.0: DISCONNECT_IND ApplID %d msgno %d plci 0x%x reason 0x%x\n",
                  isdn->appl_id, isdn->msg_no, plci, info);

        /* answer the info message */
        g_mutex_lock(isdn->data_lock);
        DISCONNECT_RESP(msg, isdn->appl_id, isdn->msg_no++, plci);
        g_mutex_unlock(isdn->data_lock);

        if (plci != isdn->active_plci) {
          /* disconnect on wrong PLCI??? */
          errprintf("CAPI 2.0: DISCONNECT_IND wrong PLCI %d, expected %d\n",
                    plci, isdn->active_plci);
        } else {
          isdn->state = ISDN_IDLE;
          isdn->active_ncci = 0;
          isdn->active_plci = 0;

          if ((info & 0xff00) == 0x3400) {
            /* network provides reason in lower byte */
            switch (info) {
              case 0x3400:
              case 0x3480:
              case 0x3490:
              case 0x349f:
                /* normal connection close */
                info = 0;
                break;
            }
          }

          /* notify application */
          if (info != 0) {
            isdn->callback->info_error(isdn->cb_context, info);
          } else {
            isdn->callback->info_disconnected(isdn->cb_context);
          }
        }
      }
      break;

    case CAPI_DISCONNECT_B3:
      /* B-channel connection is now disconnected, connection terminating */
      {
        ncci = DISCONNECT_B3_IND_NCCI(msg);
        info = DISCONNECT_B3_IND_REASON_B3(msg);

        dbgprintf(2, "CAPI 2.0: DISCONNECT_B3_IND ApplID %d msgno %d ncci 0x%x reason 0x%x\n",
                  isdn->appl_id, isdn->msg_no, ncci, info);

        /* answer the info message */
        g_mutex_lock(isdn->data_lock);
        DISCONNECT_B3_RESP(msg, isdn->appl_id, isdn->msg_no++, ncci);
        g_mutex_unlock(isdn->data_lock);

        if (ncci != isdn->active_ncci) {
          /* disconnect on wrong NCCI??? */
          errprintf("CAPI 2.0: DISCONNECT_B3_IND wrong ncci 0x%x, expected 0x%x\n",
                    ncci, isdn->active_ncci);
        } else {
          isdn->active_ncci = 0;
          if (isdn->state == ISDN_CONNECTED || isdn->state == ISDN_CONNECT_B3_WAIT) {
            /* passive disconnect, DISCONNECT_IND comes later */
            isdn->state = ISDN_DISCONNECT_ACTIVE;
          } else {
            /* active disconnect, needs to send DISCONNECT_REQ */
            isdn_trigger_disconnect(isdn);
          }
        }
      }
      break;

    case CAPI_DATA_B3:
      /* data arrived */
      {
        ncci = DATA_B3_IND_NCCI(msg);
        data = DATA_B3_IND_DATA(msg);
        datalen = DATA_B3_IND_DATALENGTH(msg);
        datahandle = DATA_B3_IND_DATAHANDLE(msg);
        flags = DATA_B3_IND_FLAGS(msg);

        dbgprintf(flags ? 2 : 3, "CAPI 2.0: DATA_B3_IND ApplID %d msgno %d ncci 0x%x data 0x%lx+%d flags 0x%x\n",
                  isdn->appl_id, isdn->msg_no, ncci, (long) data, datalen, flags);

        isdn_speed_addsamples(&isdn->in_speed, datalen);

        /* TODO: process flags */
        isdn->callback->info_data(isdn->cb_context, data, datalen);

        /* answer the info message */
        g_mutex_lock(isdn->data_lock);
        DATA_B3_RESP(msg, isdn->appl_id, isdn->msg_no++, ncci, datahandle);
        g_mutex_unlock(isdn->data_lock);

        if (debug > 1) {
          isdn_speed_debug(&isdn->in_speed, 2, "CAPI 2.0: in");
        }
      }
      break;

    case CAPI_CONNECT_B3:
      /* connect indication from remote side */
      {
        ncci = CONNECT_B3_IND_NCCI(msg);
        ncpi = CONNECT_B3_IND_NCPI(msg);

        dbgprintf(3, "CAPI 2.0: CONNECT_B3_IND ApplID %d msgno %d ncci 0x%x\n",
                  isdn->appl_id, isdn->msg_no, ncci);

        /* answer the info message */
        g_mutex_lock(isdn->data_lock);
        CONNECT_B3_RESP(msg, isdn->appl_id, isdn->msg_no++, ncci, 0, ncpi);
        g_mutex_unlock(isdn->data_lock);

        if (isdn->state == ISDN_CONNECT_ACTIVE) {
          /* CONNECT_B3_ACTIVE_IND comes later, when connection actually established */
          isdn->active_ncci = ncci;
          isdn->state = ISDN_CONNECT_B3_WAIT;
        } else {
          /* wrong connection state for B3 connect, trigger disconnect */
          isdn_trigger_disconnect(isdn);
        }
      }
      break;

    case CAPI_FACILITY:
    case CAPI_INFO:
      break;
  }
}

/*--------------------------------------------------------------------------*/

static gpointer isdn_reply_thread(gpointer param)
{
  isdn_t *isdn = (isdn_t*) param;
  _cmsg msg;
  unsigned int info;

  /* timeout is needed, since CAPI release doesn't release waitformessage as it should */
  struct timeval timeout;

  while (!thread_is_stopping(&isdn->reply_thread)) {
    /* process CAPI messages and call callbacks */
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    info = capi20_waitformessage(isdn->appl_id, &timeout);

    if (info != CapiNoError) {
      if (isdn->appl_id == 0) {
        /* ISDN inactive, retry later */
        sleep(1);
      }
      continue;
    }

    g_mutex_lock(isdn->data_lock);

    info = CAPI_GET_CMSG(&msg, isdn->appl_id);

    g_mutex_unlock(isdn->data_lock);

    g_mutex_lock(isdn->lock);

    switch (info) {
      case CapiNoError:
        /* process the message */
        switch (msg.Subcommand) {
          case CAPI_CONF:
            /* confirmation message */
            isdn_handle_confirmation(isdn, &msg);
            break;

          case CAPI_IND:
            /* indication message */
            isdn_handle_indication(isdn, &msg);
            break;
        }
        break;

      case CapiReceiveQueueEmpty:
        errprintf("CAPI 2.0: Empty queue, even if message pending\n");
        break;

      default:
        /* error */
        errprintf("CAPI 2.0: Error while receiving next message, stopping ISDN, RC=0x%x\n", info);
        isdn->reply_thread.stop_flag = 1;
        break;
    }

    g_mutex_unlock(isdn->lock);
  }

  return 0;
}

/*--------------------------------------------------------------------------*/

int open_isdn_device(isdn_t *isdn, isdn_callback_t *callbacks, void *context)
{
  unsigned int info;

  unsigned char buf[64];
  unsigned int numControllers, i, appl_id;
  unsigned int bChannels, dtmf, fax, faxExt, suppServ, transp;
  _cdword buf2[4];

  memset(isdn, 0, sizeof(isdn_t));

  info = CAPI20_ISINSTALLED();
  if (info != 0) {
    errprintf("CAPI 2.0: not installed, RC=0x%x\n", info);
    return -1;
  }

  isdn->lock = g_mutex_new();
  if (!isdn->lock) {
    errprintf("Cannot allocate ISDN mutex\n");
    return -1;
  }
  isdn->data_lock = g_mutex_new();
  if (!isdn->data_lock) {
    g_mutex_free(isdn->lock);
    isdn->lock = 0;
    errprintf("Cannot allocate data mutex\n");
    return -1;
  }

  info = CAPI20_GET_PROFILE (0, buf);
  if (info != 0) {
    errprintf("CAPI 2.0: error getting profile, RC=0x%x\n", info);
    return -1;
  }
  numControllers = buf[0] + (buf[1] << 8);

  if (numControllers == 0) {
    errprintf("CAPI 2.0: No ISDN controllers installed\n");
    return -1;
  }

  if (debug) {
    dbgprintf(1, "CAPI 2.0: Controllers found: %d\n", numControllers);
    if (capi20_get_manufacturer(0,buf)) {
      dbgprintf(1, "CAPI 2.0: Manufacturer: %s\n", buf);
    }
    if (capi20_get_version(0, (unsigned char *) buf2)) {
      dbgprintf(1, "CAPI 2.0: Version: %d.%d/%d.%d\n",
              buf2[0], buf2[1], buf2[2], buf2[3]);
    }
  }


  for (i = 1; i <= numControllers; ++i)
  {
    if (debug) {
      if (capi20_get_manufacturer(i, buf)) {
        dbgprintf(1, "CAPI 2.0: Controller %d: Manufacturer: %s\n", i, buf);
      }
      if (capi20_get_version(i, (unsigned char *) buf2)) {
        dbgprintf(1, "CAPI 2.0: Controller %d: Version: %d.%d/%d.%d\n",
                i, buf2[0], buf2[1], buf2[2], buf2[3]);
      }
    }

    info = CAPI20_GET_PROFILE(i, buf);
    if (info != 0) {
      errprintf("CAPI 2.0: error getting controller %d profile, RC=0x%x\n",
              i, info);
      return -1;
    }

    bChannels = buf[2] + (buf[3]<<8);

    if (buf[4] & 0x08)
      dtmf = 1;
    else
      dtmf = 0;

    if (buf[4] & 0x10)
      suppServ = 1;
    else
      suppServ = 0;

    if (buf[8] & 0x02 && buf[12] & 0x02 && buf[16] & 0x01)
      transp = 1;
    else
      transp = 0;

    if (buf[8] & 0x10 && buf[12] & 0x10 && buf[16] & 0x10)
      fax = 1;
    else
      fax = 0;

    if (buf[8] & 0x10 && buf[12] & 0x10 && buf[16] & 0x20)
      faxExt = 1;
    else
      faxExt = 0;

    dbgprintf(1, "CAPI 2.0: Bchan %d, DTMF %d, FAX %d/%d, transp %d, suppServ %d\n",
            bChannels, dtmf, fax, faxExt, transp, suppServ);
  }

  info = capi20_register(2 /*maxLogicalConnection*/,
                         7 /*maxBDataBlocks*/,
                         2 * ISDN_FRAGMENT_SIZE /*maxBDataLen*/,
                         &appl_id);

  if (appl_id == 0 || info != 0) {
    errprintf("CAPI 2.0: Error registering application, RC=0x%x\n", info);
    return -1;
  }
  dbgprintf(1, "CAPI 2.0: Received application ID %d\n", appl_id);

  isdn->appl_id = appl_id;
  isdn->ctrl_count = numControllers;
  isdn->callback = callbacks;
  isdn->cb_context = context;
  isdn->msg_no = 0;
  thread_init(&isdn->reply_thread);

  /* INFO and CIP masks as defined in Chapter 5.37 of CAPI 2.0 specs */

  /* call progression */
  isdn->info_mask = 0x10;
  /* speech, 3,1kHz audio, telephony */
  isdn->cip_mask = 0x00010012;
  /* telephony only */
  /*isdn->cip_mask = 0x00010000;*/
  /* all services would be: 0x1FFF03FF */

  /* activate listening on all controllers */
  for (i = 1; i <= numControllers; ++i) {
    /* TODO: listen only if the controller has voice capability */
    if (isdn_listen(isdn, i) < 0) {
      errprintf("CAPI 2.0: Error listening on controller %d\n", i);
      return -1;
    }
  }

  thread_start(&isdn->reply_thread, isdn_reply_thread, isdn);

  return 0;
}

/*--------------------------------------------------------------------------*/

int close_isdn_device(isdn_t *isdn)
{
  unsigned int info;
  int result = 0;

  if (isdn->appl_id != 0) {
    info = capi20_release(isdn->appl_id);
    if (info != 0) {
      errprintf("CAPI 2.0: Error releasing ISDN controller, RC=0x%x\n", info);
      result = -1;
    }
  }

  thread_stop(&isdn->reply_thread);

  isdn->appl_id = 0;
  if (isdn->own_msn) {
    free(isdn->own_msn);
    isdn->own_msn = 0;
  }
  if (isdn->listen_msns) {
    free(isdn->listen_msns);
    isdn->listen_msns = 0;
  }
  if (isdn->lock) {
    g_mutex_free(isdn->lock);
    isdn->lock = 0;
  }

  return result;
}

/*--------------------------------------------------------------------------*/

int activate_isdn_device(isdn_t *isdn, unsigned int active)
{
  unsigned int info, appl_id, numControllers, i;
  unsigned char buf[64];
  int result = 0;

  dbgprintf(1, "CAPI 2.0: activate %d\n", active);
  if (active) {
    /* activate */
    if (isdn->appl_id == 0) {
      info = CAPI20_GET_PROFILE (0, buf);
      if (info != 0) {
        errprintf("CAPI 2.0: error getting profile, RC=0x%x\n", info);
        result = -1;
      } else {
        numControllers = buf[0] + (buf[1] << 8);

        info = capi20_register(2 /*maxLogicalConnection*/,
                              7 /*maxBDataBlocks*/,
                              2 * ISDN_FRAGMENT_SIZE /*maxBDataLen*/,
                              &appl_id);

        if (appl_id == 0 || info != 0) {
          errprintf("CAPI 2.0: Error registering application, RC=0x%x\n", info);
          return -1;
        }
        dbgprintf(1, "CAPI 2.0: Received application ID %d\n", appl_id);
        isdn->appl_id = appl_id;

        /* activate listening on all controllers */
        for (i = 1; i <= numControllers; ++i) {
          /* TODO: listen only if the controller has voice capability */
          if (isdn_listen(isdn, i) < 0) {
            errprintf("CAPI 2.0: Error listening on controller %d\n", i);
            return -1;
          }
        }
      }
    }
  } else {
    /* deactivate */
    if (isdn->appl_id) {
      info = capi20_release(isdn->appl_id);
      if (info != 0) {
        errprintf("CAPI 2.0: Error releasing ISDN controller, RC=0x%x\n", info);
        result = -1;
      }
      isdn->appl_id = 0;
    }
  }

  return result;
}

/*--------------------------------------------------------------------------*/

int isdn_dial(isdn_t *isdn, unsigned int controller, char *number)
{
  _cmsg CMSG;  /* structure for the message */
  unsigned int info, msgno;
  int result = 0;
  char *called_nr, *calling_nr;

  g_mutex_lock(isdn->lock);

  if (isdn->state != ISDN_IDLE) {
    errprintf("ISDN connection or disconnect in progress, cannot dial (state %d)\n", isdn->state);
    result = -1;
  } else {
    msgno = isdn->msg_no++;

    if (controller == 0) {
      // TODO: pick proper controller which has voice capability
      controller = 1;
    }

    dbgprintf(1, "CAPI 2.0: CONNECT_REQ ApplID %d ctrl %d CIP %d Called %s\n",
            isdn->appl_id, controller, 16, number);

    called_nr = (char*) malloc(strlen(number) + 3);
    if (!called_nr) {
      errprintf("Cannot allocate memory for called number\n");
      return -1;
    }
    called_nr[0] = strlen(number) + 1;
    called_nr[1] = 0x80;
    strcpy(called_nr + 2, number);

    if (isdn->own_msn) {
      calling_nr = (char*) malloc(strlen(isdn->own_msn) + 4);
      if (calling_nr) {
        calling_nr[0] = strlen(isdn->own_msn) + 2;
        calling_nr[1] = 0x00;
        calling_nr[2] = 0x80; /* NOTE 0xA0 to disable displaying on remote side */
        strcpy(calling_nr + 3, isdn->own_msn);
      }
    } else {
      calling_nr = 0;
    }

    g_mutex_lock(isdn->data_lock);
    info = CONNECT_REQ(&CMSG, isdn->appl_id, msgno, controller,
                        (unsigned short) 16 /* CIP: telephony */,
                        (unsigned char*) called_nr /* called party number */,
                        (unsigned char*) calling_nr /* calling party number */,
                        0 /* called party subaddress */,
                        0 /* calling party subaddress */,
                        1 /* B1protocol: DTE (originate) */,
                        1 /* B2protocol: transparent */,
                        0 /* default B3protocol */,
                        0 /* default B1configuration */,
                        0 /* default B2configuration */,
                        0 /* default B3configuration */,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL /* additional info */);
    g_mutex_unlock(isdn->data_lock);

    free(called_nr);
    if (calling_nr)
      free(calling_nr);

    if (info == 0) {
      isdn->state = ISDN_CONNECT_REQ;
    } else {
      errprintf("CAPI 2.0: CONNECT_REQ failed, RC=0x%x\n", info);
      result = -1;
    }
  }

  g_mutex_unlock(isdn->lock);

  return result;
}

/*--------------------------------------------------------------------------*/

int isdn_send_data(isdn_t *isdn, unsigned char *data, unsigned int datalen)
{
  int result = 0;
  _cmsg CMSG;  /* structure for the message */
  unsigned int info, msgno = isdn->msg_no++;

  if (isdn->state != ISDN_CONNECTED) {
    dbgprintf(3, "ISDN data send while not connected (state %d)\n", isdn->state);
    return -1;
  }

  dbgprintf(3, "CAPI 2.0: DATA_B3_REQ ApplID %d ncci 0x%x data 0x%lx+%d\n",
            isdn->appl_id, isdn->active_ncci, (long) data, datalen);

  g_mutex_lock(isdn->data_lock);
  info = DATA_B3_REQ(&CMSG, isdn->appl_id, msgno,
                      isdn->active_ncci, data, datalen,
                      msgno, /* as data handle */
                      0x0);  /* flags */
  g_mutex_unlock(isdn->data_lock);

  if (info != 0) {
    if (isdn->state == ISDN_CONNECTED) {
      dbgprintf(1, "CAPI 2.0: DATA_B3_REQ failed (too fast audio?), RC=0x%x\n", info);
    } else {
      dbgprintf(3, "CAPI 2.0: DATA_B3_REQ failed (ISDN disconnected, OK), RC=0x%x\n", info);
    }
    result = -1;
  }

  return result;
}

/*--------------------------------------------------------------------------*/

int isdn_hangup(isdn_t *isdn)
{
  int result = 0;

  g_mutex_lock(isdn->lock);

  if (isdn->state == ISDN_IDLE) {
    errprintf("ISDN hangup called, even if connection idle\n");
    result = -1;
  } else {
    result = isdn_trigger_disconnect(isdn);
  }

  g_mutex_unlock(isdn->lock);

  return result;
}

/*--------------------------------------------------------------------------*/

int isdn_pickup(isdn_t *isdn _U_)
{
  _cmsg CMSG;  /* structure for the message */
  int result = 0;
  unsigned int info;
  unsigned char localnum[4];

  g_mutex_lock(isdn->lock);

  if (isdn->state != ISDN_RINGING) {
    errprintf("ISDN pickup called, even if not ringing\n");
    result = -1;
  } else {
    /* answer the call via CONNECT_RESP */
    dbgprintf(2, "CAPI 2.0: CONNECT_RESP ApplID %d msgno %d plci 0x%x reject %d\n",
              isdn->appl_id, isdn->msg_no, isdn->active_plci, 0);

    localnum[0] = 0x00;
    localnum[1] = 0x00;
    localnum[2] = 0x80;
    localnum[3] = 0x00;

    g_mutex_lock(isdn->data_lock);
    info = CONNECT_RESP(&CMSG, isdn->appl_id, isdn->msg_no++,
                        isdn->active_plci, 0,
                        1 /* B1protocol: originate */,
                        1 /* B2protocol: transparent */,
                        0 /* default B3protocol */,
                        0 /* default B1configuration */,
                        0 /* default B2configuration */,
                        0 /* default B3configuration */,
                        &localnum[0] /* TODO: local number */,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL /* additional info */);
    g_mutex_unlock(isdn->data_lock);

    if (info != 0) {
      errprintf("CAPI 2.0: CONNECT_RESP failed, RC=0x%x\n", info);
      isdn->state = ISDN_IDLE;
      result = -1;
    } else {
      /* connection initiated, wait for CONNECT_ACTIVE_IND */
      isdn->state = ISDN_INCOMING_WAIT;
    }
  }

  g_mutex_unlock(isdn->lock);

  return result;
}

/*--------------------------------------------------------------------------*/

int isdn_setMSN(isdn_t *isdn, char *msn)
{
  char *to_free = isdn->own_msn;

  if (msn && strcmp(msn, "0") != 0)
    isdn->own_msn = strdup(msn);
  else
    isdn->own_msn = 0;

  if (to_free)
    free(to_free);
  return 0;
}

/*--------------------------------------------------------------------------*/

int isdn_setMSNs(isdn_t *isdn _U_, char *msns _U_)
{
  if (isdn->listen_msns)
    free(isdn->listen_msns);

  isdn->listen_msns = msns ? strdup(msns) : strdup(DEFAULT_MSNS);
  return 0;
}

/*--------------------------------------------------------------------------*/

char* isdn_get_calls_filename(void) {
  unsigned int i;
  int fd;

  if (isdn_calls_filename_from_config &&
      (fd = open(isdn_calls_filename_from_config, O_RDONLY, 0644)) != -1)
  {
    close(fd);
    dbgprintf(1, "Using calls file listed in I4L config.\n");
    return isdn_calls_filename_from_config;
  }
  for (i = 0; i < sizeof(calls_filenames) / sizeof(char*); i++) {
    if ((fd = open(calls_filenames[i], O_RDONLY)) != -1) {
      close(fd);
      return calls_filenames[i];
    }
  }
  return NULL;
}

/*--------------------------------------------------------------------------*/

void isdn_speed_init(isdn_speed_t *speed)
{
  speed->samples = 0;
  speed->delta = 0;
  speed->start = 0;
  speed->debug = 0;
}

/*--------------------------------------------------------------------------*/

void isdn_speed_addsamples(isdn_speed_t *speed, unsigned int samples)
{
  uint64_t time = microsec_time();
  if (speed->start) {
    speed->samples += samples;
    speed->delta = time - speed->start;
  } else {
    speed->start = time;
    speed->samples = 0;
    speed->delta = 0;
    speed->debug = 0;
  }
}

/*--------------------------------------------------------------------------*/

void isdn_speed_debug(isdn_speed_t *speed, int level, char *prefix)
{
  uint64_t curtime = speed->start + speed->delta;

  if (curtime >= speed->debug + 1000000 && speed->delta) {
    speed->debug = curtime;
    dbgprintf(level, "%s speed: %.3f samples/sec\n", prefix,
              speed->samples * 1000000.0 / speed->delta);
  }
}

/*--------------------------------------------------------------------------*/
