/*

	OSS driver for Jack
	Copyright (C) 2003-2007 Matt Flax <faltmax@>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston,
	MA  02111-1307  USA

*/


#ifndef __JACK_IIO_DRIVER_H__
#define __JACK_IIO_DRIVER_H__

#include <jack/types.h>
#include <jack/jack.h>

#include "driver.h"

/** The structure defining all of the IIO related variables.
*/
typedef struct _iio_driver {
    JACK_DRIVER_NT_DECL;

	jack_nframes_t period_size;
//	unsigned int nperiods;
	unsigned int capture_channels;
	unsigned int playback_channels;
//
//	char *indev;
//	char *outdev;
//	int infd;
//	int outfd;
//	int format;
//	int ignorehwbuf;
//	int trigger;
//
	JSList *capture_ports;
	JSList *playback_ports;
//
//	jack_engine_t *engine;
	jack_client_t *client;

    jack_nframes_t  sample_rate; ///< The sample rate of the IIO chip.
    unsigned long   wait_time; ///< The time to wait between calls.

    void *IIO_devices; ///< The IIO C++ class maintaining all devices with a particular chip name.
} iio_driver_t;

/** Function called by jack to init. the IIO driver, possibly passing in variables.
*/
jack_driver_t *driver_initialize (jack_client_t *client, const JSList * params);
#endif

