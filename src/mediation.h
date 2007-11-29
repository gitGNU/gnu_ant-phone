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
 */

#include "session.h"

/*!
 * @brief Generate audio sample conversion tables.
 *
 *
 * @note The caller is responsible to free the memory allocated for
 *       conversion tables!
 *
 * @param format_in audio input format.
 * @param LUT_in conversion table to convert from A-law to audio.
 * @param format_out audio output format.
 * @param LUT_out conversion table to convert from audio to A-law.
 * @param LUT_generate conversion table from signed 8-bit to A-law.
 * @param LUT_analyze conversion table from A-law to signed 8-bit.
 * @param LUT_alaw2short conversion table from A-law to short.
 * @return 0 on success, -1 otherwise.
 */
int mediation_makeLUT(int format_in, unsigned char **LUT_in,
		      int format_out, unsigned char **LUT_out,
		      unsigned char **LUT_generate,
		      unsigned char **LUT_analyze,
		      short **LUT_alaw2short);

/*!
 * @brief Convert ISDN data to audio data.
 *
 * @param session current session.
 * @param isdn_buf ISDN data buffer (A-law or bit-inverse A-law).
 * @param isdn_size number of samples in ISDN buffer.
 * @param audio_buf destination buffer for audio data.
 * @param audio_size filled with size of audio data in bytes.
 * @param rec_buf recording buffer as temporary to hold at least isdn_size shorts.
 * @param bitinverse if true, ISDN data are bit-inverse A-law, otherwise A-law.
 */
void convert_isdn_to_audio(session_t *session,
                           unsigned char *isdn_buf,
                           unsigned int isdn_size,
                           unsigned char *audio_buf,
                           unsigned int *audio_size,
                           short *rec_buf,
                           unsigned int bitinverse);

/*!
 * @brief Convert audio data to ISDN data.
 *
 * @param session current session.
 * @param audio_buf buffer with audio data.
 * @param audio_size size of audio data in bytes.
 * @param isdn_buf destination ISDN data buffer (bit-inverse A-law).
 * @param isdn_size filled with number of samples written to ISDN buffer.
 * @param rec_buf recording buffer as temporary to hold at least isdn_size shorts.
 */
void convert_audio_to_isdn(session_t *session,
                           unsigned char *audio_buf,
                           unsigned int audio_size,
                           unsigned char *isdn_buf,
                           unsigned int *isdn_size,
                           short *rec_buf);
