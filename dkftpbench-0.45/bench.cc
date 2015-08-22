/*--------------------------------------------------------------------------
 Copyright 1999,2000, Dan Kegel http://www.kegel.com/
 See the file COPYING

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
#include "CHECK.h"
#include "getifaddrs.h"
#include "robouser.h"
#include "Platoon.h"
#include "dprint.h"
#include "Poller_poll.h"
#include "Poller_devpoll.h"
#include "Poller_select.h"
#include "Poller_kqueue.h"
#include "Poller_sigio.h"
#include "Poller_sigfd.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>

static void usage()
{
	printf("\
Usage: bench [-options]\n\
Options:\n\
 -hHOSTNAME host name of ftp server\n\
 -P# port number of ftp server\n\
 -n# number of users\n\
 -c# target number of simultaneous connection attempts\n\
 -k# Start next connection when: %d=immediately, %d=after prev connect complete (default %d)\n\
 -t# length of run (in seconds)\n\
 -b# desired per-client bandwidth (in bytes per second)\n\
 -B# min acceptable per-client bandwidth (in bytes per second)\n\
 -uUSERNAME user name\n\
 -pPASSWORD user password\n\
 -fFILENAME file to fetch\n\
 -m# bytes per 'packet'\n\
 -v# set verbosity (0=none, 1=some, 2=lots)\n\
 -v increase verbosity (-v -v for very verbose)\n\
 -s# selector (p=poll, s=select, d=/dev/poll, k=kqueue, r=rtsig, f=sig-per-fd)\n\
 -a use all local IP interfaces\n\
", robouser_t::CONNECT, robouser_t::CONNECTING, robouser_t::CONNECTING);
	 exit(-1);
}

int nalive = 0;

void quit(int foo)
{
	(void) foo;

	fprintf(stderr, "\nAborted!  %d users still alive...\n", nalive);
	exit(1);
}

int main(int argc, char **argv)
{
	int arg_users = 0;
	int arg_lastConnectingState = robouser_t::CONNECTING;
	int arg_nconnectingTarget = 1;
	int arg_duration = 0;
	int arg_clientBandwidth = 28800/8;
	int arg_minClientBandwidth = 0;
	int arg_useAllLocalIfs = 0;
	const char *arg_hostname = "";
	const char *arg_username = "anonymous";
	const char *arg_password = "robouser@";
	const char *arg_filename = "usenet/rec.juggling/juggling.FAQ.Z";
	char arg_selector = 'p';
	short arg_portnum = 21;
	int arg_verbosity = 0;
	int arg_mtu = 1500;

	int nconnecting, ndead;
	int old_nalive, old_ndead;
	clock_t test_end;
	Sked sked;
	DPRINT_ENABLE(false);

	/* setlinebuf(stdout) */
	setvbuf(stdout, (char *)NULL, _IOLBF, 0);

	for (int i=0; i<argc; i++) {
		if (!strncmp(argv[i], "-h", 2)) {
			arg_hostname = &argv[i][2];
		} else if (!strncmp(argv[i], "-P", 2)) {
			arg_portnum = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-n", 2)) {
			arg_users = atoi(&argv[i][2]);
			if (arg_users > Platoon_MAXUSERS) {
				printf("Number of users must be <= %d\n", Platoon_MAXUSERS);
				exit(1);
			}
		} else if (!strncmp(argv[i], "-c", 2)) {
			arg_nconnectingTarget = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-k", 2)) {
			arg_lastConnectingState = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-t", 2)) {
			arg_duration = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-b", 2)) {
			arg_clientBandwidth = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-B", 2)) {
			arg_minClientBandwidth = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-u", 2)) {
			arg_username = &argv[i][2];
		} else if (!strncmp(argv[i], "-p", 2)) {
			arg_password = &argv[i][2];
		} else if (!strncmp(argv[i], "-f", 2)) {
			arg_filename = &argv[i][2];
		} else if (!strncmp(argv[i], "-m", 2)) {
			arg_mtu = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-s", 2)) {
			arg_selector = argv[i][2];
		} else if (!strncmp(argv[i], "-v", 2)) {
			if (strlen(argv[i]) > 2)
				arg_verbosity = atoi(&argv[i][2]);
			else
				arg_verbosity++;
		} else if (!strcmp(argv[i], "-a")) {
			arg_useAllLocalIfs = 1;
		}
	}

	if (arg_users == 0) {
		printf("Invalid number of users.\n");
		usage();
	} else if (arg_clientBandwidth == 0) {
		printf("Invalid bandwidth.\n");
		usage();
	} else if (strlen(arg_hostname) == 0) {
		printf("Invalid host name.\n");
		usage();
	}

	if (!arg_minClientBandwidth)
		arg_minClientBandwidth = (arg_clientBandwidth * 3)/4;

	struct sockaddr_in *local_addrs = 0;
	int n_local_addrs=0;

	if (arg_useAllLocalIfs) {
		struct ifaddrs *addrs;
		struct ifaddrs *p;
		getifaddrs(&addrs);
		int i=0;
		for (p = addrs; p; p = p->ifa_next) {
			if (!p->ifa_addr) continue;
			i++;
		}
		local_addrs = (struct sockaddr_in *)calloc(i, sizeof(struct sockaddr_in));
		i = 0;
		in_addr_t localhost = inet_addr("127.0.0.1");
		printf("Using local addresses:\n");
		for (p = addrs; p; p = p->ifa_next) {
			if (!p->ifa_addr) continue;
			if (((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr == localhost) continue;
			local_addrs[i].sin_family = AF_INET;
			local_addrs[i].sin_port = 0;	/* ephemeral */
			local_addrs[i].sin_addr = ((struct sockaddr_in *)p->ifa_addr)->sin_addr;
			puts(inet_ntoa(local_addrs[i].sin_addr));
			i++;
		}
		n_local_addrs = i;
	}

	/* Option values, in same order as usage message */
	printf("Option values:\n\
 -h%s host name of ftp server\n\
 -P%d port number of ftp server\n\
 -n%d number of users\n\
 -c%d target number of simultaneous connection attempts\n\
 -k%d Start next connection when: %d=immediately, %d=after prev connect complete\n\
 -t%d length of run (in seconds)\n\
 -b%d desired bandwidth (in bytes per second)\n\
 -B%d min acceptable per-client bandwidth (in bytes per second)\n\
 -u%s user name\n\
 -p%s user password\n\
 -f%s file to fetch\n\
 -m%d bytes per 'packet'\n\
 -v%d verbosity\n\
 -s%c selector (p=poll, s=select, d=/dev/poll, k=kqueue, r=rtsig, f=sig-per-fd)\n\
 -a%d use all local interfaces\n\
", 
	arg_hostname, arg_portnum, arg_users, arg_nconnectingTarget,
	arg_lastConnectingState, robouser_t::CONNECT, robouser_t::CONNECTING, 
	arg_duration, arg_clientBandwidth, arg_minClientBandwidth,
	arg_username, arg_password, arg_filename, arg_mtu, arg_verbosity, arg_selector,
	arg_useAllLocalIfs);

	if (arg_verbosity > 1) {
		DPRINT_ENABLE(true);
	}

	clock_t tix_per_second = eclock_hertz();
	assert(tix_per_second < 100000);	/* numerous overflows otherwise */

	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	if (sked.init()) {
		printf("Can't init scheduler.\n");
		exit(1);
	}
	Poller *poller;

	switch (arg_selector) {
	case 'p': 
		printf("Using poll()\n");
		poller = new Poller_poll();
		break;

	case 's':
		printf("Using select()\n");
		poller = new Poller_select();
		break;

#if HAVE_DEVPOLL
	case 'd':
		printf("Using Solaris /dev/poll\n");
		poller = new Poller_devpoll();
		break;
#endif

#if HAVE_KQUEUE
	case 'k':
		printf("Using BSD kqueue()\n");
		poller = new Poller_kqueue();
		break;
#endif

#if HAVE_F_SETSIG
	case 'r':
		printf("Using Linux rtsignals / F_SETSIG / O_ASYNC\n");
		poller = new Poller_sigio();
		break;
#endif

#if HAVE_F_SETAUXFL
	case 'f':
		printf("Using Linux rtsignals / O_SIGPERFD\n");
		poller = new Poller_sigfd();
		break;
#endif

	default:
		printf("Selector %c unsupported on this platform.\n", arg_selector);
		exit(1);
	}

	/* Initialize the poller */
	int err = poller->init();
	CHECK(err, 0);

#ifdef SIGRTMIN
	/* Tell it which signal number to use.  (Only need to do this for case 'r', really.) */
	err = poller->setSignum(SIGRTMIN);
	CHECK(err, 0);
#endif

	Platoon thePlatoon;
	thePlatoon.init(poller, &sked, arg_filename,
		arg_clientBandwidth, arg_minClientBandwidth, arg_mtu, 
		arg_hostname, arg_portnum, arg_username, arg_password,
		local_addrs, n_local_addrs);

	thePlatoon.set_nuserTarget(arg_users);
	thePlatoon.set_nconnectingTarget(arg_nconnectingTarget);
	thePlatoon.set_lastConnectingState((robouser_t::state_t) arg_lastConnectingState);
	thePlatoon.setVerbosity(arg_verbosity);

	nalive = 0;
	old_nalive = 0;
	ndead = 0;
	old_ndead = 0;
	test_end = eclock() + arg_duration * tix_per_second;

	signal(SIGINT, quit);

	while (1) {
		clock_t now = eclock();

		if (arg_duration && eclock_after(now, test_end))
			break;

		thePlatoon.getStatus(&nconnecting, &nalive, &ndead);
		if ((nalive != old_nalive) || (ndead != old_ndead)) {
			if (ndead != old_ndead) {
				if ((nalive == 0) && (nconnecting == 0)) {
					printf("All users dead.  Test failed.\n");
					exit(1);
				}
			}
			test_end = now + arg_duration * tix_per_second;

			printf("%d users alive, %d users dead; at least %d seconds to end of test\n", 
				nalive, ndead, (int) ((test_end-now)/tix_per_second));
			old_nalive = nalive;
			old_ndead = ndead;
		}

		/* Let the scheduler run the robots that need it */
		sked.runAll(now);

		/* Service any clients that might be ready. */
		for (;;) {
			Poller::PollEvent event;
			int err;
			err = poller->getNextEvent(&event);
			if (err == EWOULDBLOCK)
				break;
			CHECK(0, err);

			err = event.client->notifyPollEvent(&event);
			CHECK(0, err);
			thePlatoon.reap();
		}

		/* Call poller->waitForEvents() to find out what handles are ready for read or write.
		 * Don't sleep too long here, or you'll interfere with robouser's
		 * bandwidth throttling.
		 */
		now = eclock();
		clock_t tixUntilNextEvent = sked.nextTime(now + tix_per_second) - now;
		int msUntilNextEvent = (tixUntilNextEvent * 1000) / tix_per_second + 1;
		if (msUntilNextEvent < 0)
			msUntilNextEvent = 0;
		err = poller->waitForEvents(msUntilNextEvent);
		if (err && (err != EINTR) && (err != EWOULDBLOCK)) {
			errno = err;
			perror("poll");
			exit(1);
		}
	}
	poller->shutdown();
	printf("Test over.  %d users left standing.\n", nalive);
	return 0;
}
