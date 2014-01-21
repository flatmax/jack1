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

/*
Author: Matt Flax <flatmax@flatmax.org>
Date: 2014.01.13

I am initially testing this driver like so :
JACK_DRIVER_DIR=/home/flatmax/jack1/drivers/iio/.libs ./jackd/.libs/jackd -r -d iio

To actually perform a test using a client, you need to install : make install in the topsrc dir.
*/

#include <iostream>
#include <IIO/IIO.H>

#include <values.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
extern "C" {
#include "iio_driver.h"
#include "engine.h"
}

#define IIO_DEFAULT_CHIP "AD7476A" ///< The default IIO recording chip to look for.
#define IIO_DEFAULT_READ_FS 1.e6 ///< The default IIO sample rate for the default chip.
#define IIO_DEFAULT_PERIOD_SIZE 2048 ///< The default period size is in the ms range
#define IIO_DEFAULT_CAPUTURE_PORT_COUNT MAXINT ///< The default number of capture ports is exceedingly big, trimmed down to a realistic size in driver_initialize
//#define IIO_SAFETY_FACTOR 2./3. ///< The default safety factor, allow consumption of this fraction of the available DMA buffer before we don't allow the driver to continue.
#define IIO_SAFETY_FACTOR 1. ///< The default safety factor, allow consumption of this fraction of the available DMA buffer before we don't allow the driver to continue.

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
    // open the IIO subsystem
    IIO *iio = static_cast<IIO *>(driver->IIO_devices);
    int ret=iio->open(); // try to open all IIO devices
    if (ret!=NO_ERROR)
        return ret;

    // create ports
	jack_port_t * port;
	char buf[32];
	unsigned int chn;
	int port_flags;

	if (driver->engine->set_buffer_size (driver->engine, driver->period_size)) {
		jack_error ("iio: cannot set engine buffer size to %d (check MIDI)", driver->period_size);
		return -1;
	}
	driver->engine->set_sample_rate (driver->engine, driver->sample_rate);

	port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;

	for (chn = 0; chn < driver->capture_channels; chn++){
		snprintf (buf, sizeof(buf) - 1, "capture_%u", chn+1);

		port = jack_port_register (driver->client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
		if (!port) {
			jack_error ("iio: cannot register port for %s", buf);
			break;
		}

		driver->capture_ports = jack_slist_append (driver->capture_ports, port);
	}

	port_flags = JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal;

	for (chn = 0; chn < driver->playback_channels; chn++) {
		snprintf (buf, sizeof(buf) - 1, "playback_%u", chn+1);

		port = jack_port_register (driver->client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);

		if (!port) {
			jack_error ("iio: cannot register port for %s", buf);
			break;
		}

		driver->playback_ports = jack_slist_append (driver->playback_ports, port);
	}

    return jack_activate (driver->client);
}

static int iio_driver_detach (iio_driver_t *driver, jack_engine_t *engine) {
    static_cast<IIO *>(driver->IIO_devices)->close(); // close the IIO system

    if (driver->engine == 0)
        return -1;

    // unregister all ports
    for (JSList *node = driver->capture_ports; node; node = jack_slist_next(node)) {
        jack_port_unregister(driver->client, ((jack_port_t *) node->data));
    }

    jack_slist_free(driver->capture_ports);
    driver->capture_ports = 0;

    for (JSList *node = driver->playback_ports; node; node = jack_slist_next(node)) {
        jack_port_unregister(driver->client, ((jack_port_t *) node->data));
    }

    jack_slist_free(driver->playback_ports);
    driver->playback_ports = 0;

    driver->engine = 0;
    return 0;
}

static int iio_driver_start (iio_driver_t *driver) {
    cout<<"iio_driver_start::   enabling IIO : enable(true)"<<endl;
    static_cast<IIO *>(driver->IIO_devices)->enable(true); // start the DMA

#ifdef HAVE_CLOCK_GETTIME
    driver->next_wakeup.tv_sec = 0;
#else
    driver->next_time = 0;
#endif
    return 0;
}

static int iio_driver_stop (iio_driver_t *driver) {
    cout<<"iio_driver_start:: disabling IIO : enable(false)"<<endl;
    static_cast<IIO *>(driver->IIO_devices)->enable(false); // stop the DMA
    return 0;
}

static int iio_driver_read(iio_driver_t *driver, jack_nframes_t nframes) {
    if (nframes > 0) {
        //cout<<"iio_driver_read nframes = "<<nframes<<"\n";
        Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *data = static_cast<Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *>(driver->data);
        IIO *iio = static_cast<IIO *>(driver->IIO_devices);
        uint devChCnt=(*iio)[0].getChCnt();

        // read from the IIO devices ...
        int ret=iio->read(nframes, *data);
        if (ret!=NO_ERROR)
            return -1;

        // write to the connected capture ports ...
        JSList *node = (JSList *)driver->capture_ports;
        for (channel_t chn = 0; node; node = (JSList *)jack_slist_next(node), chn++) {

            jack_port_t *port = static_cast<jack_port_t *>(node->data);

            if (!jack_port_connected (port)) /* no-copy optimization */
                continue;

            jack_default_audio_sample_t *buf = static_cast<jack_default_audio_sample_t *>(jack_port_get_buffer (port, nframes));
            for (jack_nframes_t i=0; i<nframes; i++){
                //cout<<"row = "<<chn/devChCnt<<" col = "<<i*devChCnt+chn%devChCnt<<endl;
                buf[i]=(*data)(i*devChCnt+chn%devChCnt, chn/devChCnt);
            }
        }
    }
    return 0;
}

static int iio_driver_write (iio_driver_t *driver, jack_nframes_t nframes) {
    //if (nframes>0)
    //    cout<<"iio_driver_write nframes = "<<nframes<<"\n";
    return 0;
}

static int iio_driver_null_cycle (iio_driver_t *driver, jack_nframes_t nframes) {
    //cout<<"iio_driver_null_cycle\n";

// output buffers are currently not handled ... in future, add output handling here.

    return 0;
}

/** The driver_wait function to work out if we have used more time then available to process one cycle.
*/
static jack_nframes_t iio_driver_wait(iio_driver_t *driver, int extra_fd, int *status, float *delayed_usecs) {
    //cout<<"iio_driver_wait\n";
    //float maxDelayTime=(IIO_SAFETY_FACTOR*driver->maxDelayUSecs); // this driver can handle this much delay between reads.
    float maxDelayTime=driver->maxDelayUSecs; // this driver can handle this much delay between reads.
    //cout<<"maxDelayTime "<<maxDelayTime<<endl;
    *status = 0;

    jack_time_t now = driver->engine->get_microseconds();

    bool xrun=false;
    if (driver->next_time < now){
        //cout<<"iio_driver_wait NOT good\n";
        if (driver->next_time == 0){ /* first time through */
            driver->next_time = now + driver->wait_time;
            driver->last_xrun_time=now;
        }else if ((now - driver->last_wait_ust) > maxDelayTime) { /* xrun */
            //cout<<"driver->last_wait_ust "<<driver->last_wait_ust<<" now "<<now<<endl;
            //jack_error("**** iio: xrun of %ju usec", (uintmax_t)now - driver->next_time);
            cout<<"**** iio: xrun of "<<((uintmax_t)now - driver->next_time)<<"u usec last xrun was "<<now-driver->last_xrun_time<<"us ago.\n";
            driver->last_xrun_time=now;
            driver->next_time = now + driver->wait_time;
            *status=0; // xruns are fatal - but switching to non-fatal during development
            xrun=true;
            //*status=-1; // xruns are fatal
        } else /* late, but handled by our "buffer" */
            driver->next_time += driver->wait_time;
    } else {
        //cout<<"iio_driver_wait all good\n";
        driver->next_time += driver->wait_time;
    }

    driver->last_wait_ust = driver->engine->get_microseconds(); // remember the time now
    driver->engine->transport_cycle_start (driver->engine, driver->last_wait_ust);

    *delayed_usecs = 0;
    if (xrun) return 0;
    return driver->period_size;
}

static int iio_driver_run_cycle (iio_driver_t *driver) {
//    cout<<"iio_driver_run_cycle"<<endl;
    int wait_status;
    float delayed_usecs;

    jack_nframes_t nframes = iio_driver_wait(driver, -1, &wait_status, &delayed_usecs);
    if (nframes == 0) {
        /* we detected an xrun and restarted: notify clients about the delay. */
        driver->engine->delay(driver->engine, delayed_usecs);
        return 0;
    }

    if (wait_status == 0)
        return driver->engine->run_cycle(driver->engine, nframes, delayed_usecs);

    if (wait_status < 0)
        return -1;
    else
        return 0;
}

/** Given the number of samples and the sample rate, find the number of microseconds.
\param nframes The number of frames.
\param fs The sample rate.
\return The number of microseconds represented by nframes.
*/
jack_time_t getUSecs(jack_nframes_t nframes, jack_nframes_t fs) {
    cout<<"getUSecs nframes="<<nframes<<" fs="<<fs<<endl;
    return (jack_time_t) floor((((float) nframes) / fs) * 1000000.0f);
}

/**
*/
static int iio_driver_bufsize (iio_driver_t *driver, jack_nframes_t nframes) {
    cout<<"iio_driver_bufsize"<<endl;
    // Check we aren't exceeding the safety margin for the available DMA buffer ...
    float requestedUS=(float)nframes*(float)driver->sample_rate/1.e6;
    if (requestedUS>(IIO_SAFETY_FACTOR*driver->maxDelayUSecs)) {
        jack_info("Bufsize requested of duration %.3f us which is larger the the plausible buffer size of %.3f us.", requestedUS, (IIO_SAFETY_FACTOR*driver->maxDelayUSecs));
        return -1;
    }

    // Check that the read array is large enough to handle nframes and if not, then resize ...
    Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *data = static_cast<Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *>(driver->data);
    IIO *iio = static_cast<IIO *>(driver->IIO_devices);
    int N=iio->getReadArraySampleCount(*data);
    if (N<nframes) { // if it is smaller then nframes then resize
        int ret=iio->getReadArray(driver->period_size, *data); // resize the array to be able to read enough memory
        if (ret!=NO_ERROR) {
            jack_info("iio::getReadArray couldn't extend the data buffer, indicating the problem.");
            return -1;
        }
    }
    // if the data matrix is larger in columns then the number of capture channels, then resize it.
    if ((int)ceil((float)driver->capture_channels/(float)(*iio)[0].getChCnt())<data->cols())
        data->resize(data->rows(), (int)ceil((float)driver->capture_channels/(float)(*iio)[0].getChCnt()));

    // all good, adjust the new variables...
    driver->period_size = nframes;
    driver->period_usecs = driver->wait_time = getUSecs(nframes, driver->sample_rate);

    cout<<"wait_time = "<<driver->wait_time<<endl;

    /* tell the engine to change its buffer size */
    if (driver->engine->set_buffer_size (driver->engine, nframes)) {
        jack_error ("iio: cannot set engine buffer size to %d ", nframes);
        return -1;
    }

    return 0;
}

/** free all memory allocated by a driver instance
*/
static void iio_driver_delete(iio_driver_t * driver) {
    cout<<"iio_driver_delete"<<endl;
    IIO *iio = static_cast<IIO *>(driver->IIO_devices);
    if (iio)
        delete iio;
    driver->IIO_devices=NULL;
    Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *data = static_cast<Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *>(driver->data);
    if (data)
        delete data;
    driver->data=NULL;
    free(driver);
}

jack_driver_t *driver_initialize (jack_client_t *client, const JSList * params) {
    cout<<"driver_initialize "<<endl;
    IIO *iio = NULL;
    iio_driver_t *driver = (iio_driver_t *) calloc (1, sizeof (iio_driver_t));
    driver->IIO_devices=NULL; // indicate that the iio class hasn't been created yet
    driver->data=NULL; // indicate that the iio data matrix hasn't been created yet.

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

        driver->capture_channels  = IIO_DEFAULT_CAPUTURE_PORT_COUNT; // The default number of physical input channels - a very large number, to be reduced.
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
                case 'p':
                    driver->period_size = param->value.ui;
                    break;

                }
                pnode = jack_slist_next(pnode);
            }

            iio->findDevicesByChipName(chipName); // find all devices with a particular chip which are present.

            iio->printInfo(); // print out detail about the devices which were found ...

            // Find the maximum allowable delay and check whether the desired period is within the limit, otherwise report the error.
            driver->period_usecs = driver->wait_time = getUSecs(driver->period_size, driver->sample_rate);
            driver->maxDelayUSecs=(double)iio->getChannelBufferCnt()/driver->sample_rate*1.e6; // find the duration (in us) each channel can buffer

            cout<<"wait_time = "<<driver->wait_time<<endl;
            cout<<"maxDelayUSecs = "<<driver->maxDelayUSecs<<endl;


            bool bufferSizeOK=true;
            if ((float)driver->wait_time>(IIO_SAFETY_FACTOR*driver->maxDelayUSecs)) {
                cout<<"iio driver requires a wait time/period of "<<driver->wait_time<<" us, however the maximum buffer is "<<driver->maxDelayUSecs<<" us, which is more then the safety factor of "<<IIO_SAFETY_FACTOR<<".\nIndicating the problem.\n";
                jack_info("iio driver requires a wait time/period of %d us, however the maximum buffer is %f us, which is more then the safety factor of %f.\nIndicating the problem.", driver->wait_time, driver->maxDelayUSecs, IIO_SAFETY_FACTOR);
                bufferSizeOK=false; // indicate the error
            }

            // Try to create the data buffer and store it in the driver, if a problem is encountered, then report the error.
            bool dataCreationOK=true;
            Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *data = new Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic>;
            if (data) {
                driver->data=data;
                int ret=iio->getReadArray(driver->period_size, *data); // resize the array to be able to read enough memory
                if (ret!=NO_ERROR) {
                    jack_info("iio::getReadArray couldn't create the data buffer, indicating the problem.");
                    dataCreationOK=false;
                }
            } else {
                jack_info("iio driver couldn't create the data buffer, indicating the problem.");
                dataCreationOK=false;
            }

            // if the available number of ports is less then the requested number, then restrict to the number of physical ports.
            if (iio->getChCnt()<driver->capture_channels)
                driver->capture_channels=iio->getChCnt();

            // if the data matrix is larger in columns then the number of capture channels, then resize it.
            if ((int)ceil((float)driver->capture_channels/(float)(*iio)[0].getChCnt())<data->cols())
                data->resize(data->rows(), (int)ceil((float)driver->capture_channels/(float)(*iio)[0].getChCnt()));

            cout<<"matrix size rows = "<<data->rows()<<" cols = "<<data->cols()<<endl;

            string name("iio_pcm");
            if ((driver->capture_channels!=0 || driver->playback_channels!=0) && bufferSizeOK && dataCreationOK) {
                jack_info("created iio driver ... %s|%" PRIu32 "|%" PRIu32 "|%lu|%u|%u", name.c_str(), driver->sample_rate, driver->period_size, driver->wait_time, driver->capture_channels, driver->playback_channels);
                return (jack_driver_t *) driver;
            }
            // if we get here without returning we have a problem ...
            if (bufferSizeOK && dataCreationOK) // if the buffer size and the data malloc aren't the problem, then we can't find any devices.
                jack_info("couldn't find any iio devices with the chip name : %s", chipName.c_str());
        } else
            jack_error("iio driver_initialise: new IIO failed: %s: %s@%i", strerror(errno), __FILE__, __LINE__);
    } else
        jack_error("iio driver_initialise: iio_driver_t malloc() failed: %s: %s@%i", strerror(errno), __FILE__, __LINE__);

    // if we get here, there was a problem.
    iio_driver_delete((iio_driver_t *) driver);
    return NULL;
}

jack_driver_desc_t *driver_get_descriptor () {
    jack_driver_desc_t * desc;
    jack_driver_param_desc_t * params;
    unsigned int i;

    desc = calloc (1, sizeof (jack_driver_desc_t));
    strcpy (desc->name, "iio");
    desc->nparams = 3;

    params = calloc (desc->nparams, sizeof (jack_driver_param_desc_t));

    i = 0;
    strcpy (params[i].name, "chip");
    params[i].character  = 'C';
    params[i].type       = JackDriverParamString;
    strcpy (params[i].value.str, IIO_DEFAULT_CHIP);
    strcpy (params[i].short_desc, "The name of the chip to search for in the IIO devices");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "capture");
    params[i].character  = 'i';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = IIO_DEFAULT_CAPUTURE_PORT_COUNT;
    strcpy (params[i].short_desc, "Provide capture ports.");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "period");
    params[i].character  = 'p';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1024U;
    strcpy (params[i].short_desc, "Frames per period");
    strcpy (params[i].long_desc, params[i].short_desc);

    desc->params = params;

    return desc;
}

void driver_finish (jack_driver_t *driver) {
    cout<<"driver_finish"<<endl;
    iio_driver_delete((iio_driver_t *) driver);
}

const char driver_client_name[] = "iio_pcm";
