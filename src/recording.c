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

#include "config.h"

/* regular GNU system includes */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* sndfile audio file reading/writing library */
#include <sndfile.h>

/* own header files */
#include "globals.h"
#include "isdn.h"
#include "recording.h"
#include "util.h"

/*--------------------------------------------------------------------------*/

int recording_init(struct recorder_t *recorder)
{
  memset(recorder, 0, sizeof(struct recorder_t));
  return 0;
}

/*--------------------------------------------------------------------------*/

int recording_open(struct recorder_t *recorder, char *filename,
                   enum recording_format_t format)
{
  SF_INFO sfinfo;
  char *homedir;
  char *fn;

  /* prepare path and file */
  touch_dotdir();
  
  if (!(homedir = get_homedir())) {
    errprintf("RECORD: Couldn't get home dir.\n");
    return -1;
  }
  if (asprintf(&fn, "%s/." PACKAGE "/recordings", homedir) < 0) {
    errprintf("RECORD: "
	    "recording_open: Couldn't allocate memory for directory name.\n");
    return -1;
  }
  if (touch_dir(fn) < 0) {
    errprintf("RECORD: recording_open: Can't reach directory %s.\n", fn);
    return -1;
  }
  free(fn);

  if (asprintf(&fn, "%s/." PACKAGE "/recordings/%s.%s", homedir, filename, format & RECORDING_FORMAT_WAV ? "wav" : "aiff")
      < 0) {
    errprintf("RECORD: "
	    "recording_open: Couldn't allocate memory for file name.\n");
    return -1;
  }
 
  if (access(fn, F_OK)) { /* file doesn't exist */
    sfinfo.format = 
      (format & RECORDING_FORMAT_WAV ? SF_FORMAT_WAV : SF_FORMAT_AIFF) |
      (format & RECORDING_FORMAT_S16 ? SF_FORMAT_PCM_16 : SF_FORMAT_ULAW);
    sfinfo.channels = 2;
    sfinfo.samplerate = ISDN_SPEED;
    if (!(recorder->sf = sf_open(fn, SFM_WRITE, &sfinfo))) {
      errprintf("RECORD: recording_open: sf_open (file creation) error.\n");
      return -1;
    }
  } else { /* file already exists */
    sfinfo.format = 0;
    if (!(recorder->sf = sf_open(fn, SFM_RDWR, &sfinfo))) {
      errprintf("RECORD: recording_open: sf_open (reopen) error.\n");
      return -1;
    }
    if (sf_seek(recorder->sf, 0, SEEK_END) == -1) {
      errprintf("RECORD: recording_open: sf_seek error.\n");
      return -1;
    }
  }
  recorder->filename = fn;

  /* initialize streaming buffers */
  recorder->last_write = 0;
  memset(&recorder->channel_local, 0, sizeof(rec_channel_t));
  memset(&recorder->channel_remote, 0, sizeof(rec_channel_t));

  /* NOTE: this has to be the last assignment, as it starts recording */
  recorder->start_time = microsec_time();
  return 0;
}

/*--------------------------------------------------------------------------*/

int recording_write(struct recorder_t *recorder, short *buf, int size,
		    enum recording_channel_t channel)
{
  int64_t start = recorder->start_time;
  int64_t current, startpos, position;
  int bufpos, split, delta;
  rec_channel_t *buffer;

  if (start == 0)
    return 0; /* not enabled */
  if (size < 1) {
    errprintf("RECORD: recording_write: Trying to record with wrong size %d\n",
              size);
    return -1;
  }

  /* determine channel */
  switch (channel)
  {
    case RECORDING_LOCAL:
      buffer = &recorder->channel_local;
      break;
    case RECORDING_REMOTE:
      buffer = &recorder->channel_remote;
      break;
    default:
      errprintf("RECORD: recording_write: Recording to unknown channel %d requested\n",
                (int) channel);
      return -1;
  }

  /* compute position where to start write */
  current = microsec_time() - start;
  if (current < 0)
    return 0; /* should never happen! */
  int64_t endpos = current * ISDN_SPEED / 1000000LL;
  startpos = endpos - size;
  position = buffer->position;
  if (startpos >= position - RECORDING_JITTER && startpos <= position + RECORDING_JITTER) {
    /* position falls within recording jitter, adjust it to prevent cracks in recording */
    startpos = position;
    endpos = position + size;
  }
  if (startpos < position) {
    /* should not happen, but to be sure, skip samples at the beginning */
    delta = (int) position - startpos;
    startpos = position;
    buf += delta;
    size -= delta;
    if (size <= 0)
      return 0; /* skipping too much, no data left */
  }
  bufpos = startpos % RECORDING_BUFSIZE;

  dbgprintf(3, "RECORD: recording_write: data 0x%lx+%d to channel %d, pos %lld(%d)\n",
            (long) buf, size, (int) channel, (long long) startpos, bufpos);

  /* copy data into buffer */
  if (bufpos + size <= RECORDING_BUFSIZE) {
    /* all data can be copied at once */
    memcpy(buffer->buf + bufpos, buf,
           size * sizeof(short));
  } else {
    /* must split to two copies */
    split = RECORDING_BUFSIZE - bufpos;
    memcpy(buffer->buf + bufpos, buf,
           split * sizeof(short));
    buf += split;
    size -= split;
    memcpy(buffer->buf, buf, size * sizeof(short));
  }

  /* mark buffer last position */
  buffer->position = endpos;
  return 0;
}

/*--------------------------------------------------------------------------*/

int recording_flush(struct recorder_t *recorder, unsigned int last)
{
  /* compute position to write up to */
  int64_t maxposition = recorder->channel_local.position;
  int64_t tmp = recorder->channel_remote.position;
  int64_t startposition = recorder->last_write;
  short recbuf[RECORDING_BUFSIZE * 2];  /* sample buffer */
  int srcptr, dstptr, size;

  if (recorder->start_time == 0)
    return 0; /* recording not active */

  if (tmp > maxposition)
    maxposition = tmp;

  if (startposition + (RECORDING_BUFSIZE * 7 / 8) < maxposition) {
    /* underflow, skip samples */
    dbgprintf(2, "RECORD: recording_flush: underflow detected, start %lld, current %lld\n",
              (long long) startposition, (long long) maxposition);
    startposition = maxposition - (RECORDING_BUFSIZE * 7 / 8);
  }

  if (!last) {
    /* skip last 1/8th of the buffer as jitter buffer */
    maxposition -= RECORDING_BUFSIZE / 8;
  }

  size = (int) (maxposition - startposition);
  if (maxposition <= 0 || startposition >= maxposition ||
      (!last && size < RECORDING_BUFSIZE / 8))
    return 0;   /* not enough data yet */

  /* write samples between startposition and maxposition */
  dstptr = 0;
  srcptr = startposition % RECORDING_BUFSIZE;
  while (--size) {
    recbuf[dstptr++] = recorder->channel_local.buf[srcptr];
    recorder->channel_local.buf[srcptr] = 0;
    recbuf[dstptr++] = recorder->channel_remote.buf[srcptr];
    recorder->channel_remote.buf[srcptr] = 0;

    if (++srcptr >= RECORDING_BUFSIZE)
      srcptr = 0;
  }
  sf_writef_short(recorder->sf, recbuf, dstptr / 2);

  /* update last sample pointer */
  recorder->last_write = maxposition;
  return 0;
}

/*--------------------------------------------------------------------------*/

int recording_close(struct recorder_t *recorder)
{
  int result = 0;

  if (recorder->start_time) {
    /* flush outstanding data and disable recording */
    if (recording_flush(recorder, 1) < 0)
      result = -1;
    recorder->start_time = 0;

    if (recorder->filename) {
      free(recorder->filename);
      recorder->filename = 0;
    }

    /* close the recorder */
    if (sf_close(recorder->sf) != 0)
      result = -1;
  }

  return result;
}

/*--------------------------------------------------------------------------*/
