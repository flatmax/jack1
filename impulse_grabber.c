#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <getopt.h>

#include <jack/jack.h>

jack_port_t *input_port;
jack_port_t *output_port;

unsigned int impulse_sent = 0;
float *response;
unsigned long response_duration;
unsigned long response_pos;
int grab_finished = 0;

int
process (nframes_t nframes, void *arg)

{
	sample_t *out = (sample_t *) jack_port_get_buffer (output_port, nframes);
	sample_t *in = (sample_t *) jack_port_get_buffer (input_port, nframes);
	unsigned int i;

	if (grab_finished) {
		return 0;
	} else if (impulse_sent) {
		for(i=0; i<nframes && response_pos < response_duration; i++) {
			response[response_pos++] = in[i];
		}
		if (response_pos >=  response_duration) {
			grab_finished = 1;
		}	
		for (i=0; i<nframes; i++) {
			out[i] = 0.0f;;
		}
	} else {
		out[0] = 1.0f;
		for (i=1; i<nframes; i++) {
			out[i] = 0.0f;
		}
		impulse_sent = 1;
	}

	return 0;      
}

void
jack_shutdown (void *arg)
{
	exit (1);
}

int
main (int argc, char *argv[])

{
	jack_client_t *client;
	float fs;		// The sample rate
	float peak;
	unsigned long peak_sample;
	unsigned int i;
	float duration = 0.0f;
	unsigned int c_format = 0;
        int longopt_index = 0;
	int c;
        extern int optind, opterr;
        int show_usage = 0;
        char *optstring = "d:f";
        struct option long_options[] = {
                { "help", 1, 0, 'h' },
                { "duration", 1, 0, 'd' },
                { "format", 1, 0, 'f' },
                { 0, 0, 0, 0 }
        };

        while ((c = getopt_long (argc, argv, optstring, long_options, &longopt_index)) != -1) {
                switch (c) {
		case 1:
			// end of opts, but don't care
			break;
                case 'h':
                        show_usage++;
                        break;
		case 'd':
			duration = (float)atof(optarg);
			break;
		case 'f':
			if (*optarg == 'c' || *optarg == 'C') {
				c_format = 1;
			}
			break;
		default:
			show_usage++;
			break;
		}
	}
	if (show_usage || duration <= 0.0f) {
		fprintf(stderr, "usage: jack_impulse_grab -d duration [-f (C|gnuplot)]\n");
		exit(1);
	}

	/* try to become a client of the JACK server */

	if ((client = jack_client_new("impulse_grabber")) == 0) {
		fprintf (stderr, "jack server not running?\n");
		return 1;
	}

	/* tell the JACK server to call `process()' whenever
	   there is work to be done.
	*/

	jack_set_process_callback (client, process, 0);

	/* tell the JACK server to call `jack_shutdown()' if
	   it ever shuts down, either entirely, or if it
	   just decides to stop calling us.
	*/

	jack_on_shutdown (client, jack_shutdown, 0);

	/* display the current sample rate. once the client is activated 
	   (see below), you should rely on your own sample rate
	   callback (see above) for this value.
	*/

	fs = jack_get_sample_rate(client);
	response_duration = (int)(fs * duration);
	response = malloc(response_duration * sizeof(float));
	fprintf(stderr, "Grabbing %f seconds (%lu samples) of impulse response\n", duration, response_duration);

	/* create two ports */

	input_port = jack_port_register (client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	output_port = jack_port_register (client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	/* tell the JACK server that we are ready to roll */

	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client");
		return 1;
	}

	/* connect the ports. Note: you can't do this before
	   the client is activated (this may change in the future).
	*/

	if (jack_connect (client, "alsa_pcm:in_1", jack_port_name (input_port))) {
		fprintf (stderr, "cannot connect input ports\n");
	}
	
	if (jack_connect (client, jack_port_name (output_port), "alsa_pcm:out_1")) {
		fprintf (stderr, "cannot connect output ports\n");
	}

	/* Wait for grab to finish */
	while (!grab_finished) {
		sleep (1);
	}
	jack_client_close (client);

	peak = response[0];
	peak_sample = 0;
	if (c_format) {
		printf("impulse[%lu] = {", response_duration);
		for (i=0; i<response_duration; i++) {
			if (i % 4 != 0) {
				printf(" ");
			} else {
				printf("\n\t");
			}
			printf("\"%+1.10f\"", response[i]);
			if (i < response_duration - 1) {
				printf(",");
			}
			if (fabs(response[i]) > peak) {
				peak = fabs(response[i]);
				peak_sample = i;
			}
		}
		printf("\n};\n");
	} else {
		for (i=0; i<response_duration; i++) {
			printf("%1.12f\n", response[i]);
			if (fabs(response[i]) > peak) {
				peak = fabs(response[i]);
				peak_sample = i;
			}
		}
	}
	fprintf(stderr, "Peak value was %f at sample %lu\n", peak, peak_sample);

	exit (0);
}
