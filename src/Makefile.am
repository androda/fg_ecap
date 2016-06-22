# what flags you want to pass to the C compiler & linker
AM_CXXFLAGS = --pedantic -Wall -O2 -std=c++11 -fPIC
AM_LDFLAGS =
#LDADD = /usr/local/lib/libecap.a

# this lists the binaries to produce, the (non-PHONY, binary) targets in
# the previous manual Makefile
#noinst_PROGRAMS = fg_reqmod.so fg_respmod.so
#fg_reqmod_so_SOURCES = fg_reqmod.cc
#fg_respmod_so_SOURCES = fg_respmod.cc
lib_LTLIBRARIES = libreqmod.la librespmod.la
#libreqmod_sodir = src
libreqmod_la_SOURCES = fg_reqmod.cc
libreqmod_la_LDFLAGS = -shared -fPIC -version-info 0:1:0

#librespmod_sodir = src
librespmod_la_SOURCES = fg_respmod.cc
librespmod_la_LDFLAGS = -shared -fPIC -version-info 0:1:0
