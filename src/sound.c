/*
 * ALSA sound handling functions
 *
 * This file is part of ANT (Ant is Not a Telephone)
 *
 * Copyright 2002, 2003 Roland Stigge
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
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
  #include <fcntl.h>
#endif
#include <math.h>
#include <errno.h>
#include <sys/soundcard.h>
#ifdef HAVE_STDLIB_H
  #include <stdlib.h>
#endif
#include <string.h>
#include <ctype.h>

/* own header files */
#include "globals.h"
#include "sound.h"

/* try formats in this order */
int default_audio_priorities[] = {SND_PCM_FORMAT_S16_LE,
                                  SND_PCM_FORMAT_S16_BE,
                                  SND_PCM_FORMAT_U16_LE,
                                  SND_PCM_FORMAT_U16_BE,
                                  SND_PCM_FORMAT_U8,
                                  SND_PCM_FORMAT_S8,
            /* alaw/ulaw at last because no native soundcard support assumed */
                                  SND_PCM_FORMAT_A_LAW,
                                  SND_PCM_FORMAT_MU_LAW,
                                  0}; /* end of list */

/*--------------------------------------------------------------------------*/

int audio_enum_devices(audio_enum_fnc_t callback,
                       audio_direction_t dir,
                       void *context)
{
  static void **hints = 0;
  void **n;
  char *name, *descr, *io, *d, *p, *r;
  const char *filter;
  int err;
  int wasspace;

  if (hints == 0)
  {
    err = snd_device_name_hint(-1, "pcm", &hints);
    if (err < 0)
    {
      errprintf("AUDIO: snd_device_name_hint for pcm failed: %s\n",
              snd_strerror(err));
      return err;
    }
  }

  n = hints;
  filter = (dir == AUDIO_DIR_CAPTURE) ? "Input" : "Output";
  while (*n != NULL) {
    name = snd_device_name_get_hint(*n, "NAME");
    descr = snd_device_name_get_hint(*n, "DESC");
    io = snd_device_name_get_hint(*n, "IOID");

    if (io == NULL || strcmp(io, filter) == 0)
    {
      // have a device
      dbgprintf(2, "AUDIO: have device (%s): %s\n", io, name);

      p = r = d = strdup(descr);
      wasspace = 0;

      while (*p)
      {
        if (!isspace(*p)) {
          if (wasspace)
            *d++ = ' ';
          *d++ = *p++;
          wasspace = 0;
        } else {
          wasspace = 1;
          p++;
        }
      }
      *d = 0;

      callback(context, name, r);
      free(r);
    }

    if (name != NULL)
      free(name);
    if (descr != NULL)
      free(descr);
    if (io != NULL)
      free(io);

    n++;
  }

  return 0;
}

/*--------------------------------------------------------------------------*/

/*!
 * @brief Common initialization for a specific audio device
 *
 * @param audio PCM descriptor of an audio device.
 * @param format PCM format.
 * @param channels number of PCM channels.
 * @param speed requested/actual sampling rate (in/out).
 * @param fragment_size requested/actual fragment size (in/out).
 *
 * @return 0 if successful, non-zero otherwise.
 */
static int init_audio_device(snd_pcm_t *audio,
                             int format,
                             int channels,
                             unsigned int *speed,
		             int *fragment_size)
{
  int err;
  unsigned int rspeed;
  int dir;
  unsigned int buffer_time, period_time;
  snd_pcm_uframes_t period_size;

  snd_pcm_hw_params_t *hwparams;
  snd_pcm_sw_params_t *swparams;
  snd_pcm_hw_params_alloca(&hwparams);
  snd_pcm_sw_params_alloca(&swparams);

  if ((err = snd_pcm_hw_params_any(audio, hwparams)) < 0) {
    errprintf("AUDIO: AUDIO: No audio configurations available: %s\n",
              snd_strerror(err));
    return err;
  }

  if ((err = snd_pcm_hw_params_set_format(audio, hwparams, format)) < 0) {
    errprintf("AUDIO: Audio sample format (%d) not available: %s\n",
              format, snd_strerror(err));
    return err;
  }

  if ((err = snd_pcm_hw_params_set_channels(audio, hwparams, channels)) < 0) {
    errprintf("AUDIO: Audio channels count (%d) not available: %s\n",
              channels, snd_strerror(err));
    return err;
  }

  rspeed = *speed;
  if ((err = snd_pcm_hw_params_set_rate_near(audio, hwparams, &rspeed, 0)) < 0) {
    errprintf("AUDIO: Audio speed %dHz not available: %s\n",
              *speed, snd_strerror(err));
    return err;
  }
  if (rspeed != *speed) {
    errprintf("AUDIO: Audio speed doesn't match (requested %dHz, got %dHz)\n",
              *speed, rspeed);
    *speed = rspeed;
    /*return -EINVAL;*/
  }

  buffer_time = 150000;
  if ((err = snd_pcm_hw_params_set_buffer_time_near(audio, hwparams, &buffer_time, &dir)) < 0) {
    errprintf("AUDIO: Unable to set audio buffer time %d: %s\n",
              buffer_time, snd_strerror(err));
    return err;
  }

  period_time = 25000;
  if ((err = snd_pcm_hw_params_set_period_time_near(audio, hwparams, &period_time, &dir)) < 0) {
    errprintf("AUDIO: Unable to set audio period time %d: %s\n",
              period_time, snd_strerror(err));
    return err;
  }

  if ((err = snd_pcm_hw_params_get_period_size_min(hwparams, &period_size, &dir)) < 0) {
    errprintf("AUDIO: Unable to get audio period time: %s\n",
              snd_strerror(err));
    return err;
  }
  *fragment_size = period_size;

  if ((err = snd_pcm_hw_params_set_access(audio, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    errprintf("AUDIO: Unable to set audio access mode: %s\n",
              snd_strerror(err));
    return err;
  }

  if ((err = snd_pcm_hw_params(audio, hwparams)) < 0) {
    errprintf("AUDIO: Unable to set hw params for audio: %s\n",
              snd_strerror(err));
    return err;
  }

  if ((err = snd_pcm_sw_params_current(audio, swparams)) < 0) {
    errprintf("AUDIO: Unable to determine current swparams for audio: %s\n",
              snd_strerror(err));
    return err;
  }

  if ((err = snd_pcm_sw_params_set_start_threshold(audio, swparams, period_size)) < 0) {
    errprintf("AUDIO: Unable to set start threshold: %s\n",
              snd_strerror(err));
    return err;
  }

  /*
  if ((err = snd_pcm_sw_params_set_sleep_min(audio, swparams, 0)) < 0) {
    errprintf("AUDIO: Unable to set minimum sleep time: %s\n",
              snd_strerror(err));
    return err;
  }
  */

  if ((err = snd_pcm_sw_params_set_xfer_align(audio, swparams, 1)) < 0) {
    errprintf("AUDIO: Unable to set transfer alignment: %s\n",
              snd_strerror(err));
    return err;
  }

  if ((err = snd_pcm_sw_params(audio, swparams)) < 0) {
    printf("Unable to set sw params for audio (required): %s\n", snd_strerror(err));
    return err;
  }

  if ((err = snd_pcm_sw_params_current(audio, swparams)) < 0) {
    errprintf("AUDIO: Unable to determine current swparams for audio: %s\n",
              snd_strerror(err));
    return err;
  }

  if ((err = snd_pcm_sw_params_set_silence_size(audio, swparams, period_size * 2)) < 0) {
    errprintf("AUDIO: Unable to set silence threshold: %s\n",
              snd_strerror(err));
    return err;
  }

  /*
  if ((err = snd_pcm_sw_params_set_stop_threshold(audio, swparams, 0)) < 0) {
    errprintf("AUDIO: Unable to set stop threshold: %s\n",
              snd_strerror(err));
    return err;
  }
  */

  if ((err = snd_pcm_sw_params(audio, swparams)) < 0) {
    printf("Unable to set sw params for audio (optional): %s\n", snd_strerror(err));
    /*return err;*/
  }

#if 0
  if ((err = snd_pcm_prepare(audio)) < 0) {
    errprintf("AUDIO: Cannot prepare audio: %s\n", snd_strerror(err));
    return -1;
  }
#endif

  return 0;
}

/*--------------------------------------------------------------------------*/

int open_audio_devices(char *in_audio_device_name,
		       char *out_audio_device_name,
		       int channels, int *format_priorities,
		       snd_pcm_t **audio_in, snd_pcm_t **audio_out,
		       int *fragment_size_in, int *fragment_size_out,
		       int *format_in, int *format_out,
                       unsigned int *speed_in, unsigned int *speed_out)
{
  int err;
  int initresult;
  int *priority;
  
  /* try to open the sound device */
  if ((err = snd_pcm_open(audio_in, in_audio_device_name, SND_PCM_STREAM_CAPTURE, 0/*SND_PCM_NONBLOCK*/)) < 0) {
    errprintf("AUDIO: Audio recording device '%s' open error: %s, trying default\n",
            in_audio_device_name, snd_strerror(err));
    if ((err = snd_pcm_open(audio_in, "default", SND_PCM_STREAM_CAPTURE, 0/*SND_PCM_NONBLOCK*/)) < 0) {
      errprintf("AUDIO: Audio recording device 'default' open error: %s\n",
                snd_strerror(err));
      return -1;
    }
  }
  if ((err = snd_pcm_open(audio_out, out_audio_device_name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
    errprintf("AUDIO: Audio playback device '%s' open error: %s, trying default\n",
            out_audio_device_name, snd_strerror(err));
    if ((err = snd_pcm_open(audio_out, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
      errprintf("AUDIO: Audio playback device 'default' open error: %s\n",
                snd_strerror(err));
      return -1;
    }
  }

  /* set format and sampling rate */
  for (initresult = -1, priority = format_priorities;
       initresult && *priority; priority++) {

    *format_in = *priority;
    initresult = init_audio_device(*audio_in, *format_in, channels, speed_in, fragment_size_in);
  }

  if (initresult)
    return initresult; /* no chance */

  for (initresult = -1, priority = format_priorities;
       initresult && priority; priority++) {

    *format_out = *priority;
    initresult = init_audio_device(*audio_out, *format_out, channels, speed_out, fragment_size_out);
  }

#if 0
  /* not implemented in ALSA */
  if ((err = snd_pcm_link(*audio_out, *audio_in)) < 0) {
    errprintf("AUDIO: Error linking in/out devices: %s\n",
              snd_strerror(err));
    return -1;
  }
#endif

  if (debug > 2) {
    /* TODO: redirect output to dbgprintf */
    snd_output_t *output;
    snd_output_stdio_attach(&output, stderr, 0);
    dbgprintf(2, "AUDIO: Dump of IN PCM:");
    snd_pcm_dump(*audio_in, output);
    dbgprintf(2, "AUDIO: Dump of OUT PCM:");
    snd_pcm_dump(*audio_out, output);
  }

  return initresult;
}

int audio_stop(snd_pcm_t *audio_in, snd_pcm_t *audio_out) {

  int err, err0;

  err = 0;
  if ((err0 = snd_pcm_drop(audio_in)) < 0) {
    errprintf("AUDIO: Unable to reset audio capture: %s\n", snd_strerror(err0));
    err = -1;
  }
  if ((err0 = snd_pcm_drop(audio_out)) < 0) {
    errprintf("AUDIO: Unable to reset audio playback: %s\n", snd_strerror(err0));
    err = -1;
  }
  return err;
}

/*--------------------------------------------------------------------------*/

int close_audio_devices(snd_pcm_t *audio_in, snd_pcm_t *audio_out)
{

  int err, err0;

  audio_stop(audio_in, audio_out);

  err = 0;
  if ((err0 = snd_pcm_close(audio_in)) < 0) {
    errprintf("AUDIO: Unable to close audio capture: %s\n", snd_strerror(err0));
    err = -1;
  }
  if ((err0 = snd_pcm_close(audio_out)) < 0) {
    errprintf("AUDIO: Unable to close audio playback: %s\n", snd_strerror(err0));
    err = -1;
  }
  return err;
}

/*--------------------------------------------------------------------------*/

int sample_size_from_format(int format)
{
  switch(format) {
  case SND_PCM_FORMAT_S16_LE:
  case SND_PCM_FORMAT_S16_BE:
  case SND_PCM_FORMAT_U16_LE:
  case SND_PCM_FORMAT_U16_BE:
    return 2;

  case SND_PCM_FORMAT_U8:
  case SND_PCM_FORMAT_S8:
  case SND_PCM_FORMAT_MU_LAW:
  case SND_PCM_FORMAT_A_LAW:
    return 1;

  default:
    return 0;
  }
}

/*--------------------------------------------------------------------------*/
