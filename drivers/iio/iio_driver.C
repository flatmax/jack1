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

extern "C" {
#include "iio_driver.h"
#include "engine.h"
}

#define ELAPSED_TIME(last_time, this_time) {Debugger<<"time since last time = "<<(uintmax_t)(this_time-*last_time)<<'\n'; *last_time = this_time;}

#define IIO_DEFAULT_CHIP "AD7476A" ///< The default IIO recording chip to look for.
#define IIO_DEFAULT_READ_FS 1.e6 ///< The default IIO sample rate for the default chip.
#define IIO_DEFAULT_PERIOD_SIZE 2048 ///< The default period size is in the ms range
#define IIO_DEFAULT_PERIOD_COUNT 2 ///< The default number of periods
#define IIO_DEFAULT_CAPUTURE_PORT_COUNT MAXINT ///< The default number of capture ports is exceedingly big, trimmed down to a realistic size in driver_initialize
//#define IIO_SAFETY_FACTOR 2./3. ///< The default safety factor, allow consumption of this fraction of the available DMA buffer before we don't allow the driver to continue.
#define IIO_SAFETY_FACTOR 1. ///< The default safety factor, allow consumption of this fraction of the available DMA buffer before we don't allow the driver to continue.

static int iio_driver_attach (iio_driver_t *driver, jack_engine_t *engine) {
    //DebuggerLocal<<"iio_driver_attach\n";
    ELAPSED_TIME(&(driver->debug_last_time), driver->engine->get_microseconds())

    // open the IIO subsystem
    IIOMMap *iio = static_cast<IIOMMap *>(driver->IIO_devices);
    //Debugger<<"iio_driver_attach : about to open the IIOMMap device using "<<driver->nperiods<<" of size "<<driver->period_size<<" frames each.\n";
    int ret=iio->open(driver->nperiods, driver->period_size); // try to open all IIO devices
    if (ret!=NO_ERROR)
        return -1;

    driver->maxDelayUSecs=IIO_SAFETY_FACTOR*iio->getMaxDelay(driver->sample_rate)*1.e6; // find the duration (in us) each channel can buffer
    //Debugger<<"maxDelayUSecs = "<<driver->maxDelayUSecs<<endl;
    if ((float)driver->wait_time>(IIO_SAFETY_FACTOR*driver->maxDelayUSecs)) {
        //Debugger<<"iio driver requires a wait time/period of "<<driver->wait_time<<" us, however the maximum buffer is "<<driver->maxDelayUSecs<<" us, which is more then the safety factor of "<<IIO_SAFETY_FACTOR<<".\nIndicating the problem.\n";
        jack_info("iio driver requires a wait time/period of %d us, however the maximum buffer is %f us, which is more then the safety factor of %f.\nIndicating the problem.", driver->wait_time, driver->maxDelayUSecs, IIO_SAFETY_FACTOR);
        iio->close();
        return -1;
    }

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
        range.min = range.max = (int)iio->getMaxDelay(1.);
        cout<<"fix latencies, range currently set to "<<range.min<<", "<<range.max<<endl;
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
        range.min = range.max = (int)iio->getMaxDelay(1.);

        //cout<<"fix latencies, range currently set to "<<range.min<<", "<<range.max<<endl;
        jack_port_set_latency_range (port, JackCaptureLatency, &range);

        driver->playback_ports = jack_slist_append (driver->playback_ports, port);
    }
    return jack_activate (driver->client);
}

static int iio_driver_detach (iio_driver_t *driver, jack_engine_t *engine) {
    DebuggerLocal<<"iio_driver_detach\n";
    ELAPSED_TIME(&(driver->debug_last_time), driver->engine->get_microseconds())

    IIOMMap *iio = static_cast<IIOMMap *>(driver->IIO_devices);
    iio->enable(false); // stop the DMA
    iio->close(); // close the IIO system

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
    DebuggerLocal<<"iio_driver_start::   enabling IIO : enable(true)\n";
    ELAPSED_TIME(&(driver->debug_last_time), driver->engine->get_microseconds())

    IIOMMap *iio = static_cast<IIOMMap *>(driver->IIO_devices);
    int ret;
    if ((ret=iio->enable(true))!=NO_ERROR) { // start the DMA
        iio->close();
        return ret;
    }

	zeroTime(&NEXT_TIME); // driver->next_wakeup.tv_sec = 0; which is the same as driver->next_time = 0;
    return 0;
}

static int iio_driver_stop (iio_driver_t *driver) {
    DebuggerLocal<<"iio_driver_start:: disabling IIO : enable(false)"<<endl;
    ELAPSED_TIME(&(driver->debug_last_time), driver->engine->get_microseconds())

    IIOMMap *iio = static_cast<IIOMMap *>(driver->IIO_devices);
    iio->enable(false); // stop the DMA
    return 0;
}

static int iio_driver_read(iio_driver_t *driver, jack_nframes_t nframes) {
    Debugger<<"iio_driver_read\n";
    ELAPSED_TIME(&(driver->debug_last_time), driver->engine->get_microseconds())

    if (nframes > 0) {
        ////Debugger<<"iio_driver_read nframes = "<<nframes<<"\n";
        IIOMMap *iio = static_cast<IIOMMap *>(driver->IIO_devices);
        uint devChCnt=(*iio)[0].getChCnt();

        // read from the IIO devices ...
        // Ret the data array pointer to use for reading.
        Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *data = static_cast<Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *>(driver->data);
        int ret=iio->read(nframes, *data);
        if (ret!=NO_ERROR)
            return -1;

        // write to the connected capture ports ...
        JSList *node = (JSList *)driver->capture_ports;
        for (channel_t chn = 0; node; node = (JSList *)jack_slist_next(node), chn++) {

            jack_port_t *port = static_cast<jack_port_t *>(node->data);

            if (!jack_port_connected (port)) /* no-copy optimization */
                continue;

//            int col=chn/devChCnt;
//            int rowOffset=chn%devChCnt;

            jack_default_audio_sample_t *buf = static_cast<jack_default_audio_sample_t *>(jack_port_get_buffer (port, nframes));
            for (jack_nframes_t i=0; i<nframes; i++){
                //cout<<"row = "<<i*devChCnt+rowOffset<<" col = "<<col<<endl;
                //buf[i]=(*data)(i*devChCnt+rowOffset, col)*100.;
                buf[i]=(float)i/(float)nframes;
                //cout<<(*data)(i*devChCnt+rowOffset, col)<<'\t'<<buf[i]<<'\n';
            }
        }
        Debugger<<" spent "<< (driver->engine->get_microseconds()-driver->debug_last_time)<<" us waiting for lock and copying data over\n";
    }
    return 0;
}

static int iio_driver_write (iio_driver_t *driver, jack_nframes_t nframes) {
    if (nframes>0){
        Debugger<<"iio_driver_write nframes = "<<nframes<<"\n";
    }
    return 0;
}

static int iio_driver_null_cycle (iio_driver_t *driver, jack_nframes_t nframes) {
    //Debugger<<"iio_driver_null_cycle\n";
    ELAPSED_TIME(&(driver->debug_last_time), driver->engine->get_microseconds())

    if (nframes>0) {
        IIOMMap *iio = static_cast<IIOMMap *>(driver->IIO_devices);
        uint devChCnt=(*iio)[0].getChCnt();

        // read from the IIO devices ...
        // Ret the data array pointer to use for reading.
        Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *data = static_cast<Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *>(driver->data);
        int ret=iio->read(nframes, *data);
        if (ret!=NO_ERROR)
            return -1;

        // output buffers are currently not handled ... in future, add output handling here.
    }
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
    ELAPSED_TIME(&(driver->debug_last_time), driver->engine->get_microseconds())

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
static int iio_driver_bufsize(iio_driver_t *driver, jack_nframes_t nframes) {
    //DebuggerLocal<<"iio_driver_bufsize"<<endl;
    ELAPSED_TIME(&(driver->debug_last_time), driver->engine->get_microseconds())

    IIOMMap *iio = static_cast<IIOMMap *>(driver->IIO_devices);
//    int newDMABufSize=iio->setChannelBufferCnt(nframes*2); // ensure we have a periods head room
//    Debugger<<"new DMA Buf. Size ="<<newDMABufSize*(*iio)[0].getChCnt()<<endl;

    jack_nframes_t period_sizeOrig=driver->period_size;
    jack_time_t period_usecsOrig = driver->period_usecs;
    unsigned long wait_timeOrig = driver->wait_time;
    double maxDelayUSecsOrig=driver->maxDelayUSecs;

    driver->period_size = nframes;
    driver->period_usecs = driver->wait_time = getUSecs(nframes, driver->sample_rate);

    Debugger<<"wait_time = "<<driver->wait_time<<endl;

    driver->maxDelayUSecs=IIO_SAFETY_FACTOR*iio->getMaxDelay(driver->sample_rate)*1.e6; // find the duration (in us) each channel can buffer

    Debugger<<"maxDelayUSecs = "<<driver->maxDelayUSecs<<endl;

    if ((float)driver->wait_time>(IIO_SAFETY_FACTOR*driver->maxDelayUSecs)) {
        Debugger<<"iio driver requires a wait time/period of "<<driver->wait_time<<" us, however the maximum buffer is "<<driver->maxDelayUSecs<<" us, which is more then the safety factor of "<<IIO_SAFETY_FACTOR<<".\nIndicating the problem.\n";
        jack_info("iio driver requires a wait time/period of %d us, however the maximum buffer is %f us, which is more then the safety factor of %f.\nIndicating the problem.", driver->wait_time, driver->maxDelayUSecs, IIO_SAFETY_FACTOR);
        driver->period_size=period_sizeOrig;
        driver->period_usecs=period_usecsOrig;
        driver->wait_time=wait_timeOrig;
        driver->maxDelayUSecs=maxDelayUSecsOrig;
        return -1;
    }

//    if (newDMABufSize!=nframes)
//        return -1;

    // Check we aren't exceeding the safety margin for the available DMA buffer ...
    float requestedUS=(float)nframes/(float)driver->sample_rate*1.e6;
    if (requestedUS>driver->maxDelayUSecs) {
        jack_info("Bufsize requested of duration %.3f us which is larger the the plausible buffer size of %.3f us.", requestedUS, (IIO_SAFETY_FACTOR*driver->maxDelayUSecs));
        driver->period_size=period_sizeOrig;
        driver->period_usecs=period_usecsOrig;
        driver->wait_time=wait_timeOrig;
        driver->maxDelayUSecs=maxDelayUSecsOrig;
        return -1;
    }

    // resize the input data storage buffers ...
//    int ret=iio->setSampleCountChannelCount(nframes, driver->capture_channels);
//    if (ret!=NO_ERROR) {
//        jack_info("iio::getReadArray couldn't extend the data buffer, indicating the problem.");
//        return -1;
//    }
    // Check that the read array is large enough to handle nframes and if not, then resize ...
    Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *data = static_cast<Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *>(driver->data);
    uint N=iio->getReadArraySampleCount(*data);
    if (N<nframes) { // if it is smaller then nframes then resize
        int ret=iio->getReadArray(driver->period_size, *data); // resize the array to be able to read enough memory
        if (ret!=NO_ERROR) {
            jack_info("iio::getReadArray couldn't extend the data buffer, indicating the problem.");
            driver->period_size=period_sizeOrig;
            driver->period_usecs=period_usecsOrig;
            driver->wait_time=wait_timeOrig;
            driver->maxDelayUSecs=maxDelayUSecsOrig;
            ret=iio->getReadArray(driver->period_size, *data); // Try to resize the array to be able to read enough memory
            if (ret!=NO_ERROR)
                jack_info("iio::getReadArray couldn't reset the data buffer to the original size.");
            return -1;
        }
    }
    // if the data matrix is larger in columns then the number of capture channels, then resize it.
    int colCnt=(int)ceil((float)driver->capture_channels/(float)(*iio)[0].getChCnt()); // check whether we require less then the available number of channels
    if (colCnt<data->cols())
        data->resize(data->rows(), colCnt);

    // resize the memory mapped blocks
    if (iio->resizeMMapBlocks(driver->nperiods, driver->period_size) != NO_ERROR) {
        jack_error ("iio: cannot resize the mmap buffers to %d ", nframes);
        driver->period_size=period_sizeOrig;
        driver->period_usecs=period_usecsOrig;
        driver->wait_time=wait_timeOrig;
        driver->maxDelayUSecs=maxDelayUSecsOrig;
        if (iio->getReadArray(driver->period_size, *data)!=NO_ERROR) // Try to resize the array to be able to read enough memory
            jack_info("iio::getReadArray couldn't reset the data buffer to the original size.");
        if (colCnt<data->cols())
            data->resize(data->rows(), colCnt);
        if (iio->resizeMMapBlocks(driver->nperiods, driver->period_size) != NO_ERROR)
            jack_error ("iio: could not reset the mmap buffer size to %d : this may cause problems.", driver->period_size);
        return -1;
    }

    /* tell the engine to change its buffer size */
    if (driver->engine->set_buffer_size(driver->engine, nframes)) {
        jack_error ("iio: cannot set engine buffer size to %d ", nframes);
        driver->period_size=period_sizeOrig;
        driver->period_usecs=period_usecsOrig;
        driver->wait_time=wait_timeOrig;
        driver->maxDelayUSecs=maxDelayUSecsOrig;
        if (iio->getReadArray(driver->period_size, *data)!=NO_ERROR) // Try to resize the array to be able to read enough memory
            jack_info("iio::getReadArray couldn't reset the data buffer to the original size.");
        if (colCnt<data->cols())
            data->resize(data->rows(), colCnt);
        if (iio->resizeMMapBlocks(driver->nperiods, driver->period_size) != NO_ERROR)
            jack_error ("iio: could not reset the mmap buffer size to %d : this may cause problems.", driver->period_size);
        return -1;
    }

    return 0;
}

/** free all memory allocated by a driver instance
*/
static void iio_driver_delete(iio_driver_t * driver) {
    DebuggerLocal<<"iio_driver_delete"<<endl;
    ELAPSED_TIME(&(driver->debug_last_time), driver->engine->get_microseconds())

    IIOMMap *iio = static_cast<IIOMMap *>(driver->IIO_devices);
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
    DebuggerLocal<<"driver_initialize "<<endl;

    IIOMMap *iio = NULL;
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

        iio = new IIOMMap; // initialise the IIO system.
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
                case 'n':
                    driver->nperiods = param->value.ui;
                    break;

                }
                pnode = jack_slist_next(pnode);
            }

            if (iio->findDevicesByChipName(chipName)!=NO_ERROR) { // find all devices with a particular chip which are present.
                jack_info("\nThe iio driver found no devices by the name %s\n", chipName.c_str());
                return NULL;
            }

            if (iio->getDeviceCnt()<1) { // If there are no devices found by that chip name, then indicate.
                jack_info("\nThe iio driver found no devices by the name %s\n", chipName.c_str());
                return NULL;
            }

            iio->printInfo(); // print out detail about the devices which were found ...

            // Find the maximum allowable delay and check whether the desired period is within the limit, otherwise report the error.
            driver->period_usecs = driver->wait_time = getUSecs(driver->period_size, driver->sample_rate);
            Debugger<<"wait_time = "<<driver->wait_time<<endl;

            driver->maxDelayUSecs=-1.e6; // the mmap max delay is currently unknown

            // if the available number of ports is less then the requested number, then restrict to the number of physical ports.
            if (iio->getChCnt()<driver->capture_channels)
                driver->capture_channels=iio->getChCnt();

            // Try to create the data buffer and store it in the driver, if a problem is encountered, then report the error.
            bool dataCreationOK=true;
//            // resize the input data storage buffers ...
//            int ret=iio->setSampleCountChannelCount(driver->period_size, driver->capture_channels);
//            if (ret!=NO_ERROR) {
//                jack_info("iio driver couldn't create the data buffer, indicating the problem.");
//                dataCreationOK=false;
//            }
            int colCnt=(int)ceil((float)driver->capture_channels/(float)(*iio)[0].getChCnt()); // check whether we require less then the available number of channels
            Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic> *data = new Eigen::Array<unsigned short int, Eigen::Dynamic, Eigen::Dynamic>;
            if (data) {
                driver->data=data;
                int ret=iio->getReadArray(driver->period_size, *data); // resize the array to be able to read enough memory
                if (ret!=NO_ERROR) {
                    jack_info("iio::getReadArray couldn't create the data buffer, indicating the problem.");
                    dataCreationOK=false;
                }
                if (data->cols()>colCnt) // resize the data columns to match the specified number of columns (channels / channels per device)
                    data->resize(data->rows(), colCnt);

            } else {
                jack_info("iio driver couldn't create the data buffer, indicating the problem.");
                dataCreationOK=false;
            }
//
//            // if the data matrix is larger in columns then the number of capture channels, then resize it.
//            if ((int)ceil((float)driver->capture_channels/(float)(*iio)[0].getChCnt())<data->cols())
//                data->resize(data->rows(), (int)ceil((float)driver->capture_channels/(float)(*iio)[0].getChCnt()));
//
//            Debugger<<"matrix size rows = "<<data->rows()<<" cols = "<<data->cols()<<endl;

            string name("iio_pcm");
            if ((driver->capture_channels!=0 || driver->playback_channels!=0) && dataCreationOK) {
                jack_info("created iio driver ... %s|%" PRIu32 "|%" PRIu32 "|%lu|%u|%u", name.c_str(), driver->sample_rate, driver->period_size, driver->wait_time, driver->capture_channels, driver->playback_channels);
                return (jack_driver_t *) driver;
            }
            // if we get here without returning we have a problem ...
            if (dataCreationOK) // if the buffer size and the data malloc aren't the problem, then we can't find any devices.
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
    DebuggerLocal<<"driver_finish"<<endl;

    iio_driver_delete((iio_driver_t *) driver);
}

const char driver_client_name[] = "iio_pcm";
