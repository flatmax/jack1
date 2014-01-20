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
Date: 2014.01.20

Test like so :
LD_LIBRARY_PATH=jack1/libjack/.libs ./drivers/iio/.libs/JackIIODriverTest
*/

#include "JackClient.H"
#include <iostream>
using namespace std;

#include <math.h>
#include <unistd.h>

/** This Jack Client is used to test the IIO driver.
*/
class JackIIODriverTestClient : public JackClient {
    int processAudio(jack_nframes_t nframes) { ///< The Jack client callback
        cout<<"JackIIODriverTestClient::processAudio nframes = "<<nframes<<"\n";

//        //	print input data to stdout
//        for (uint i=0; i<inputPorts.size(); i++) {
//            jack_default_audio_sample_t *in = ( jack_default_audio_sample_t* ) jack_port_get_buffer ( inputPorts[i], nframes );
//            for (uint j=0; j<nframes; j++)
//                rms+=in[j]*in[j];
//            cout<<"input ch "<<i<<" rms = "<<sqrt(rms/nframes)<<'\t';
//        }
//        cout<<endl;

        return 0;
    }
public:

//    ///Constructor
//    JackIIODriverTestClient() {        phase=0.;
//    }

};

int main(int argc, char *argv[]) {
    JackIIODriverTestClient jackClient; // init the jack client for testing the IIO driver

    // connect to the jack server
    int res=jackClient.connect("jack iio test client");
    if (res!=0)
        return JackClientDebug().evaluateError(res);

    cout<<"Jack : sample rate set to : "<<jackClient.getSampleRate()<<" Hz"<<endl;
    cout<<"Jack : block size set to : "<<jackClient.getBlockSize()<<" samples"<<endl;

    res=jackClient.createPorts("in ", 2, "out ", 0);
    if (res!=0)
        return JackClientDebug().evaluateError(res);

    // start the client
    res=jackClient.startClient(2, 0, true);
    if (res!=0 && res!=JACK_HAS_NO_PLAYBACK_PORTS_ERROR)
        return JackClientDebug().evaluateError(res);

    sleep(10); // sleep for 10 seconds ... Microsoft users may have to use a different sleep function
    return 0;
}

