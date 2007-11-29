/*
 * functions for mediation between ISDN and ALSA
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
 *
 *
 */

#include "config.h"

/* regular GNU system includes */
#include <stdio.h>
#ifdef HAVE_STDLIB_H
  #include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#include <errno.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <math.h>

/* own header files */
#include "globals.h"
#include "session.h"
/* ulaw conversion (LUT) */
#include "g711.h"
#include "isdn.h"
#include "sound.h"
#include "util.h"
#include "mediation.h"
#include "llcheck.h"
#include "fxgenerator.h"
#include "recording.h"


/*!
 * @brief Invert bits in a byte.
 *
 * For the fun of it, ISDN doesn't use plain A-law, but instead uses
 * bit-inverse A-law. I.e., instead of bit order 01234567, it sends
 * 76543210. This function converts it to bit-inverse.
 *
 * @param c byte to invert.
 * @return inverted byte.
 */
static unsigned char bitinverse(unsigned char c);

/*--------------------------------------------------------------------------*/

static unsigned char bitinverse(unsigned char c)
{
  return
      ((c >> 7) & 0x1) |
      ((c >> 5) & 0x2) |
      ((c >> 3) & 0x4) |
      ((c >> 1) & 0x8) |
      ((c << 1) & 0x10) |
      ((c << 3) & 0x20) |
      ((c << 5) & 0x40) |
      ((c << 7) & 0x80);
}

/*--------------------------------------------------------------------------*/

int mediation_makeLUT(int format_in, unsigned char **LUT_in,
		      int format_out, unsigned char **LUT_out,
		      unsigned char **LUT_generate,
		      unsigned char **LUT_analyze,
		      short **LUT_alaw2short) {
  int sample_size_in;
  int sample_size_out;
  int buf_size_in;
  int buf_size_out;
  int sample;
  int i;
  short s;
  
  /* Allocation */
  sample_size_in = sample_size_from_format(format_in); /* isdn -> audio */
  if (sample_size_in == 0 ||
      !(*LUT_in = (unsigned char *)malloc(buf_size_in = sample_size_in * 256)))
    return -1;
  
  sample_size_out = sample_size_from_format(format_out); /* audio -> isdn */
  if (sample_size_out == 0 ||
      !(*LUT_out =
	(unsigned char *)malloc(buf_size_out =
				(1 + (sample_size_out - 1) * 255) * 256)))
    return -1;

  if (!(*LUT_generate = (unsigned char*) malloc (256)))
    return -1;
  if (!(*LUT_analyze = (unsigned char*) malloc (256)))
    return -1;
  if (!(*LUT_alaw2short = (short*) malloc (256*sizeof(short))))
    return -1;
  
  /* Calculation */
  for (i = 0; i < buf_size_in; i += sample_size_in) { /* isdn -> audio */
    switch(format_in) {
    case SND_PCM_FORMAT_U8:
      (*LUT_in)[i] = (unsigned char)((alaw2linear((unsigned char)i) / 256 &
				     0xff) ^ 0x80);
      break;

    case SND_PCM_FORMAT_S8:
      (*LUT_in)[i] = (unsigned char)(alaw2linear((unsigned char)i) / 256 &
				     0xff);
      break;

    case SND_PCM_FORMAT_MU_LAW:
      (*LUT_in)[i] = linear2ulaw(alaw2linear((unsigned char)i));
      break;

    case SND_PCM_FORMAT_A_LAW:
      (*LUT_in)[i] = (unsigned char)i;
      break;

    case SND_PCM_FORMAT_S16_LE:
      sample = alaw2linear((unsigned char)(i / 2));
      (*LUT_in)[i] = (unsigned char)(sample & 0xff);
      (*LUT_in)[i+1] = (unsigned char)(sample >> 8 & 0xff);
      break;

    case SND_PCM_FORMAT_S16_BE:
      sample = alaw2linear((unsigned char)(i / 2));
      (*LUT_in)[i+1] = (unsigned char)(sample & 0xff);
      (*LUT_in)[i] = (unsigned char)(sample >> 8 & 0xff);
      break;

    case SND_PCM_FORMAT_U16_LE:
      sample = alaw2linear((unsigned char)(i / 2));
      (*LUT_in)[i] = (unsigned char)(sample & 0xff);
      (*LUT_in)[i+1] = (unsigned char)((sample >> 8 & 0xff) ^ 0x80);
      break;

    case SND_PCM_FORMAT_U16_BE:
      sample = alaw2linear((unsigned char)(i / 2));
      (*LUT_in)[i+1] = (unsigned char)(sample & 0xff);
      (*LUT_in)[i] = (unsigned char)((sample >> 8 & 0xff) ^ 0x80);
      break;

    default:
      errprintf("MEDIATION: "
	        "Unsupported in format %d appeared while building input LUT.\n",
                format_in);
      return -1;
    }
  }
	
  for (i = 0; i < buf_size_out; i++) { /* audio -> isdn */
    switch(format_out) {
    case SND_PCM_FORMAT_U8:
      (*LUT_out)[i] = linear2alaw((i - 128) * 256);
      break;

    case SND_PCM_FORMAT_S8:
      (*LUT_out)[i] = linear2alaw(i * 256);
      break;

    case SND_PCM_FORMAT_MU_LAW:
      (*LUT_out)[i] = linear2alaw(ulaw2linear((unsigned char)i));
      break;

    case SND_PCM_FORMAT_A_LAW:
      (*LUT_out)[i] = (unsigned char)i;
      break;

    /* next 4 cases:
       input int i stores first buffer byte in low byte */
    case SND_PCM_FORMAT_S16_LE:
      (*LUT_out)[i] = linear2alaw((int)(signed char)(i >> 8) << 8 |
				  (int)(i & 0xff));
      break;
      
    case SND_PCM_FORMAT_S16_BE:
      (*LUT_out)[i] = linear2alaw((int)(signed char)(i & 0xff) << 8 |
				  (int)(i >> 8));
      break;

    case SND_PCM_FORMAT_U16_LE:
      (*LUT_out)[i] = linear2alaw(i - 32768);
      break;

    case SND_PCM_FORMAT_U16_BE:
      (*LUT_out)[i] = linear2alaw(((i & 0xff) << 8 | i >> 8) - 32768);
      break;

    default:
      errprintf("MEDIATION: "
	        "Unsupported out format %d appeared while building output LUT.\n",
                format_out);
      return -1;
    }
  }

  for (i = 0; i < 256; i++) { /* 8 bit unsigned -> isdn -> 8 bit unsigned */
    (*LUT_generate)[i] = linear2alaw((i - 128) * 256);
    (*LUT_alaw2short)[i] = s = alaw2linear((unsigned char)i); /* alaw->short */
    (*LUT_analyze)[i] = (unsigned char)((s / 256 & 0xff) ^ 0x80);
  }

  return 0;
}

/*--------------------------------------------------------------------------*/

/* XXX: smooth samples when converting speeds in next 2 functions */

void convert_isdn_to_audio(session_t *session,
                           unsigned char *isdn_buf,
                           unsigned int isdn_size,
                           unsigned char *audio_buf,
                           unsigned int *audio_size,
                           short *rec_buf,
                           unsigned int inverse) {
  unsigned int i, j;
  unsigned char inbyte;  /* byte read from ttyI */
  unsigned int to_process; /* number of samples to process
                     (according to ratio / ratio_support_count) */
  unsigned int outptr;  /* output sample pointer */
  unsigned char sample; /* 8 bit unsigned sample */
  double llratio; /* line level falloff ratio */
  int max = 0; /* for llcheck */

  outptr = 0;

  dbgprintf(3, "MEDIATION: From isdn: got %d bytes.\n", isdn_size);

  for (i = 0; i < isdn_size; i++) {
    inbyte = isdn_buf[i];
    if (inverse)
      inbyte = bitinverse(inbyte);

    /* store sample for recording */
    rec_buf[i] = session->option_record_remote ?
                 session->audio_LUT_alaw2short[inbyte] : 0;

    /* input line level check */
    sample = session->audio_LUT_analyze[inbyte];
    if (abs((int)sample - 128) > max)
      max = abs((int)sample - 128);

    /* touchtone to audio: after llcheck to monitor other end */
    if (session->touchtone_countdown_audio > 0) {
      inbyte = fxgenerate(session, EFFECT_TOUCHTONE,
                          session->touchtone_index,
                          (double)session->touchtone_countdown_audio /
                          ISDN_SPEED); /* playing reverse is ok */
      session->touchtone_countdown_audio--;
    }

    /* mediation */
    to_process = (int)floor((double)(i + 1) * session->ratio_in) -
                (int)floor((double)i * session->ratio_in);
    /* printf("isdn -> audio: to_process == %d\n", to_process); */
    for (j = 0; j < to_process; j++) {
      if (session->audio_sample_size_out == 1) {
        audio_buf[outptr++] =
          session->audio_LUT_in[(int)inbyte];
      } else { /* audio_sample_size == 2 */
        audio_buf[outptr++] =
          session->audio_LUT_in[(int)inbyte * 2];
        audio_buf[outptr++] =
          session->audio_LUT_in[(int)inbyte * 2 + 1];
      }
    }
  }

  if (session->option_record && inverse) {
    recording_write(session->recorder, rec_buf, isdn_size, RECORDING_REMOTE);
  }

  llratio = isdn_size / 400.0;
  if (llratio > 1.0)
    llratio = 1.0;

  session->llcheck_in_state =
      session->llcheck_in_state * (1.0 - llratio) +
      ((double)max / 128) * llratio;
  dbgprintf(4, "MEDIATION: Audio out gain: %.3f\n", session->llcheck_in_state);

  *audio_size = outptr;
}

/*--------------------------------------------------------------------------*/

void convert_audio_to_isdn(session_t *session,
                           unsigned char *audio_buf,
                           unsigned int audio_size,
                           unsigned char *isdn_buf,
                           unsigned int *isdn_size,
                           short *rec_buf) {
  unsigned int i, j;
  unsigned int outptr;  /* output sample pointer */
  unsigned char sample; /* the alaw sample */
  /* the alaw sample when muted: */
  unsigned char zero = session->audio_LUT_generate[128];
  unsigned int to_process; /* number of samples to process
                     (according to ratio / ratio_support_count) */
  unsigned char sampleu8; /* 8 bit unsigned sample */
  double llratio; /* line level falloff ratio */
  int max = 0; /* for llcheck */

  outptr = 0;

  dbgprintf(3, "MEDIATION: From audio: got %d bytes.\n", audio_size);

  for (i = 0; i < audio_size;
       i += session->audio_sample_size_in) {

    to_process = (int)floor((double)(outptr + 1)
                            * session->ratio_out) -
                  (int)floor((double)outptr
                            * session->ratio_out);
    /* printf("audio -> isdn: to_process == %d\n", to_process); */
    for (j = 0; j < to_process; j++) {
      if (session->audio_sample_size_in == 1) {
        sample = session->audio_LUT_out[(int)(audio_buf[i])];
      } else { /* audio_sample_size == 2 */
        /* multiple byte samples are used "little endian" in int
            to look up in LUT (see mediation_makeLUT) */
        sample = session->audio_LUT_out[(int)(audio_buf[i]) |
                                        ((int)(audio_buf[i+1]) << 8)];
      }

      /* touchtone to isdn: before llcheck to monitor it */
      if (session->touchtone_countdown_isdn > 0) {
        sample = fxgenerate(session, EFFECT_TOUCHTONE,
                            session->touchtone_index,
                            (double)session->touchtone_countdown_isdn /
                            ISDN_SPEED /* playing reverse is ok */ );
        session->touchtone_countdown_isdn--;
      }

      if (session->option_muted) /* zero if muted */
        sample = zero;

      /* input line level check */
      sampleu8 = session->audio_LUT_analyze[sample];
      if (abs((int)sampleu8 - 128) > max)
        max = abs((int)sampleu8 - 128);

      /* store sample for recording */
      rec_buf[outptr] = session->option_record_local ?
                        session->audio_LUT_alaw2short[sample] : 0;

      isdn_buf[outptr++] = bitinverse(sample);
    }

  }

  if (session->option_record) {
    recording_write(session->recorder, rec_buf, outptr, RECORDING_LOCAL);
  }

  llratio = outptr / 400.0;
  if (llratio > 1.0)
    llratio = 1.0;

  session->llcheck_out_state =
      session->llcheck_out_state * (1.0 - llratio) +
      ((double)max / 128) * llratio;
  dbgprintf(4, "MEDIATION: Audio in gain: %.3f\n", session->llcheck_out_state);

  *isdn_size = outptr;
}

/*--------------------------------------------------------------------------*/
