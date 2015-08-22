#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>		// for MAXINT
#include "Sked.h"
#include "dprint.h"

clock_t g_prev_when = 0;
int g_errors = 0;
int g_events = 0;
class SkedTestClient : public SkedClient {
public:
	int m_id;
	clock_t m_when;
	SkedTestClient(int id, clock_t when) : m_id(id), m_when(when) {}
	void skedCallback(clock_t now);
};

void SkedTestClient::skedCallback(clock_t now)
{
	//DPRINT(("SkedTestClient::skedCallback(%ld) when %ld id %d\n", now, m_when, m_id));
	if (eclock_after(g_prev_when, m_when)) {
		g_errors++;
		printf("SkedTestClient::skedCallback(): Error, event %d out of order.\n", m_id);
	}
	g_events++;
	g_prev_when = m_when;
	(void) now;
}

/* Return time in seconds it took to push and pop n events in a size n heap */
float pushpopn(int n)
{
	Sked sk;
	SkedTestClient **sc;
	int err = sk.init();
	if (err) {
		printf("pushpoptest: Sked::init() failed.\n");
		exit(1);
	}
	sc = (SkedTestClient **)malloc(n * sizeof(SkedTestClient *));
	if (!sc) {
		g_errors++;
		return ENOMEM;
	}

	/* preload */
	for (int i=n-1; i>=0; i--) {
		sc[i] = new SkedTestClient(i,0);
		Sked::SkedRecord sr(sc[i], random());
		err = sk.push(&sr);
		if (err)
			g_errors++;
	}

	/* Time n inserts in a size n heap */
	clock_t start = eclock();
	for (int j=0; j<n; j++) {
		const Sked::SkedRecord *p = sk.top();
		SkedTestClient *scp;
		if (!p) {
			g_errors++;
			return -1;
		}
		if (sk.empty()) {
			g_errors++;
			return -1;
		}
		/* Recycle the client that is going away */
		scp = (SkedTestClient *)(p->skc);
		assert(scp->skedIndex == 1);
		sk.pop();
		Sked::SkedRecord sr(scp, random());
		err = sk.push(&sr);
		if (err)
			g_errors++;
	}
	clock_t stop = eclock();

	for (int k=n-1; k>=0; k--) {
		if (sk.empty()) {
			g_errors++;
			return -1;
		}
		sk.pop();
	}
	if (!sk.empty())
		g_errors++;

	float fduration = (stop - start) / (float) eclock_hertz();

	printf("pushpopn(%d): %f seconds\n", n, fduration);

	free(sc);
	return fduration;
}

#define N_TEST_EVENTS 200

int main(int argc, char **argv)
{
	Sked sk;
	int err = sk.init();
	if (err) {
		printf("main: Sked::init() failed.\n");
		exit(1);
	}
	(void) argc; (void) argv;

	g_prev_when = eclock();

	/* Schedule N events at random times */
	SkedTestClient *sktc[N_TEST_EVENTS];
	for (int i=0; i<N_TEST_EVENTS; i++) {
		clock_t now = eclock() + 1 + (random() % N_TEST_EVENTS);
		sktc[i] = new SkedTestClient(i, now);
		if (!sktc[i]) {
			printf("Test failed: can't allocate SkedTestClient\n");
			exit(1);
		}
		//DPRINT(("main: sktc[%d] %p\n", i, sktc[i]));
		sk.addClient(sktc[i], now);
	}
	/* Delete one tenth of the events */
	int n_deleted = N_TEST_EVENTS/10;
	for (int j=0; j < n_deleted; j++) {
		err = sk.delClient(sktc[j + N_TEST_EVENTS/2]);
		if (err) g_errors++;
	}

	/* Run them; callback checks if they occur when they should.  Kind of. */
	while (!sk.empty()) {
		if (sk.runAll(eclock())) {
			printf("main: Error: runAll fails\n");
			g_errors++;
			break;
		}
	}
	if (g_events != N_TEST_EVENTS-n_deleted) {
		printf("main: Error:  wanted %d events, got %d actual events.\n", 
				N_TEST_EVENTS-n_deleted, g_events);
		g_errors++;
	}

	if (g_errors > 0) {
		printf("Test failed, %d error(s).\n", g_errors);
		exit(1);
	}
	printf("Sked_test: No tests failed.\n");
	return 0;
}

