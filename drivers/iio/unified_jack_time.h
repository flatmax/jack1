/*

	IIO driver for Jack : unified API for struct timespec and jack_time_t.
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
Date: 2014.02.11

This header specifies types, inline functions and necessary definitions to allow the iio_driver_wait function to be written (mostly) once and applied to both time types.
It can be used in other jack drivers.
*/

// needed for clock_nanosleep
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif
#include <time.h>

#ifdef HAVE_CLOCK_GETTIME
typedef struct timespec timeType; ///< Use timespec as the timeType if available
#define NEXT_TIME_NAME next_wakeup ///< The variable name for the next time concept
#else
typedef jack_time_t timeType;  ///< Use jack_time_t as the timeType as fallback
#define NEXT_TIME_NAME next_time ///< The variable name for the next time concept
#endif

#define NEXT_TIME driver->NEXT_TIME_NAME ///< The macro NEXT_TIME is used to represent the next time wall which the driver must target.

#ifdef HAVE_CLOCK_GETTIME
static inline void getNow(timeType *now){clock_gettime(CLOCK_REALTIME, now);}

inline bool compareTimesLt(timeType ts1, timeType ts2) {
    return (ts1.tv_sec < ts2.tv_sec) || (ts1.tv_sec == ts2.tv_sec && ts1.tv_nsec < ts2.tv_nsec);
}

inline bool compareTimeZero(timeType ts) {
    return ts.tv_sec==0.;
}

static inline unsigned long long ts_to_nsec(timeType ts){
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

inline bool timeDiffUsComp(timeType ts1, timeType ts2, float maxD){
//    printf("timeDiffUsComp:: diff = %ju , maxDelay = %.4f\n",(ts_to_nsec(ts1) - ts_to_nsec(ts2))/1000LL, maxD);

    return (ts_to_nsec(ts1) - ts_to_nsec(ts2))/1000LL > maxD;
}

static inline struct timespec nsec_to_ts(unsigned long long nsecs){
    struct timespec ts;
    ts.tv_sec = nsecs / (1000000000LL);
    ts.tv_nsec = nsecs % (1000000000LL);
    return ts;
}

static inline struct timespec add_ts(struct timespec ts, unsigned int usecs){
    unsigned long long nsecs = ts_to_nsec(ts);
    nsecs += usecs * 1000LL;
    return nsec_to_ts(nsecs);
}

inline timeType addTimes(timeType time1, uint time2){
    return add_ts(time1, time2);
}

inline uintmax_t timeDiffUs(timeType t1, timeType t2){
    return (ts_to_nsec(t1) - ts_to_nsec(t2))/1000LL;
}

inline void zeroTime(timeType *ts1){
    ts1->tv_sec = 0;
}

inline int nanoSleep(timeType nextTime, float *delayed_usecs){
        //cout<<"nanoSleep timespec"<<endl;
		if(clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &nextTime, NULL)) {
			jack_error("error while sleeping");
			return -1;
		} else {
            timeType now; getNow(&now);
			// guaranteed to sleep long enough for this to be correct
			*delayed_usecs = (ts_to_nsec(now) - ts_to_nsec(nextTime));
			*delayed_usecs /= 1000.0;
		}
		return 0;
}

#else
static inline void getNow(timeType *now){*now = driver->engine->get_microseconds();}

bool compareTimesLt(timeType t1, timeType t2) {
    return t1<t2;
}

inline bool compareTimeZero(timeType ts) {
    return ts==0.;
}

inline bool timeDiffUsComp(timeType ts1, timeType ts2, float maxD){
    return (ts1 - ts2) > maxD;
}

inline timeType addTimes(timeType time1, timeType time2){
    return time1+time2;
}

inline uintmax_t timeDiffUs(timeType t1, timeType t2){
    return t1-t2;
}

inline void zeroTime(timeType *t1){
    *t1=0;
}

inline void nanoSleep(timeType nextTime, float *delayed_usecs, int *status){
        //cout<<"nanoSleep jack_time_t"<<endl;
        timeType now; getNow(&now);
		jack_time_t wait = nextTime - now;
		struct timespec ts = { .tv_sec = wait / 1000000, .tv_nsec = (wait % 1000000) * 1000 };
		nanosleep(&ts,NULL);
		return 0;
}

#endif

