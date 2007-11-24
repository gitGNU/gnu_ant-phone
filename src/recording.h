/*
 * recording functions
 *
 * This file is part of ANT (Ant is Not a Telephone)
 *
 * Copyright 2003 Roland Stigge
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

#ifndef _ANT_RECORDING_H
#define _ANT_RECORDING_H

#include "config.h"

/* regular GNU system includes */
#include <stdio.h>

/* sndfile audio file reading/writing library */
#include <sndfile.h>

/*!
 * @brief Recorder internal buffer size (number of items, 1 item = 2 shorts).
 */
#define RECORDING_BUFSIZE 32768

/*!
 * @brief Recording jitter to compensate, about 25ms.
 */
#define RECORDING_JITTER 200

/*!
 * @brief Recording formats.
 */
enum recording_format_t {
  RECORDING_FORMAT_WAV = 0x10,
  RECORDING_FORMAT_AIFF = 0x20,
  
  RECORDING_FORMAT_ULAW = 0x01,
  RECORDING_FORMAT_S16 = 0x02,

  RECORDING_FORMAT_MAJOR = 0xF0,
  RECORDING_FORMAT_MINOR = 0x0F
};

/*!
 * @brief Recording channels.
 */
enum recording_channel_t {
  RECORDING_LOCAL = 0,    /*!< local channel */
  RECORDING_REMOTE = 1    /*!< remote channel */
};

/*!
 * @brief Recording channel structure (cyclic buffer).
 *
 * Each recording data buffer computes its start and end positions based on
 * sample count, current microtime, start microtime and ISDN speed. Then,
 * it will write itself at appropriate position in recording buffer and update
 * the position.
 *
 * Consumer of recording buffer will take last written position and minimum of
 * the positions in buffers and write out the samples in between. The written
 * samples will be zeroed out, to prevent repeating data. The consumer will
 * be repeatedly called by main thread (via GTK timeout).
 *
 * Opening of recording device will set a flag to start writing to recording
 * buffers. Closing of recording device will turn off recording flag and flush
 * all not yet written data.
 *
 * In this way, it's possible to more or less guarantee real-time properties
 * of ISDN and audio threads (as they will not wait for any disk writes) and
 * also be reasonably sure the samples do make it to the disk. If they don't,
 * because of CPU load, there will be skipping of samples on disk. So what...
 */
typedef struct {
  int64_t position;                 /*!< one past last sample (absolute position) */
  short buf[RECORDING_BUFSIZE];     /*!< buffer with samples to record */
} rec_channel_t;

/*!
 * @brief Recorder state.
 */
struct recorder_t {
  SNDFILE *sf;                      /*!< sndfile state */
  char *filename;                   /*!< audio file name */

  int64_t start_time;               /*!< recording start time (0=disabled) */
  rec_channel_t channel_local;      /*!< recoding data channel for local data */
  rec_channel_t channel_remote;     /*!< recoding data channel for remote data */
  int64_t last_write;               /*!< position of last known write */
};

/*!
 * @brief Initialize recorder structure.
 *
 * @param recorder struct to be filled with recorder state for recording
 *                 session until recording_close().
 * @return 0 on success, -1 otherwise.
 */
int recording_init(struct recorder_t *recorder);

/*!
 * @brief Opens a file and prepares recorder.
 *
 * If the file already exists, new data will be appended at the end.
 *
 * @param recorder struct to be filled with recorder state for recording
 *                 session until recording_close().
 * @param filename the base file name. It will be expanded with full path
 *                 and extension.
 * @return 0 on success, -1 otherwise.
 */
int recording_open(struct recorder_t *recorder, char *filename,
                   enum recording_format_t format);

/*!
 * @brief Writes specified number of shorts to recording channel.
 *
 * @param recorder struct with sound file state.
 * @param buf buffer to write.
 * @param size buffer size.
 * @param channel channel to write to (RECORDING_LOCAL or RECORDING_REMOTE).
 * @return 0 on success, -1 otherwise.
 */
int recording_write(struct recorder_t *recorder, short *buf, int size,
                    enum recording_channel_t channel);

/*!
 * @brief Flushes current record buffer to the file.
 *
 * This function is to be called periodically by main thread to write
 * outstanding sound samples to the sound file and make space in cyclic
 * buffer for data pro
 *
 * @param recorder struct with sound file state.
 * @param last if nonzero, flush all samples, otherwise account for jitter.
 * @return 0 on success, -1 otherwise.
 */
int recording_flush(struct recorder_t *recorder, unsigned int last);

/*!
 * @brief Finishes recording to file.
 *
 * This function disables recording and writes remaining data from queue
 * to file (eventually filling the lagging channel with silence).
 *
 * @param recorder struct with sound file state.
 * @return 0 on success, -1 otherwise.
 */
int recording_close(struct recorder_t *recorder);

#endif /* recording.h */
