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
#include "Poller_devpoll.h"
#include "Poller_kqueue.h"
#include "Poller_poll.h"
#include "Poller_select.h"
#include "Poller_sigio.h"
#include "dprint.h"
#include "eclock.h"
#include "CHECK.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

// bogus Solaris fix
#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif

void die(int lineno, char method)
{
	perror("");
	printf("Error.  Test %c failed at line %d.\n", method, lineno);
	exit(1);
}
#define DIE(method) die(__LINE__, method)

/*----------------------------------------------------------------------
 Portable function to set a socket into nonblocking mode.
----------------------------------------------------------------------*/
static int setNonblocking(int fd)
{
	int flags;

	/* Caution: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
#if defined(O_NONBLOCK)
	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}

/**--------------------------------------------------------------------------
 Class to run iterations of a test for about N seconds when you
 don't know how long it takes to run one iteration.
 Use with a do - while loop, e.g.
	IterationController ic;
	ic.init(10);		// to specify a 10 second run
	do {
		... whatever you're trying to benchmark ...
	} while (ic.anotherIterationNeeded());
	printf("Each iteration took %g seconds.\n", ic.getResult());
--------------------------------------------------------------------------*/

class IterationController
{
public:
	/// Call this once at beginning of loop
	void init(int desiredRuntimeSeconds) {
		m_ticksPerBlock = 0;
		m_iterationsPerBlock = 1;
		m_iterationsLeft = m_iterationsPerBlock;
		m_eclock_hertz = eclock_hertz();
		m_desiredTicksPerBlock = (m_eclock_hertz * desiredRuntimeSeconds);
		m_start = eclock();
		//DPRINT(("IterationController::init: start %d\n", m_start));
	}

	/// Call this at end of each iteration to see if another is needed
	bool anotherIterationNeeded() {
		//DPRINT(("IterationController::anotherIterationNeeded: m_iL %d\n", m_iterationsLeft));
		if (--m_iterationsLeft > 0)
			return true;
		clock_t now = eclock();
		m_ticksPerBlock = now - m_start;
		//DPRINT(("IterationController::anotherIterationNeeded: now %d m_tpb %d m_dtpb %d m_ipb %d\n", 
			//now, m_ticksPerBlock, m_desiredTicksPerBlock, m_iterationsPerBlock));
		if (m_ticksPerBlock < m_desiredTicksPerBlock) {
			m_iterationsLeft = m_iterationsPerBlock;
			if (m_ticksPerBlock < (m_desiredTicksPerBlock / 16))
				m_iterationsLeft = (m_iterationsPerBlock * 15);		// ramp up faster if way off
			m_iterationsPerBlock += m_iterationsLeft;
			//DPRINT(("IterationController::anotherIterationNeeded: m_ipb now %d\n", 
				//m_iterationsPerBlock));
			return true;
		}
		return false;
	}

	/// Call this after loop exits to get time per iteration in seconds
	double getResult() {
		//DPRINT(("IterationController::getResult: m_tpb %d m_ipb %d result %g\n",
			//m_ticksPerBlock, m_iterationsPerBlock,
			//(double) m_ticksPerBlock / ((double) m_iterationsPerBlock * m_eclock_hertz)));
		return (double) m_ticksPerBlock / ((double) m_iterationsPerBlock * m_eclock_hertz);
	}

private:
	int m_ticksPerBlock;
	int m_iterationsPerBlock;
	int m_iterationsLeft;
	int m_eclock_hertz;
	int m_desiredTicksPerBlock;
	clock_t m_start;
};

// Clear the kernel profiling interface.

bool kernelProfileActive = false;

void kernelProfileStart()
{
	kernelProfileActive = false;

	// Writing to the profile file clears it.  Must be root.
	int fd = open("/proc/profile", O_RDWR);
	if (fd < 0)
		return;
	if (write(fd, "", 1) != 1) {
		DIE('0');
	}
	close(fd);
	kernelProfileActive = true;
}

// Collect data accumulated by the kernel profiling interface into the given file.
void kernelProfileStop(const char *filename)
{
	if (kernelProfileActive) {
		char buf[256];
		sprintf(buf, "cp /proc/profile %s", filename);
		if (system(buf))
			DIE('0');
		kernelProfileActive = false;
	}
}

template<class T>
class Poller_bench : Poller::Client
{
	T poller;

public:
	int notifyPollEvent(Poller::PollEvent *event)
	{
		char buf[1];
		int n;
		assert(event->revents & POLLIN);
		//DPRINT(("notifyPollEvent: reading one byte from fd %d\n", event->fd));
		n=read(event->fd, buf, sizeof(buf));
		// Poller_sigio does not promise that all notifications are true,
		// so it's ok to get falsely awoken.
		if (n != sizeof(buf) && errno != EAGAIN) {
			int err = errno;
			fprintf(stderr, "\nread(%d, %p, %d) returns %d?!, errno %d\n",
				event->fd, buf, sizeof(buf), n, err);
			DIE('?');
		}
		// for the benefit of Poller_sigio, clear event flag once we've handled it
		poller.clearReadiness(event->fd, POLLIN);

		return 0;
	}

	// Return how long it took to run each loop of the test, in microseconds
	int bench(int num_pipes, int fdpairs[][2], int num_active, int seconds, char method) {
		int i, k;
		int spacing;

		int err = poller.init();
		if (err)
			DIE(method);

		err = poller.setSignum(40);
		if (err)
			DIE(method);

		for (i = 0; i < num_pipes; i++) {
			// Add the read end to be monitored
			if (poller.add(fdpairs[i][0], this, POLLIN)) {
				DIE(method);
			}
		}

		spacing = num_pipes / num_active;
		int half = spacing/2;
		CHECK(true, (spacing >= 1));
		CHECK(true, (spacing * (num_active-1) + half < num_pipes));

		char oname[256];
		sprintf(oname, "bench%d%c.dat", num_pipes, method);
		IterationController ic;
		kernelProfileStart();
		ic.init(seconds);
		do {
			for (k=0; k<num_active; k++)
				if (write(fdpairs[half + k * spacing][1], "a", 1) != 1)
					DIE(method);
			if (poller.waitAndDispatchEvents(0))
				DIE(method);
		} while (ic.anotherIterationNeeded());
		kernelProfileStop(oname);

		return (int)(ic.getResult() * 1e6);
	}
};

int main(int argc, char **argv)
{
	int runtime, num_active;
	const char *methods;
	int (*fdpairs)[2];
	char method;

	setvbuf(stdout, (char *)NULL, _IONBF, 0); 
	if (argc < 5) {
		printf("Usage: %s runtime nactive [psdkr] npipes [npipes...]\n", argv[0]);
		return 1;
	}

	if ((runtime=atoi(argv[1])) < 1) {
		printf("invalid runtime %s\n", argv[1]);
		return 1;
	}
	if ((num_active=atoi(argv[2])) < 1) {
		printf("invalid num_active %s\n", argv[2]);
		return 1;
	}
	methods = argv[3];

	// Grab array of desired num_pipes values
	int i;
	int maxpipes = 0;
	int np[128];
	for (i=4; i<argc; i++) {
		int p = atoi(argv[i]);
		np[i-4] = p;
		if (p > maxpipes)
			maxpipes = p;
		if (p < 1) {
			printf("invalid npipes %s\n", argv[i]);
			return 1;
		}
		if (num_active > p) {
			printf("num_active %d > num_pipes %d\n", num_active, p);
			return 1;
		}
	}
	np[i-4] = 0;

	fdpairs = new int[maxpipes][2];
	if (!fdpairs) {
		DIE('0');
	}

	clock_t start = eclock();
	for (i = 0; i < maxpipes; i++) {
		// Create a pipe
		int fds[2];
		int err = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
		if (err) {
			DIE('0');
		}
		fdpairs[i][0] = fds[0];
		fdpairs[i][1] = fds[1];
		if (fdpairs[i][0] < 0) {
			printf("Bad fd %d!\n", fdpairs[i][0]);
			DIE('0');
		}
		if (fdpairs[i][1] < 0) {
			printf("Bad fd %d!\n", fdpairs[i][1]);
			DIE('0');
		}

		// This test is written so pipe reads and writes should never block.
		// We want to be able to check whether read or write would block, and abort.
		if (setNonblocking(fdpairs[i][0]) || setNonblocking(fdpairs[i][1]))
			DIE('0');
	}
	int delta = (int)(eclock() - start);
	printf("%d microseconds to open each of %d socketpairs\n", 
		(int)((delta * 1.0e6) / (eclock_hertz() * maxpipes)), maxpipes);

	printf("%10s", "pipes");
	for (i=0; np[i]; i++)
		printf("%7d ", np[i]);
	printf("\n");

	while ((method = *methods++) != 0) {
		switch (method) {
		case 'p': 
			printf("%10s", "poll");
			for (i=0; np[i]; i++) {
				Poller_bench<Poller_poll> bench_poll;
				printf("%7d ", bench_poll.bench(np[i], fdpairs, num_active, runtime, method));
			}
			printf("\n");
			break;

		case 's':
			printf("%10s", "select");
			for (i=0; np[i]; i++) {
				if (np[i] * 2 < FD_SETSIZE) {
					Poller_bench<Poller_select> bench_select;
					printf("%7d ", bench_select.bench(np[i], fdpairs, num_active, runtime, method));
				} else
					printf("%7s ", "-");
			}
			printf("\n");
			break;

#if HAVE_DEVPOLL
		case 'd':
			printf("%10s", "/dev/poll");
			for (i=0; np[i]; i++) {
				Poller_bench<Poller_devpoll> bench_devpoll;
				printf("%7d ", bench_devpoll.bench(np[i], fdpairs, num_active, runtime, method));
			}
			printf("\n");
			break;
#endif

#if HAVE_KQUEUE
		case 'k':
			printf("%10s", "kqueue");
			for (i=0; np[i]; i++) {
				Poller_bench<Poller_kqueue> bench_kqueue;
				printf("%7d ", bench_kqueue.bench(np[i], fdpairs, num_active, runtime, method));
				break;
			}
			printf("\n");
			break;
#endif

#if HAVE_F_SETSIG
		case 'r':
			printf("%10s", "rtsig");
			for (i=0; np[i]; i++) {
				Poller_bench<Poller_sigio> bench_sigio;
				printf("%7d ", bench_sigio.bench(np[i], fdpairs, num_active, runtime, method));
				break;
			}
			printf("\n");
			break;
#endif
		default:
			printf("Method %c unsupported on this platform.\n", method);
		}
	}

	start = eclock();
	for (i=0; i<maxpipes; i++) {
		if (close(fdpairs[i][0])) DIE('0');
		if (close(fdpairs[i][1])) DIE('0');
	}
	delta = (int)(eclock() - start);
	printf("%d microseconds to close each of %d socketpairs\n",
		(int)((delta * 1.0e6) / (eclock_hertz() * maxpipes)), maxpipes);

	delete [] fdpairs;

	exit(0);
}
