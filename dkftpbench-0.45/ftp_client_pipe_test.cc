/* Unit self-test code */

#include "dprint.h"
#include "ftp_client_pipe.h"
#include "Poller_poll.h"
#include <arpa/inet.h> 
#include <assert.h> 
#include <errno.h> 
#include <fcntl.h> 
#include <netdb.h> 
#include <poll.h> 
#include <stdio.h> 
#include <string.h> 
#include <stdlib.h> 
#include <sys/socket.h> 	/* for AF_INET */
#include <unistd.h> 

/* Whether the latest command is finished, and how */
bool g_done;
int g_xerr;
int g_status;
int g_size;

void check(int d, int e, int line) { if (d != e) {
		printf("check: test failed: %d != %d at line %d\n", d, e, line);
		exit(1);
	}
}

void checkne(int d, int e, int line) { if (d == e) {
		printf("checkne: test failed: %d == %d at line %d\n", d, e, line);
		exit(1);
	}
}

void strcheck(const char * d, const char * e, int line)
{
	if (!d || !e) {
		printf("strcheck: test failed: One of the character string is null: %s %s\n", d, e);
		exit(1);
	}
	if (strcmp(d, e)) {
		printf("strcheck: test failed: %s != %s at line %d\n", d, e, line);
		exit(1);
	}
}

void statuscheck(const char * d, int e, int line)
{
	if (!d) {
		printf("strcheck: test failed: Character string is null\n");
		exit(1);
	}
	if (atoi(d) != e) {
		printf("statuscheck: test failed: atoi(%s) != %d at line %d\n", d, e, line);
		exit(1);
	}
}

#define CHECK(d, e) check(d, e, __LINE__)
#define CHECKNE(d, e) checkne(d, e, __LINE__)
#define STRCHECK(d, e) strcheck(d, e, __LINE__)
#define STATUSCHECK(d, e) statuscheck(d, e, __LINE__)

/* return whether the given status code indicates a given FTP command status */
#define STATUS_OK(s)  (((s) >= 200) && ((s) <= 299))

/* We have to implement the handle_data_io method from 
 * ftp_client_pipe_datainterface_t
 */
class ftp_client_pipe_test_t: public ftp_client_pipe_datainterface_t, Poller::Client
{
	ftp_client_pipe_t cp;
public:
	int handle_data_io(int fd, short revents, clock_t now);
	int notifyPollEvent(Poller::PollEvent *event) { 
		DPRINT(("test notifyPollEvent: fd %d, client %p\n", event->fd, event->client));
		return cp.notifyPollEvent(event);
	}
	void fetchFile(const char *hostname, const char *user, const char *pass, const char *fname);
	void ftpCmdDone(int xerr, int status, const char *buf);
};

/*----------------------------------------------------------------------
 User-supplied function.
 The operating system has told us that our data file descriptor
 is ready for I/O.  This function deals with it.

 This is called internally by handle_io().
 It must be overridden by the user with a useful method.

 Function must issue a single read or write on the fd (as appropriate 
 for the call that triggered the transfer) with as large a buffer as 
 is practical.
 If read reads zero bytes, the transfer is over.
 When the transfer is over, this routine must return '0' without
 closing the file.
 If the transfer is not over, this routine must return the
 number of bytes tranferred during this call.
----------------------------------------------------------------------*/
int ftp_client_pipe_test_t::handle_data_io(int fd, short revents, clock_t now)
{
	char buf[16384];
	(void) now;
	int nread = read(fd, buf, sizeof(buf));
	if (nread == 0) {
		DPRINT(("ftp_client_pipe_test_t::handle_data_io: fd %d, revents %x; transfer over\n", fd, revents));
		return 0;
	}
	if (nread == -1) {
		DPRINT(("ftp_client_pipe_test_t::handle_data_io: fd %d, revents %x; read fails, errno %d\n", fd, revents, errno));
		return 0;
	}
	DPRINT(("ftp_client_pipe_test_t::handle_data_io: fd %d, revents %x; read %d bytes\n", fd, revents, nread));
	//printf("Got '%.*s'\n", nread, buf);
	return nread;
}


/*----------------------------------------------------------------------
 App-supplied function.
 The current command just finished.
----------------------------------------------------------------------*/
void ftp_client_pipe_test_t::ftpCmdDone(int xerr, int status, const char *buf)
{
	DPRINT(("ftp_client_pipe_test_t::ftpCmdDone(%d, %d, %s)\n", xerr, status, buf));
	g_done = true;
	g_status = status;
	g_xerr = xerr;
	sscanf(buf, "%*d %d", &g_size);
}

/*----------------------------------------------------------------------
 Fetch a file from an ftp server.
----------------------------------------------------------------------*/
void ftp_client_pipe_test_t::fetchFile(const char *hostname, const char *user, const char *pass, const char *fname)
{
	int err;
	enum state_t {LOGGING_IN,SETTING,GETTING,DONE};
	state_t state;
	Sked sk;

	err = sk.init();
	if (err) {
		printf("main: Sked::init() failed.\n");
		exit(1);
	}

	Poller_poll m_poller;
	err = m_poller.init();
	CHECK(err, 0);

	cp.init(this, &sk, 56000/8, &m_poller, 0);
	err = cp.connect(hostname, 21);	/* 21 is standard ftp server port */
	CHECK(err, 0);

	g_done = false;
	state = LOGGING_IN;
	err = cp.login(user, pass);
	CHECK(err, 0);

	clock_t tix_per_second = eclock_hertz();
	assert(tix_per_second < 100000);	/* numerous overflows otherwise */

	/* Start pumping data to the pipe */
	while (state != DONE) {
		/* Handle pending stuff */
		clock_t now = eclock();
		sk.runAll(now);

		/* Did the last command finish? */
		if (g_done) {
			g_done = false;
			switch(state) {
			case LOGGING_IN:
				if (g_status == 230) {
					/* login succeeded.  */
					printf("main:LOGGING_IN: done.  Fetching %s\n", fname);
					/* Switch to binary mode. */
					err = cp.type("I");
					CHECK(err, 0);
					state = SETTING;
				} else {
					printf("main:LOGGING_IN: Unexpected status %d or xerr %d, test failed.\n", g_status, g_xerr);
					exit(1);
				}
				break;

			case SETTING:
				if (g_status == 200) {
					/* type I succeeded.  */
					printf("main:SETTING: done. TYPE I\n");
					/* Fetch the file using passive mode. */
					err = cp.get(fname, true);
					CHECK(err, 0);
					state = GETTING;
				} else {
					printf("main:SETTING: Unexpected status %d or xerr %d, test failed.\n", g_status, g_xerr);
					exit(1);
				}
				break;

			case GETTING:
				if (!g_xerr && STATUS_OK(g_status)) {
					printf("main: transfer succeeded\n");
					state = DONE;
					break;
				} else {
					printf("main:GETTING: Unexpected status %d or xerr %d, test failed.\n", g_status, g_xerr);
					exit(1);
				}

			default:
				printf("bug\n");
				exit(1);
			}
		}

		/* Call poller.waitForEvents() to find out what handles are ready for read or write */
		clock_t tixUntilNextEvent = sk.nextTime(now + tix_per_second) - now;
		int msUntilNextEvent = (tixUntilNextEvent * 1000) / tix_per_second + 1;
		int rfds = m_poller.waitForEvents(msUntilNextEvent);
		DPRINT(("main: state %d, rfds %d\n", state, rfds));
		if (rfds < 0) {
			perror("poll");
			exit(1);
		}

		if (rfds != EWOULDBLOCK) {
			/* At least one handle is ready.  Deal with it. */
			while (1) {
				Poller::PollEvent event;
				err = m_poller.getNextEvent(&event);
				if (err == EWOULDBLOCK)
					break;
				CHECK(0, err);
				printf("main: fd %d, revents %x, state %d\n", 
					event.fd, event.revents, state);
				err = event.client->notifyPollEvent(&event);
				if (err) {
					printf("main: Error %d on fd %d.  Closing.\n", err, event.fd);
					cp.shutdown();
					CHECK(0,1);
				}
			}
		}

	}
	printf("fetchFile returning.\n");
}

/* Main program to test the ftp client pipe module.
 * This is an attended test - i.e. you have to make sure the file
 * is actually on the given ftp server before you run it.
 * Prints message and exits with nonzero status if any test fails.
 */
int main( int argc, char **argv )
{

	if (argc != 5) {
		printf("\
Usage: %s host username password filename_to_get\n\
Logs in to the given ftp server with the given username and password, and\n\
retrieves the given file.\n", argv[0]);
		exit(1);
	}
	ftp_client_pipe_test_t test;

	test.fetchFile(argv[1], argv[2], argv[3], argv[4]);
	printf("No tests failed.\n" );
	return 0;
}

