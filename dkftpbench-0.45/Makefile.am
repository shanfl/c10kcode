# To turn Makefile.am into Makefile from scratch, run 
# 'aclocal; autoconf; automake --add-missing; ./configure'
# See 'info automake' or http://sourceware.cygnus.com/automake/
# and 'info autoconf'
#
# To run the unit tests, do 'make check'.

#----------------------------------------------------------------------
# List everything we build; break list into three kinds of executables

# Unattended unit tests
TESTS = \
	ftp_client_proto_test \
	nbbio_test \
	Poller_test \
	Sked_test

# Following tests are not yet unattended
HAND_TESTS = \
	robouser_test \
	ftp_client_pipe_test

# Real programs 
bin_PROGRAMS = dkftpbench

noinst_PROGRAMS = $(TESTS) $(HAND_TESTS) dklimits Poller_bench profile

lib_LIBRARIES = libPoller.a
pkginclude_HEADERS = \
	Poller.h \
	Poller_devpoll.h \
	Poller_poll.h \
	Poller_select.h \
	Poller_sigio.h \
	Poller_sigfd.h \
	Poller_kqueue.h

# No info yet
info_TEXINFOS = 

EXTRA_DIST = \
	inet_aton.c \
	bakeoff.html \
	Poller_bench.sh \
	Poller_bench.html \
	callbackDemo.html \
	classes.html \
	index.html \
	nonblocking.html \
	results.html \
	theory.html \
	one-include.patch \
	omni_scripts.tar.gz \
	config/ac_cxx_namespaces.m4 \
	config/CORBA_CHECK_SYMBOL.m4 \
	config/RSSH_CHECK_MODCBROKER.m4 \
	config/RSSH_CHECK_OMNIORB.m4 \
	config/RSSH_CHECK_ORBACUS.m4 \
	config/RSSH_CHECK_ORBEXPRESS.m4 \
	config/RSSH_CHECK_ORBIT.m4 \
	config/RSSH_CHECK_ORBIT.m4.orig \
	config/RSSH_CHECK_ORBIXE.m4 \
	config/RSSH_CHECK_ORB.m4 \
	config/RSSH_CHECK_OTS.m4 \
	config/RSSH_CHECK_PTHREADS.m4 \
	config/RSSH_CHECK_SUNPRO_CC.m4 \
	config/RSSH_CHECK_SUNPRO_C.m4 \
	config/RSSH_CHECK_TAO.m4 \
	config/RSSH_ENABLE_PTHREADS.m4 \
	config/RSSH_PIC_FLAG.m4 \
	config/RSSH_PROG_CXXAR.m4 \
	config/RSSH_PROG_CXXLD.m4 \
	config/RSSH_RTTI.m4 \
	config/RSSH_TRY_RTTI_NOCACHE.m4 \
	config/YAD_CHECK_INCLUDE_LIB.m4 \
	config/config.guess \
	config/config.sub \
	config/install-sh \
	config/missing \
	config/mkinstalldirs \
	CorbaPlatoon.idl \
	CorbaPlatoon_impl.cc \
	CorbaPlatoon_test.cc \
	corbaftpbench.cc

#----------------------------------------------------------------------
# For each thing we build, list its sources, and anything special about
# how to build it

libPoller_a_SOURCES = \
	Poller.h \
	Poller_sigfd.cc \
	Poller_sigio.cc \
	Poller_poll.cc \
	Poller_select.cc \
	Poller_devpoll.cc \
	Poller_kqueue.cc \
	Poller.cc

dkftpbench_SOURCES = \
	CHECK.h \
	Poller.h \
	Sked.h \
	dprint.h \
	eclock.h \
	ftp_client_pipe.h \
	ftp_client_proto.h \
	getifaddrs.h \
	getifaddrs.c \
	nbbio.h \
	robouser.h \
	Platoon.h \
	bench.cc \
	dprint.cc \
	eclock.cc \
	ftp_client_pipe.cc \
	ftp_client_proto.cc \
	nbbio.cc \
	robouser.cc \
	Platoon.cc \
	CHECK.cc \
	Poller_sigfd.cc \
	Poller_sigio.cc \
	Poller_poll.cc \
	Poller_select.cc \
	Poller_devpoll.cc \
	Poller_kqueue.cc \
	Poller.cc \
	Sked.cc

dklimits_SOURCES = \
	dklimits.c

ftp_client_proto_test_SOURCES = \
	dprint.cc \
	ftp_client_proto.cc \
	ftp_client_proto_test.cc

ftp_client_pipe_test_SOURCES = \
	dprint.cc \
	eclock.cc \
	ftp_client_pipe.cc \
	ftp_client_pipe_test.cc \
	ftp_client_proto.cc \
	Poller_poll.cc \
	Poller.cc \
	nbbio.cc \
	Sked.cc

nbbio_test_SOURCES = \
	dprint.cc \
	nbbio.cc \
	nbbio_test.cc

robouser_test_SOURCES = \
	dprint.cc \
	eclock.cc \
	ftp_client_pipe.cc \
	ftp_client_proto.cc \
	nbbio.cc \
	robouser.cc \
	Platoon.cc \
	robouser_test.cc \
	CHECK.cc \
	Poller_poll.cc \
	Poller.cc \
	Sked.cc

Poller_bench_SOURCES = \
	dprint.cc \
	eclock.cc \
	CHECK.cc \
	Poller.h \
	Poller.cc \
	Poller_devpoll.cc \
	Poller_devpoll.h \
	Poller_poll.h \
	Poller_poll.cc \
	Poller_bench.cc \
	Poller_select.h \
	Poller_select.cc \
	Poller_sigio.h \
	Poller_sigio.cc \
	Poller_sigfd.h \
	Poller_sigfd.cc \
	Poller_kqueue.h \
	Poller_kqueue.cc
	
Poller_test_SOURCES = \
	dprint.cc \
	CHECK.cc \
	eclock.cc \
	Poller.h \
	Poller.cc \
	Poller_sigfd.cc \
	Poller_sigfd.h \
	Poller_sigio.cc \
	Poller_sigio.h \
	Poller_devpoll.cc \
	Poller_devpoll.h \
	Poller_poll.h \
	Poller_poll.cc \
	Poller_test.cc \
	Poller_select.h \
	Poller_select.cc \
	Poller_kqueue.h \
	Poller_kqueue.cc

Sked_test_SOURCES = \
	dprint.cc \
	eclock.cc \
	Sked.cc \
	Sked_test.cc

#------- start of corba.  Not fully autoconfisticated yet ---------

CorbaPlatoon_srv.cc: CorbaPlatoon.idl
	@IDLCXX@ @IDLFLAGS@ CorbaPlatoon.idl
#   Copy to get uniform filenames for generated files
	cp CorbaPlatoon@IDL_SRV_CPP_SUFFIX@ CorbaPlatoon_srv.cc
	cp CorbaPlatoon@IDL_SRV_H_SUFFIX@   CorbaPlatoon_srv.hh
	cp CorbaPlatoon@IDL_CLN_CPP_SUFFIX@ CorbaPlatoon_cln.cc
	cp CorbaPlatoon@IDL_CLN_H_SUFFIX@   CorbaPlatoon_cln.hh

CorbaPlatoon_srv.o: CorbaPlatoon_srv.cc
	$(CXX) -c $(CXXFLAGS) @IDLCXXFLAGS@ @CPPFLAGS@ $(LIBS) -o CorbaPlatoon_srv.o CorbaPlatoon_srv.cc

# fixme: should not use CHECK
CorbaPlatoon_impl: CorbaPlatoon_impl.cc CorbaPlatoon_srv.o Platoon.o eclock.o robouser.o Sked.o ftp_client_pipe.o ftp_client_proto.o nbbio.o CHECK.o libPoller.a getifaddrs.o dprint.o
	$(CXX) -c $(CXXFLAGS) @IDLCXXFLAGS@ @CPPFLAGS@ -o CorbaPlatoon_impl.o CorbaPlatoon_impl.cc
	$(CXX) $(CXXFLAGS) $(LIBS) @ORB_LIBS@ -o CorbaPlatoon_impl CorbaPlatoon_impl.o CorbaPlatoon_srv.o Platoon.o eclock.o robouser.o Sked.o ftp_client_pipe.o ftp_client_proto.o nbbio.o CHECK.o libPoller.a getifaddrs.o dprint.o

CorbaPlatoon_test: CorbaPlatoon_test.cc CorbaPlatoon_srv.o
	$(CXX) -c $(CXXFLAGS) @IDLCXXFLAGS@ @CPPFLAGS@ -o CorbaPlatoon_test.o CorbaPlatoon_test.cc
	$(CXX) $(CXXFLAGS) $(LIBS) @ORB_LIBS@ -o CorbaPlatoon_test CorbaPlatoon_test.o CorbaPlatoon_srv.o

corbaftpbench: corbaftpbench.cc CorbaPlatoon_srv.o eclock.o
	$(CXX) -c $(CXXFLAGS) $(DEFS) @IDLCXXFLAGS@ @CPPFLAGS@ -o corbaftpbench.o corbaftpbench.cc
	$(CXX) $(CXXFLAGS) $(LIBS) @ORB_LIBS@ -o corbaftpbench corbaftpbench.o CorbaPlatoon_srv.o eclock.o

clean:
	/bin/rm -f *.o CorbaPlatoon.hh CorbaPlatoon{_srv,Dyn_srv}.cc

#---- end of corba --------------------------------------------------------

#--------------------------------------------------------------------------
# Program to do fine-grained profiling of Linux kernel.  From the net...

profile_SOURCES = profile.c

#--------------------------------------------------------------------------
# Extract documentation from sources.

docs:
	-rm -rf doc
	-mkdir doc
	doc++ -d doc *.h
	perl -p -i.bak -e "s/--------*//g" doc/*.html  

#--------------------------------------------------------------------------
# Generate data files.

data:
	dd if=/dev/zero of=x10k.dat bs=1k count=10
	dd if=/dev/zero of=x100k.dat bs=1k count=100
	dd if=/dev/zero of=x1000k.dat bs=1k count=1000




