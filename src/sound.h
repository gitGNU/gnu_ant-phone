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

#include <alsa/asoundlib.h>

#define DEFAULT_FRAGMENT_SIZE 128
#define DEFAULT_AUDIO_DEVICE_NAME_IN "default"
#define DEFAULT_AUDIO_DEVICE_NAME_OUT "default"

extern int default_audio_priorities[];

/*!
 * @brief Opens the audio device(s).
 *
 * @param in_audio_device_name name of input device.
 * @param out_audio_device_name name of output device.
 * @param channels requestes number of channels (1/2).
 * @param format_priorities list of sorted integers with valid sound formats
 *        (e.g. SND_PCM_FORMAT_U8). the first working one will be used.
 * @param audio_in filled with input PCM handle.
 * @param audio_out filled with output PCM handle.
 * @param fragment_size_in in/out fragment size for input.
 * @param fragment_size_out in/out fragment size for output.
 * @param speed_in in/out requested/actual input speed.
 * @param speed_out in/out requested/actual output speed.
 * @return 0 if successful, -1 on error.
*/
int open_audio_devices(char *in_audio_device_name,
		       char *out_audio_device_name,
		       int channels, int *format_priorities,
		       snd_pcm_t **audio_in, snd_pcm_t **audio_out,
		       int *fragment_size_in, int *fragment_size_out,
		       int *format_in, int *format_out,
                       unsigned int *speed_in, unsigned int *speed_out);

/*!
 * @brief Close audio devices..
 *
 * @param audio_in input device to close.
 * @param audio_out output device to close.
 * @return 0 if successful, -1 on error.
 */
int close_audio_devices(snd_pcm_t *audio_in, snd_pcm_t *audio_out);

/*!
 * @brief Stops audio playback and recording on specified devices.
 *
 * @param audio_in input device to stop.
 * @param audio_out output device to stop.
 * @return 0 if successful, -1 on error.
 */
int audio_stop(snd_pcm_t *audio_in, snd_pcm_t *audio_out);

/*!
 * @brief Get number of bytes per sample for the specified format.
 *
 * @param format ALSA format (e.g. SND_PCM_FORMAT_U8).
 * @return >= 1 on success, 0 otherwise (when format not supported).
 */
int sample_size_from_format(int format);
