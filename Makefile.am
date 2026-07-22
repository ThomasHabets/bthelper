AUTOMAKE_OPTIONS=foreign
DISTCLEANFILES=*~
AM_CPPFLAGS=-I$(builddir)
ACLOCAL_AMFLAGS=-I m4

bin_PROGRAMS=bt-connecter bt-listener

bt_connecter_SOURCES=\
src/bt-connecter.cc \
src/main.cc \
src/buffer.cc \
src/shuffle.cc \
src/common.cc

bt_listener_SOURCES=\
src/bt-listener.cc \
src/main.cc \
src/shuffle.cc \
src/buffer.cc \
src/common.cc
