MAINTAINCLEANFILES = Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)
#AM_CXXFLAGS = $(JACK_CFLAGS) $(GTKIOSTREAM_CFLAGS) $(EIGEN_CFLAGS) -fpermissive

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_iio.la

jack_iio_la_LDFLAGS = -module -avoid-version
#jack_iio_la_SOURCES = iio_driver.C
jack_iio_la_SOURCES = iio_driver_dummy.c # this is used to test the framework.
jack_iio_la_LIBADD = $(top_builddir)/jackd/libjackserver.la #$(GTKIOSTREAM_LIBS) $(EIGEN_LIBS)
#jack_portaudio_la_LIBADD = $(PA_LIBS)

noinst_HEADERS = iio_driver.h

noinst_PROGRAMS = JackIIODriverTest

JackIIODriverTest_SOURCES = JackIIODriverTest.C
JackIIODriverTest_CPPFLAGS = $(GTKIOSTREAM_CFLAGS) $(JACK_CFLAGS)
JackIIODriverTest_LDADD = $(top_builddir)/libjack/libjack.la $(GTKIOSTREAM_LIBS)

