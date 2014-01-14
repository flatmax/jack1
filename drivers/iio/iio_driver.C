/*

	IIO driver for Jack
	Copyright (C) 2013 Matt Flax <flatmax@flatmax.org>

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

#include <iostream>
#include <IIO/IIO.H>

#include <values.h>
#include <inttypes.h>
extern "C" {
#include "iio_driver.h"
#include "engine.h"
}

#define IIO_DEFAULT_CHIP "AD7476A" ///< The default IIO recording chip to look for.
#define IIO_DEFAULT_READ_FS 1.e6 ///< The default IIO sample rate for the default chip.
#define IIO_DEFAULT_PERIOD_SIZE 3000 ///< The default period size is in the ms range

//#define IIO_DRIVER_N_PARAMS	2
//const static jack_driver_param_desc_t iio_params[IIO_DRIVER_N_PARAMS] = {
//    {
//        "inchannels",
//        'i',
//        JackDriverParamUInt,
//        { .ui = IIO_DRIVER_DEF_INS },
//        NULL,
//        "capture channels",
//        "capture channels"
//    },
//    {
//        "capture",
//        'C',
//        JackDriverParamString,
//        { .str = IIO_DEFAULT_CHIP },
//        NULL,
//        "input chip name",
//        "input chip name"
//    }
//};

static int iio_driver_attach (iio_driver_t *driver, jack_engine_t *engine) {
    //return jack_activate (driver->client);
    return 0;
}

static int iio_driver_detach (iio_driver_t *driver, jack_engine_t *engine) {
    return 0;
}

static int iio_driver_start (iio_driver_t *driver) {
    return 0;
}

static int iio_driver_stop (iio_driver_t *driver) {
    return 0;
}

static int iio_driver_read (iio_driver_t *driver, jack_nframes_t nframes) {
    return 0;
}

static int iio_driver_write (iio_driver_t *driver, jack_nframes_t nframes) {
    return 0;
}

static int iio_driver_null_cycle (iio_driver_t *driver, jack_nframes_t nframes) {

// output buffers are currently not handled ... in future, add output handling here.

    return 0;
}

static int iio_driver_run_cycle (iio_driver_t *driver) {
    return 0;
}

/** Given the number of samples and the sample rate, find the number of microseconds.
\param nframes The number of frames.
\param fs The sample rate.
\return The number of microseconds represented by nframes.
*/
jack_time_t getUSecs(jack_nframes_t nframes, jack_nframes_t fs) {
    return (jack_time_t) floor((((float) nframes) / fs) * 1000000.0f);
}

static int iio_driver_bufsize (iio_driver_t *driver, jack_nframes_t nframes) {
    driver->period_size = nframes;
    driver->period_usecs = driver->wait_time = getUSecs(nframes, driver->sample_rate);

    /* tell the engine to change its buffer size */
    if (driver->engine->set_buffer_size (driver->engine, nframes)) {
        jack_error ("iio: cannot set engine buffer size to %d ", nframes);
        return -1;
    }

    return 0;
}

jack_driver_t *driver_initialize (jack_client_t *client, const JSList * params) {
    IIO *iio = NULL;
    iio_driver_t *driver = (iio_driver_t *) calloc (1, sizeof (iio_driver_t));

    if (driver) {
        jack_driver_nt_init((jack_driver_nt_t *) driver);

        driver->engine = NULL; // setup the required driver variables.
        driver->client = client;
        driver->last_wait_ust = 0;

        driver->write         = (JackDriverReadFunction)       iio_driver_write;
        driver->read          = (JackDriverReadFunction)       iio_driver_read;
        driver->null_cycle    = (JackDriverNullCycleFunction)  iio_driver_null_cycle;
        driver->nt_attach     = (JackDriverNTAttachFunction)   iio_driver_attach;
        driver->nt_stop       = (JackDriverNTStopFunction)     iio_driver_stop;
        driver->nt_start      = (JackDriverNTStartFunction)    iio_driver_start;
        driver->nt_detach     = (JackDriverNTDetachFunction)   iio_driver_detach;
        driver->nt_bufsize    = (JackDriverNTBufSizeFunction)  iio_driver_bufsize;
        driver->nt_run_cycle  = (JackDriverNTRunCycleFunction) iio_driver_run_cycle;

        driver->sample_rate = IIO_DEFAULT_READ_FS; // IIO sample rate is fixed.
        driver->period_size = IIO_DEFAULT_PERIOD_SIZE;

        driver->period_usecs = driver->wait_time = getUSecs(driver->period_size, driver->sample_rate);

        driver->capture_channels  = MAXINT; // The default number of physical input channels - a very large number, to be reduced.
        driver->capture_ports     = NULL;
        driver->playback_channels = 0; // currently doesn't support playback.
        driver->playback_ports    = NULL;

        iio = new IIO; // initialise the IIO system.
        if (iio) { // if the IIO class was successfully created ...
        driver->IIO_devices=static_cast<void*>(iio); // store the iio class in the C structure

            string chipName(IIO_DEFAULT_CHIP); // the default chip name to search for in the IIO devices.

            const JSList *pnode = params; // param pointer
            while (pnode != NULL) {
                const jack_driver_param_t *param = (const jack_driver_param_t *) pnode->data;
                switch (param->character) {

                case 'C': // we are specifying a new chip name
                    chipName = param->value.str;
                    break;
                case 'i': // we are specifying the number of capture channels
                    driver->capture_channels = param->value.ui;
                    break;
                }
                pnode = jack_slist_next(pnode);
            }

            iio->findDevicesByChipName(chipName); // find all devices with a particular chip which are present.

            iio->printInfo(); // print out detail about the devices which were found ...

            // if the available number of ports is less then the requested number, then restrict to the number of physical ports.
            if (iio->getChCnt()<driver->capture_channels)
                driver->capture_channels=iio->getChCnt();

            string name("iio_pcm");
            //jack_info("created iio driver ... %s|%" PRIu32 "|%" PRIu32 "|%lu|%u|%u", name.c_str(), driver->sample_rate, driver->period_size, driver->wait_time, driver->capture_channels, driver->playback_channels);

            return (jack_driver_t *) driver;
        } else
            jack_error("iio driver_initialise: new IIO failed: %s: %s@%i", strerror(errno), __FILE__, __LINE__);
        } else
        jack_error("iio driver_initialise: iio_driver_t malloc() failed: %s: %s@%i", strerror(errno), __FILE__, __LINE__);

    // if we get here, there was a problem.
    if (driver)
        free(driver);
    if (iio)
        delete iio;
    return NULL;
}
