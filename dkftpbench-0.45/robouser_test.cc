/********************* Unit Self-Test ************************************/

#include "CHECK.h"
#include "dprint.h"
#include "robouser.h"
#include "Poller_poll.h"
#include "Platoon.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#define time_before(a, b) (((long)((a)-(b))) < 0)
int main(const int argc, const char *argv[])
{
#if 1
	const char *host = "ftp.uu.net";
	const char *user = "anonymous";
	const char *pass = "robouser@";
	const char *fname= "/usenet/.message";
	int min_bytes_per_sec = 50;
#else
	const char *host = "dual";
	//const char *host = "127.0.0.1";
	const char *user = "ftp";
	const char *pass = "me@";
	const char *fname= "pub/x10k.dat";
	int min_bytes_per_sec = 500;
#endif

	if (argc > 2) {
		user = argv[1];
		pass = argv[2];
	}
	if (argc > 4) {
		host = argv[3];
		fname = argv[4];
	}

	Sked sked;

	DPRINT_ENABLE(1);
	setlinebuf(stdout);

	if (sked.init()) {
		printf("Can't init scheduler.\n");
		exit(1);
	}

	printf("Attempting to fetch %s from %s as user %s...\n", fname, host, user);

	// kludge to avoid overstressing the throttler.  FIXME
	int max_bytes_per_sec = eclock_hertz();

	Poller_poll poller;

	int err = poller.init();
	CHECK(err, 0);


	Platoon thePlatoon;
	thePlatoon.init(&poller, &sked,fname,max_bytes_per_sec,
			min_bytes_per_sec,1500,host,21,user,pass, NULL, 0);
	thePlatoon.setVerbosity(1);
	thePlatoon.set_nuserTarget(1);

	time_t end = time(0) + 30;

	/* We succeed if we can fetch 1000 bytes in 30 seconds */
	while (true) {
		int nconnecting, nalive, ndead;

		if (!time_before(time(0), end)) {
			printf("Test failed, took too long.\n");
			exit(1);
		}

		/* Let the scheduler run the robots that need it */
		sked.runAll(eclock());

		/* See how the robots are doing */
		long bytesFetched;
		bytesFetched = thePlatoon.getStatus(&nconnecting, &nalive, &ndead);
		if (ndead) {
			printf("Test failed, user died\n");
			exit(1);
		}
		if (bytesFetched > 1000)
			break;		/* success */

		/* Call poll() to find out what handles are ready for read or write */
		err = poller.waitForEvents(1000);
		if (err == EWOULDBLOCK)
			continue;
		if (err) {
			perror("poll");
			exit(1);
		}

		/* At least one handle is ready.  Deal with it. */
		for (;;) {
			Poller::PollEvent event;
			int err;
			err = poller.getNextEvent(&event);
			if (err == EWOULDBLOCK)
				break;
			CHECK(0, err);

			err = event.client->notifyPollEvent(&event);
			CHECK(0, err);
		}
	}
	printf("robouser_test: no tests failed.\n");
	exit(0);
}
