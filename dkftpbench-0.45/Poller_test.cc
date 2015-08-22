/*--------------------------------------------------------------------------
 Copyright 1999,2000, Dan Kegel http://www.kegel.com/
 See the file COPYING
 (Also freely licensed to Disappearing, Inc. under a separate license
 which allows them to do absolutely anything they want with it, without
 regard to the GPL.)  

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
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
--------------------------------------------------------------------------*/
#include "Poller_poll.h"
#include "Poller_select.h"
#include "Poller_kqueue.h"
#include "Poller_devpoll.h"
#include "Poller_sigio.h"
#include "Poller_sigfd.h"
#include "dprint.h"
#include "CHECK.h"

#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/socket.h>

/*----------------------------------------------------------------------
 Portable function to set a socket into nonblocking mode.
----------------------------------------------------------------------*/
static int setNonblocking(int fd)
{
	int flags;

	/* Caution: O_NONBLOCK is defined but broken on SunOS 4.1.x & AIX 3.2.5. */
#if defined(O_NONBLOCK)
	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}

// handy little function used to count number of zeroes to right of given bit
// converts a bit mask like POLLIN into an index into m_eventCounts[].
static int log2(int val)
{
	int i;
	for (i=0; i<16; i++)
		if (val == (1 << i))
			return i;
	return -1;
}

static const char *eventbitname(int bitnum)
{
	switch (1<<bitnum) {
	case POLLIN: return "POLLIN";
	case POLLPRI: return "POLLPRI";
	case POLLOUT: return "POLLOUT";
	case POLLERR: return "POLLERR";
	case POLLHUP: return "POLLHUP";
	case POLLNVAL: return "POLLNVAL";
	default: return "unknown";
	}
	return "bug";
}

template<class T>
class Poller_test : Poller::Client 
{
private:
	/// The object being tested.
	T m_p;
	/// The two file descriptors we use to test Poller.
	int m_filedes[2];

	/// Number of bits in struct pollfd's events field we care about.
	static const int EVENTBITS = 8;

	/// Number of times each bit in struct pollfd's events field has been set.
	int m_eventCounts[EVENTBITS];


public:

	/// Just increments counts in m_eventCounts
	int notifyPollEvent(Poller::PollEvent *event)
	{
		int i;
		int fd = event->fd;
		short eventflags = event->revents;
		DPRINT(("notifyPollEvent: fd %d, eventflags %x\n", fd, eventflags));
		for (i=0; i<EVENTBITS; i++) {
			if (eventflags & (1 << i)) {
				m_eventCounts[i]++;
				DPRINT(("notifyPollEvent: m_eventCounts[%d %s] = %d\n", i, eventbitname(i), m_eventCounts[i]));
			}
		}
		(void) fd;
		return 0;
	}

	void clear() {
		DPRINT(("clear: clearing m_eventCounts\n"));
		for (int i=0; i<EVENTBITS; i++) 
			m_eventCounts[i] = 0;
	}

	/// The main function for this test.
	void unitTest()
	{
		testWakeUp();
		testCaching();
		testRejection("127.0.0.1", 2222);
		// To fully test nonblocking connections, must also test with a *remote*
		// computer that will reject the connection.
		//testRejection("192.168.123.254", 2222);
		testMondo();
	}

	/// My original big hairy test.
	void testMondo()
	{
		CHECK(0, m_p.init());
		CHECK(0, m_p.setSignum(SIGRTMIN));

		clear();

		// Create a pipe so we have file descriptors to watch with Poller.
		// We use a pipe rather than a file or socket because it's easy to 
		// generate readiness events by writing to one end of the pipe.
		CHECK(0, pipe(m_filedes));
		CHECK(0, setNonblocking(m_filedes[0]));
		CHECK(0, setNonblocking(m_filedes[1]));

		// Add a client to watch the standard input.  We'll delete it shortly.
		CHECK(0, m_p.add(0, this, POLLIN));
		// Watch our 2 test file descriptors, and count their readiness events.
		// The same client watches both file descriptors.
		CHECK(0, m_p.add(m_filedes[1], this, POLLOUT));
		CHECK(0, m_p.add(m_filedes[0], this, POLLIN|POLLPRI));
		// Delete the client registered for standard input; this used to trigger a bug in Poller.
		CHECK(0, m_p.del(0));

		/* For sigio and friends, assume that fd isn't really ready, and
		 * clear sigio's spurious readiness notification, as if we
		 * had tried to do I/O and gotten an EWOULDBLOCK.
		 * Err, except for POLLOUT on the writing end of the pipe, of course.
		 */
		m_p.clearReadiness(m_filedes[0], POLLIN|POLLPRI);
		m_p.clearReadiness(m_filedes[1], POLLIN);

		// Initially, the only thing that's ready is the writing end of the pipe
		// Verify we get notified that the expected number of fd's are readable/writable
		clear();
		CHECK(0, m_p.waitAndDispatchEvents(0));
		CHECK(0, m_eventCounts[log2(POLLIN)]);
		CHECK(1, m_eventCounts[log2(POLLOUT)]);

		CHECK(0, m_eventCounts[log2(POLLPRI)]);
		CHECK(0, m_eventCounts[log2(POLLERR)]);
		CHECK(0, m_eventCounts[log2(POLLHUP)]);
		CHECK(0, m_eventCounts[log2(POLLNVAL)]);

		DPRINT(("unitTest: same thing, but mask off all events first\n"));
		clear();
		CHECK(0, m_p.andMask(m_filedes[0],0));
		CHECK(0, m_p.andMask(m_filedes[1],0));
		CHECK(EWOULDBLOCK, m_p.waitAndDispatchEvents(0));
		CHECK(0, m_eventCounts[log2(POLLOUT)]);

		DPRINT(("unitTest: make sure andMask can't set bits\n"));
		CHECK(0, m_p.andMask(m_filedes[0],~0));
		CHECK(0, m_p.andMask(m_filedes[1],~0));
		CHECK(EWOULDBLOCK, m_p.waitAndDispatchEvents(0));
		CHECK(0, m_eventCounts[log2(POLLOUT)]);

		DPRINT(("unitTest: Flush cached events\n"));
		CHECK(EWOULDBLOCK, m_p.waitAndDispatchEvents(100));
		clear();
		CHECK(EWOULDBLOCK, m_p.waitAndDispatchEvents(100));
		CHECK(0, m_eventCounts[log2(POLLIN)]);
		CHECK(0, m_eventCounts[log2(POLLPRI)]);
		CHECK(0, m_eventCounts[log2(POLLOUT)]);
		CHECK(0, m_eventCounts[log2(POLLERR)]);
		CHECK(0, m_eventCounts[log2(POLLHUP)]);
		CHECK(0, m_eventCounts[log2(POLLNVAL)]);

		DPRINT(("unitTest: write to fd %d until full, make sure readiness goes off\n", m_filedes[1]));
		CHECK(0, m_p.setMask(m_filedes[0],POLLIN|POLLPRI));
		CHECK(0, m_p.setMask(m_filedes[1],POLLOUT));
		while (write(m_filedes[1], "hi", 2) == 2)
			;
		CHECK(EWOULDBLOCK, errno);
		/* We got an EWOULDBLOCK, which is exactly when we are
		 * required to clear our readiness notification 
		 */
		m_p.clearReadiness(m_filedes[1], POLLOUT);

		clear();
		CHECK(0, m_p.waitAndDispatchEvents(1000));		// FIXME: fails with Poller_sigio
		// After we write to the write end of the pipe, the write end is no longer ready,
		// and the read end is ready for reading.
		CHECK(1, m_eventCounts[log2(POLLIN)]);
		CHECK(0, m_eventCounts[log2(POLLPRI)]);
		CHECK(0, m_eventCounts[log2(POLLOUT)]);
		CHECK(0, m_eventCounts[log2(POLLERR)]);
		CHECK(0, m_eventCounts[log2(POLLHUP)]);
		CHECK(0, m_eventCounts[log2(POLLNVAL)]);

		// Check for bug reported by Jeon:
		// Deleting a client who has events waiting and then calling getNextEvent
		// returns the deleted file descriptor and a NULL client.
		// At this point, we know that m_filedes[0] is readable.
		CHECK(0, m_p.waitForEvents(0));
		CHECK(0, m_p.del(m_filedes[0]));
		Poller::PollEvent e;
		CHECK(EWOULDBLOCK, m_p.getNextEvent( &e ));
		m_p.shutdown();
	}


	/// Just test rejected connections.
	void testRejection(const char *theHost, int thePort)
	{
		CHECK(0, m_p.init());
		CHECK(0, m_p.setSignum(SIGRTMIN));

		DPRINT(("Connect to a port that will refuse connections, verify error reported.\n"));
		// Requires theHost = a nonlocal IP adr, thePort = port where that host will refuse a connection
		// i.e. this test assumes that if you do `telnet theHost thePort`, you get a quick rejection

		int sock = socket (AF_INET, SOCK_STREAM, 0);
		CHECKNE(sock, -1);
		CHECK(0, setNonblocking(sock));

		struct sockaddr_in sin;
		sin.sin_family=AF_INET;
		sin.sin_port=htons(thePort);
		sin.sin_addr.s_addr=inet_addr(theHost);

		DPRINT(("connecting to host %s port %d, expect rejection\n", theHost, thePort));
		CHECK(-1, connect (sock, reinterpret_cast<struct sockaddr *>(&sin), sizeof (sin)));
		CHECK(errno, EINPROGRESS);

		/* Don't add the socket to the Poller until after you've
		 * called connect().  Otherwise, it's ambiguous what writability 
		 * means; unconnected sockets are always 'writable', but once you
		 * call connect(), it won't become writable again until the connect
		 * completes or fails.
		 * Be careful to only ask for POLLOUT + POLLERR until connection
		 * completes; other events would just be confusing.
		 */
		m_p.add(sock, this, POLLOUT|POLLERR);

		clear();
		CHECK(0, m_p.waitAndDispatchEvents(1000));
		// we expect rejection, so the error count should be 1
		// however, select doesn't tell us about errors, 
		//CHECK(1, m_eventCounts[log2(POLLERR)]);
		// so we have to do I/O to tell whether it failed
		CHECK(true, m_eventCounts[log2(POLLOUT)]||m_eventCounts[log2(POLLERR)]);
		// nonblocking connect completion signalled by POLLOUT; see Stevens UNP vol 1 p. 410
		/* check to see if connect succeeded */
		int connecterr = -1;
		socklen_t len = sizeof(connecterr);
		if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&connecterr, &len) < 0) {
			int xerr = errno;
			EDPRINT(("can't getsockopt SO_ERROR fd %d fails, errno %d\n", sock, xerr));
			CHECK(0, 1);
		}
		DPRINT(("got connection errno %d\n", connecterr));
		if (connecterr == EINPROGRESS) {
			DPRINT(("Hmm, kernel says we're still connecting...\n"));
			/* after every EWOULDBLOCK/EINPROGRESS, must clear readiness */
			m_p.clearReadiness(sock, POLLOUT);
			/* This would happen if we had a spurious writability readiness
			 * event.  Normally we'd just continue in the event loop until
			 * next event.  I haven't coded that here out of laziness.
			 */
			CHECK(0, 1);
		} else if (connecterr) {
			DPRINT(("connection failed as expected\n"));
		} else {
			DPRINT(("connection succeeded.  Are you sure you edited the source and specified a remote computer that will reject the connection??\n"));
			CHECK(0, 1);
		}

		CHECK(0, m_p.del(sock));
		close(sock);
		m_p.shutdown();
	}

	/// Just test wakeUp.
	void testWakeUp()
	{
		CHECK(0, m_p.init());
		CHECK(0, m_p.setSignum(SIGRTMIN));

		DPRINT(("testWakeUp: Set up a wakeup pipe\n"));
		CHECK(0, m_p.initWakeUpPipe());

		/* Clear the cached readiness bits */
		m_p.waitAndDispatchEvents(0);

		DPRINT(("testWakeUp: Verify that waitForEvents() will block at moment\n"));
		CHECK(EWOULDBLOCK, m_p.waitAndDispatchEvents(0));

		DPRINT(("testWakeUp: Verify that wakeUp will keep waitForEvents() from blocking\n"));
		CHECK(0, m_p.wakeUp());
		int err = m_p.waitAndDispatchEvents(1000);
		if (err != 0) {
			fprintf(stderr, "Your kernel might not support SIGIO with pipes...\n"
			  "see http://www.cs.helsinki.fi/linux/linux-kernel/2002-13/0191.html\n");
		}
		CHECK(0, err);

		m_p.shutdown();
	}

	/// Just test state caching.
	void testCaching(void)
	{
		DPRINT(("testCaching\n"));
		CHECK(0, m_p.init());
		CHECK(0, m_p.setSignum(SIGRTMIN));

		// Create a pipe so we have file descriptors to watch with Poller.
		// We use a pipe rather than a file or socket because it's easy to 
		// generate readiness events by writing to one end of the pipe.
		CHECK(0, pipe(m_filedes));
		CHECK(0, setNonblocking(m_filedes[0]));
		CHECK(0, setNonblocking(m_filedes[1]));

		// Watch our 2 test file descriptors, and count their readiness events.
		// The same client watches both file descriptors.
		CHECK(0, m_p.add(m_filedes[1], this, POLLOUT));
		CHECK(0, m_p.add(m_filedes[0], this, POLLIN|POLLPRI));

		for (int i=0; i<3; i++) {
			DPRINT(("testCaching: loop iter %d\n", i));
			// Initially, the only thing that's ready is the writing end of the pipe
			// Verify we get notified that the expected number of fd's are readable/writable
			clear();
			CHECK(0, m_p.waitAndDispatchEvents(0));
			/* well, these should be 0 and 1, but Poller is allowed to give us spurious
			 * wakeups for anything we're registered for, and it does initially for Poller_sigio.
			 * Makes this hard to test, eh?
			 */
#if 0
			CHECK(0, m_eventCounts[log2(POLLIN)]);
			CHECK(1, m_eventCounts[log2(POLLOUT)]);
#endif

			CHECK(0, m_eventCounts[log2(POLLPRI)]);
			CHECK(0, m_eventCounts[log2(POLLERR)]);
			CHECK(0, m_eventCounts[log2(POLLHUP)]);
			CHECK(0, m_eventCounts[log2(POLLNVAL)]);
		}
		CHECK(0, m_p.del(m_filedes[0]));
		CHECK(0, m_p.del(m_filedes[1]));
		close(m_filedes[0]);
		close(m_filedes[1]);
		m_p.shutdown();
	}
};

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	// send all DPRINTs to logfile
	FILE *log = fopen("Poller_test.log", "w");
	CHECKNE(0,log);
	DPRINT_SETFP(log);
	// setlinebuf(log)
	setvbuf(log, (char *)NULL, _IOLBF, 0); 
	DPRINT_ENABLE(true);

	DPRINT(("poll test\n"));
	Poller_test<Poller_poll> ptp;
	ptp.unitTest();
	
	DPRINT(("select test\n"));
	Poller_test<Poller_select> pts;
	pts.unitTest();
	
#if HAVE_KQUEUE
	DPRINT(("kqueue test\n"));
	Poller_test<Poller_kqueue> ptk;
	ptk.unitTest();
#endif

#if HAVE_DEVPOLL
	DPRINT(("devpoll test\n"));
	Poller_test<Poller_devpoll> ptd;
	ptd.unitTest();
#endif

#if HAVE_F_SETSIG
	DPRINT(("sigio test\n"));
	Poller_test<Poller_sigio> ptr;
	ptr.unitTest();
#endif

#if HAVE_F_SETSIG && HAVE_F_SETAUXFL
	DPRINT(("sigfd test\n"));
	Poller_test<Poller_sigfd> ptf;
	ptf.unitTest();
#endif

	unlink("Poller_test.log");
	printf("Poller_test: test passed\n");
	exit(0);
}
