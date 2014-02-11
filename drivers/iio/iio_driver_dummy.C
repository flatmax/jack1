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
*/

#define DEBUG_LOCAL_OUTPUT
//#define DEBUG_OUTPUT

#include <IIO/IIOMMap.H>

#define __STDC_FORMAT_MACROS
#include <values.h>

//#include <stdbool.h>
//#include <string.h>
//#include <errno.h>
//#include <math.h>
//#include <stdio.h>

extern "C" {
#include "iio_driver.h"
#include "engine.h"
}

#define ELAPSED_TIME(last_time, this_time) {Debugger<<"time since last time = "<<(uintmax_t)(this_time-*last_time)<<'\n'; *last_time = this_time;}

#define IIO_DEFAULT_CHIP "AD7476A" ///< The default IIO recording chip to look for.
#define IIO_DEFAULT_READ_FS 1.e6 ///< The default IIO sample rate for the default chip.
//#define IIO_DEFAULT_READ_FS 48.e3 ///< The default IIO sample rate for the default chip.
#define IIO_DEFAULT_PERIOD_SIZE 2048 ///< The default period size is in the ms range
#define IIO_DEFAULT_PERIOD_COUNT 2 ///< The default number of periods
#define IIO_DEFAULT_CAPUTURE_PORT_COUNT MAXINT ///< The default number of capture ports is exceedingly big, trimmed down to a realistic size in driver_initialize
//#define IIO_SAFETY_FACTOR 2./3. ///< The default safety factor, allow consumption of this fraction of the available DMA buffer before we don't allow the driver to continue.
#define IIO_SAFETY_FACTOR 1. ///< The default safety factor, allow consumption of this fraction of the available DMA buffer before we don't allow the driver to continue.

static int iio_driver_attach (iio_driver_t *driver, jack_engine_t *engine) {
    printf("iio_driver_attach\n");

    // create ports
    jack_port_t * port;
    char buf[32];
    unsigned int chn;
    int port_flags;

    if (driver->engine->set_buffer_size (driver->engine, driver->period_size)) {
        jack_error ("iio: cannot set engine buffer size to %d", driver->period_size);
        return -1;
    }
    driver->engine->set_sample_rate (driver->engine, driver->sample_rate);

    port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;

    for (chn = 0; chn < driver->capture_channels; chn++) {
        snprintf (buf, sizeof(buf) - 1, "capture_%u", chn+1);

        port = jack_port_register (driver->client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
        if (!port) {
            jack_error ("iio: cannot register port for %s", buf);
            break;
        }
        //cout<<"Registered port "<<buf<<endl;

        jack_latency_range_t range;
        range.min = range.max = 1024;
        //cout<<"fix latencies, range currently set to "<<range.min<<", "<<range.max<<endl;
        jack_port_set_latency_range (port, JackCaptureLatency, &range);

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
        //cout<<"Registered port "<<buf<<endl;

        jack_latency_range_t range;
        range.min = range.max = 1024;
        //cout<<"fix latencies, range currently set to "<<range.min<<", "<<range.max<<endl;
        jack_port_set_latency_range (port, JackCaptureLatency, &range);

        driver->playback_ports = jack_slist_append (driver->playback_ports, port);
    }
    return jack_activate (driver->client);
}

static int iio_driver_detach (iio_driver_t *driver, jack_engine_t *engine) {
    JSList *node;
    printf("iio_driver_detach\n");
    if (driver->engine == 0)
        return -1;

    // unregister all ports
    for (node = driver->capture_ports; node; node = jack_slist_next(node)) {
        jack_port_unregister(driver->client, ((jack_port_t *) node->data));
    }

    jack_slist_free(driver->capture_ports);
    driver->capture_ports = 0;

    for (node = driver->playback_ports; node; node = jack_slist_next(node)) {
        jack_port_unregister(driver->client, ((jack_port_t *) node->data));
    }

    jack_slist_free(driver->playback_ports);
    driver->playback_ports = 0;

    driver->engine = 0;
    return 0;
}

static int iio_driver_start (iio_driver_t *driver) {
    printf("iio_driver_start::   enabling IIO : enable(true)\n");

    zeroTime(&NEXT_TIME); // driver->next_wakeup.tv_sec = 0; which is the same as driver->next_time = 0;

    return 0;
}

static int iio_driver_stop (iio_driver_t *driver) {
    printf("iio_driver_start:: disabling IIO : enable(false)\n");

    return 0;
}

static int iio_driver_read(iio_driver_t *driver, jack_nframes_t nframes) {
    JSList *node;
    channel_t chn;
    jack_nframes_t i;

    //Debugger<<"iio_driver_read\n";

    if (nframes > 0) {
        ////Debugger<<"iio_driver_read nframes = "<<nframes<<"\n";
//        for (jack_nframes_t i=0; i<nframes; i++){
//            cout<<(float)(*data)(i,0)<<endl;
//        cout<<endl;

        // write to the connected capture ports ...
        node = (JSList *)driver->capture_ports;
        for (chn = 0; node; node = (JSList *)jack_slist_next(node), chn++) {

            //jack_port_t *port = static_cast<jack_port_t *>(node->data);
            jack_port_t *port = (jack_port_t*)(node->data);

            if (!jack_port_connected (port)) /* no-copy optimization */
                continue;

            //jack_default_audio_sample_t *buf = static_cast<jack_default_audio_sample_t *>(jack_port_get_buffer (port, nframes));
            jack_default_audio_sample_t *buf = (jack_default_audio_sample_t *)(jack_port_get_buffer (port, nframes));
            for (i=0; i<nframes; i++) {
                //cout<<"row = "<<i*devChCnt+rowOffset<<" col = "<<col<<endl;
                //buf[i]=(*data)(i*devChCnt+rowOffset, col)*100.;
                buf[i]=(float)i/(float)nframes;
                //cout<<(*data)(i*devChCnt+rowOffset, col)<<'\t'<<buf[i]<<'\n';
            }
        }
        //Debugger<<" spent "<< (driver->engine->get_microseconds()-driver->debug_last_time)<<" us waiting for lock and copying data over\n";
    }
    return 0;
}

static int iio_driver_write (iio_driver_t *driver, jack_nframes_t nframes) {
    //Debugger<<"iio_driver_write nframes = "<<nframes<<"\n";
//    if (nframes>0){
//        //Debugger<<"iio_driver_write nframes = "<<nframes<<"\n";
//    }
    return 0;
}

static int iio_driver_null_cycle (iio_driver_t *driver, jack_nframes_t nframes) {
    //Debugger<<"iio_driver_null_cycle\n";

    return 0;
}

/** The driver_wait function to work out if we have used more time then available to process one cycle.
    This is written once for either timespec (HAVE_CLOCK_GETTIME) or jack_time_t, see the unified_jack_time.h file.

    This function manages the time now and the next expected time to return. The return happens once per block of time (period_size).

    The general idea of *_driver_wait is to do the following :
    a] Mark the time now if this is the first time through or if an overrun previously happened.
    b] If an overrun is detected (we have exceeded the maximum time held in our audio buffers) then indicate.
    c] If we are late but not dangerously, then keep going.
    d] If we are early then sleep a little to allow clients enough time to process.

    The effect of 'c]' and 'd]' is to create time pumping. Theoretically we will always be either a little over or under time, and it will be difficult to match audio block
    time exactly. This doesn't matter if the time pumping is a small fraction of our block time. This means that the clients will have slightly more or less then audio block
    time to process.
*/
static jack_nframes_t iio_driver_wait(iio_driver_t *driver, int extra_fd, int *status, float *delayed_usecs) {
	jack_nframes_t nframes = driver->period_size;
    //Debugger<<"iio_driver_wait\n";
    *delayed_usecs = 0;
    *status = 0;

    timeType now; getNow(&now); // the time right now

	if (compareTimesLt(NEXT_TIME, now)) { // NEXT_TIME < now  ... this is a problem as the next time should be >= now
        //printf("iio_driver_wait NOT good\n");
        if (compareTimeZero(NEXT_TIME)) { /* first time through */
            //DebuggerLocal<<"iio_driver_first time - OK\n";
            //printf("first time through\n\n\n");
			getNow(&NEXT_TIME); // reset the next time to now, will be incremented later
        } else if (timeDiffUsComp(now, driver->next_wakeup, driver->maxDelayUSecs)) { /* xrun = (now - NEXT_TIME) > maxDelayTime */
            ////Debugger<<"NEXT_TIME "<<NEXT_TIME<<" now "<<now<<endl;
            jack_error("**** iio: xrun of %ju usec", timeDiffUs(now, NEXT_TIME));
            nframes=0; // indicated the xrun
            zeroTime(&NEXT_TIME);  // reset the next time to zero because of the overrun, we don't know when to start again.
            //*status=-1; // xruns are fatal
        } else /* late, but handled by our "buffer" */
            ;
	} else { // now sleep to ensure we give the clients enough time to process
        *status = nanoSleep(NEXT_TIME, delayed_usecs);
        //printf("iio_driver_wait all good\n");
    }

    if (nframes!=0) // if there is no xrun, then indicate the next expected time to land in this funciton.
        NEXT_TIME=addTimes(NEXT_TIME, driver->wait_time);

	driver->last_wait_ust = driver->engine->get_microseconds ();
	driver->engine->transport_cycle_start (driver->engine, driver->last_wait_ust);

	return nframes;
}

static int iio_driver_run_cycle (iio_driver_t *driver) {
    //Debugger<<"iio_driver_run_cycle\n";

    int wait_status;
    float delayed_usecs;

    jack_nframes_t nframes = iio_driver_wait(driver, -1, &wait_status, &delayed_usecs);
    if (nframes == 0) {
        /* we detected an xrun and restarted: notify clients about the delay. */
        //DebuggerLocal<<"iio_driver_run_cycle :: xrun detected, delaying\n";
        driver->engine->delay(driver->engine, delayed_usecs);
        return 0;
    }

    if (wait_status == 0) {
        //Debugger<<"iio_driver_run_cycle :: calling engine->run_cycle, nframes="<<nframes<<" delayed_usecs="<<delayed_usecs<<"\n";
        return driver->engine->run_cycle(driver->engine, nframes, delayed_usecs);
    }

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
    //Debugger<<"getUSecs nframes="<<nframes<<" fs="<<fs<<'\n';
    return (jack_time_t) floor((((float) nframes) / fs) * 1000000.0f);
}

/**
*/
static int iio_driver_bufsize (iio_driver_t *driver, jack_nframes_t nframes) {
    printf("iio_driver_bufsize\n");

    jack_nframes_t period_sizeOrig=driver->period_size;
    jack_time_t period_usecsOrig = driver->period_usecs;
    unsigned long wait_timeOrig = driver->wait_time;
    double maxDelayUSecsOrig=driver->maxDelayUSecs;

    driver->period_size = nframes;
    driver->period_usecs = driver->wait_time = getUSecs(nframes, driver->sample_rate);

    //Debugger<<"wait_time = "<<driver->wait_time<<endl;

    /* tell the engine to change its buffer size */
    if (driver->engine->set_buffer_size(driver->engine, nframes)) {
        jack_error ("iio: cannot set engine buffer size to %d ", nframes);
        driver->period_size=period_sizeOrig;
        driver->period_usecs=period_usecsOrig;
        driver->wait_time=wait_timeOrig;
        driver->maxDelayUSecs=maxDelayUSecsOrig;
        return -1;
    }

    return 0;
}

/** free all memory allocated by a driver instance
*/
static void iio_driver_delete(iio_driver_t * driver) {
    printf("iio_driver_delete\n");

    free(driver);
}

jack_driver_t *driver_initialize (jack_client_t *client, const JSList * params) {
    printf("driver_initialize \n");

    iio_driver_t *driver = (iio_driver_t *) calloc (1, sizeof (iio_driver_t));
    driver->IIO_devices=NULL; // indicate that the iio class hasn't been created yet
    if (driver) {
        jack_driver_nt_init((jack_driver_nt_t *) driver);

        driver->write         = (JackDriverReadFunction)       iio_driver_write;
        driver->read          = (JackDriverReadFunction)       iio_driver_read;
        driver->null_cycle    = (JackDriverNullCycleFunction)  iio_driver_null_cycle;
        driver->nt_attach     = (JackDriverNTAttachFunction)   iio_driver_attach;
        driver->nt_stop       = (JackDriverNTStopFunction)     iio_driver_stop;
        driver->nt_start      = (JackDriverNTStartFunction)    iio_driver_start;
        driver->nt_detach     = (JackDriverNTDetachFunction)   iio_driver_detach;
        driver->nt_bufsize    = (JackDriverNTBufSizeFunction)  iio_driver_bufsize;
        driver->nt_run_cycle  = (JackDriverNTRunCycleFunction) iio_driver_run_cycle;

        driver->engine = NULL; // setup the required driver variables.
        driver->client = client;
        driver->last_wait_ust = 0;

        driver->sample_rate = IIO_DEFAULT_READ_FS; // IIO sample rate is fixed.
        driver->period_size = IIO_DEFAULT_PERIOD_SIZE;
        driver->nperiods    = IIO_DEFAULT_PERIOD_COUNT;

        driver->capture_channels  = IIO_DEFAULT_CAPUTURE_PORT_COUNT; // The default number of physical input channels - a very large number, to be reduced.
        driver->capture_ports     = NULL;
        driver->playback_channels = 0; // currently doesn't support playback.
        driver->playback_ports    = NULL;


        const JSList *pnode = params; // param pointer
        while (pnode != NULL) {
            const jack_driver_param_t *param = (const jack_driver_param_t *) pnode->data;
            switch (param->character) {

            case 'i': // we are specifying the number of capture channels
                driver->capture_channels = param->value.ui;
                break;
            case 'p':
                driver->period_size = param->value.ui;
                break;
            case 'n':
                driver->nperiods = param->value.ui;
                break;

            }
            pnode = jack_slist_next(pnode);
        }

        driver->period_usecs = driver->wait_time = getUSecs(driver->period_size, driver->sample_rate);
        driver->maxDelayUSecs=driver->period_usecs*driver->nperiods; // the mmap max delay is currently unknown
        cout<<"max delay = "<<driver->maxDelayUSecs<<" us"<<endl;
        driver->capture_channels=4;
        jack_info("created DUMMY iio driver ... dummy_iio|%" PRIu32 "|%" PRIu32 "|%lu|%u|%u", driver->sample_rate, driver->period_size, driver->wait_time, driver->capture_channels, driver->playback_channels);
        return (jack_driver_t *) driver;

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

    desc = (jack_driver_desc_t *)calloc (1, sizeof (jack_driver_desc_t));
    strcpy (desc->name, "iio");
    desc->nparams = 4;

    params = (jack_driver_param_desc_t *)calloc (desc->nparams, sizeof (jack_driver_param_desc_t));

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

    i++;
    strcpy (params[i].name, "nperiods");
    params[i].character  = 'n';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 2U;
    strcpy (params[i].short_desc, "Number of periods of playback latency");
    strcpy (params[i].long_desc, params[i].short_desc);

    desc->params = params;

    return desc;
}

void driver_finish (jack_driver_t *driver) {
    printf("driver_finish\n");

    iio_driver_delete((iio_driver_t *) driver);
}

const char driver_client_name[] = "iio_pcm";
