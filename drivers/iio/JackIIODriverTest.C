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
#include <values.h>

#include <OptionParser.H>
#include <Sox.H>

#include <Thread.H>

/** This Jack Client is used to test the IIO driver.
*/
class JackIIODriverTestClient : public JackClient, public Cond {
    Sox sox; ///< Write to file class

    Eigen::Array<jack_default_audio_sample_t, Eigen::Dynamic, Eigen::Dynamic> data; ///< The data received from Jack

    long int sampleCount;

    int processAudio(jack_nframes_t nframes) { ///< The Jack client callback
        //cout<<"JackIIODriverTestClient::processAudio nframes = "<<nframes<<"\n";

        if (sampleCount>0){

        if (data.rows()!=nframes){
            cout<<"current data size = "<<data.rows()<<','<<data.cols()<<  " requesting nframes = "<<nframes<<" resizing the data buffer"<<endl;
            data.resize(nframes, data.cols());
        }

        //	print input data to stdout
        for (uint i=0; i<inputPorts.size(); i++) {
            jack_default_audio_sample_t *in = ( jack_default_audio_sample_t* ) jack_port_get_buffer ( inputPorts[i], nframes );
            jack_default_audio_sample_t *channelData=data.col(i).data();
            for (uint j=0; j<nframes; j++){
                channelData[j]=in[j];
                //cout<<in[j]<<'\t'<<channelData[j]<<'\n';
            }
        }

        int written=sox.write(data);
        if (written!=nframes*data.cols()) {
            if (written>0){
                cout<<"current data size = "<<data.rows()<<','<<data.cols()<<  " requesting nframes = "<<nframes<<" resizing the data buffer"<<endl;
                cout<<"Attempted to write "<<nframes<<" samples (per channel) to the audio file, however only "<<written<<" samples were written. Exiting!"<<endl;
            } else {
                cout<<SoxDebug().evaluateError(written)<<endl;
                cout<<"Output matrix size (rows, cols) = ("<<data.rows()<<", "<<data.cols()<<")"<<endl;
                cout<<"Error writing, exiting."<<endl;
            }
        }

        sampleCount-=nframes;
        if (sampleCount<=0){
            sox.closeWrite();
            // signal the main thread that we are finished.
            lock(); // lock the mutex, indicate the condition and wake the thread.
            complete=true;
            signal(); // Wake the WaitingThread
            unLock(); // Unlock so the WaitingThread can continue.
        }
        }
        return 0;
    }

    /** resize the data matrix.
    */
    int bufferSizeChange(jack_nframes_t nframes){
        cout<<"JackIIODriverTestClient::bufferSizeChange nframes="<<nframes<<endl;
        data.resize(nframes,data.cols());
        return NO_ERROR;
    }

public:
    bool complete;

    ///Constructor
    JackIIODriverTestClient(char *name, float fs, int chCnt) : JackClient() {
        cout<<"JackIIODriverTestClient::JackIIODriverTestClient name="<<name<<"\nfs="<<fs<<"\nchCnt="<<chCnt<<endl;
        if (sox.openWrite(name, fs, chCnt, MAXSHORT)!=NO_ERROR)
            exit(-1);
        data.resize(1,chCnt);
        sampleCount=0;
        complete=false;
    }

    virtual ~JackIIODriverTestClient(){
        sox.closeWrite();
    }

    /** Reset the sample count.
    \param fs the sample rate
    \param duration  The time to run for.
    */
    void reset(float fs, float duration){
        sampleCount=(int)ceil(duration*fs);
        cout<<"reading "<<sampleCount<<" samples"<<endl;
        lock();
        complete=false;
        unLock();
    }
};

int printUsage(string name, int chCnt, float T) {
    cout<<name<<" : An application to stream input from IIO devices to file."<<endl;
    cout<<"Usage:"<<endl;
    cout<<"\t "<<name<<" [options] outFileName"<<endl;
    cout<<"\t -i : The number of channels to open, if the available number is less, then it is reduced to the available : (-i "<<chCnt<<")"<<endl;
    cout<<"\t -t : The duration to sample for : (-t "<<T<<")"<<endl;
    Sox sox;
    vector<string> formats=sox.availableFormats();
    cout<<"The known output file extensions (output file formats) are the following :"<<endl;
    for (uint i=0; i<formats.size(); i++)
        cout<<formats[i]<<' ';
    cout<<endl;
    return 0;
}

int main(int argc, char *argv[]) {


    // defaults
    int chCnt=4;
    float T=1.; // seconds

    OptionParser op;

    int i=0;
    string help;
    if (op.getArg<string>("h", argc, argv, help, i=0)!=0)
        return printUsage(argv[0], chCnt, T);
    if (op.getArg<string>("help", argc, argv, help, i=0)!=0)
        return printUsage(argv[0], chCnt, T);
    if (argc<2)
        return printUsage(argv[0], chCnt, T);

    if (op.getArg<int>("i", argc, argv, chCnt, i=0)!=0)
        ;

    if (op.getArg<float>("t", argc, argv, T, i=0)!=0)
        ;

    cout<<"\nNumber of channels i="<<chCnt<<endl;
    cout<<"Duration t="<<T<<" seconds"<<endl;
    float fs=1.e6;
    cout<<"using a sample rate = "<<fs<<" Hz"<<endl;
    cout<<endl;

    JackIIODriverTestClient jackClient(argv[argc-1], fs, chCnt); // init the jack client for testing the IIO driver

    cout<<"Connecting to jackd"<<endl;
    // connect to the jack server
    int res=jackClient.connect("jack iio test client");
    if (res!=0)
        return JackClientDebug().evaluateError(res);

    cout<<"Jack : sample rate set to : "<<jackClient.getSampleRate()<<" Hz"<<endl;
    cout<<"Jack : block size set to : "<<jackClient.getBlockSize()<<" samples"<<endl;

    cout<<"Creating ports ... "<<endl;
    res=jackClient.createPorts("in ", chCnt, "out ", 0);
    if (res!=0)
        return JackClientDebug().evaluateError(res);

    jackClient.reset(fs, T);

    //jackClient.setBlockSize(jackClient.getBlockSize());

    cout<<"Starting the client and connecting to the ports"<<endl;
    // start the client
    res=jackClient.startClient(chCnt, 0, true);
    if (res!=0 && res!=JACK_HAS_NO_PLAYBACK_PORTS_ERROR)
        return JackClientDebug().evaluateError(res);


    cout<<"waiting for the client to finish"<<endl;
    // wait for the client to finish.
    jackClient.lock();
    while (!jackClient.complete)
        jackClient.wait();
    jackClient.unLock();

//    jackClient.stopClient();
    return 0;
}

