#include "ftp_client_proto.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void check(int d, int e, int line) { if (d != e) {
		printf("check: test failed: %d != %d at line %d\n", d, e, line);
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
		printf("strcheck: test failed: '%s' != '%s' at line %d\n", d, e, line);
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
#define STRCHECK(d, e) strcheck(d, e, __LINE__)
#define STATUSCHECK(d, e) statuscheck(d, e, __LINE__)

#define SLEN 1024

/* Main program to test the ftp client protocol module.
 * Doesn't need any networking; simulates the server itself.
 * Prints message and exits with nonzero status if any test fails.
 */
int main(int argc, char **argv)
{
	ftp_client_proto_t ftp;
	int err;
	char sbuf[SLEN];
	const char *p;

	(void) argc; (void) argv;

	ftp.init();
	CHECK(ftp.isOutputReady(), false);

	/*---------------------*/
	/* Test login() method */
	err = ftp.login("user", "password");		/* start login */
	CHECK(err,0);

	/* Simulate server's response to connect */
	CHECK(ftp.isInputReady(), true);
	err = ftp.giveInput("221 Hello, welcome to ftp.null.net");
	CHECK(err,0);
	CHECK(ftp.isOutputReady(), true);
	p = ftp.getOutput();
	STRCHECK(p, "USER user\r\n");				/* it should send USER... */
	err = ftp.advanceOutput();
	CHECK(err,0);
	CHECK(ftp.isOutputReady(), false);

	/* Simulate server's response to USER */
	CHECK(ftp.isInputReady(), true);
	err = ftp.giveInput("331 User name OK, need password.");
	CHECK(err,0);
	CHECK(ftp.isOutputReady(), true);
	p = ftp.getOutput();
	STRCHECK(p, "PASS password\r\n");			/* followed by PASS. */
	err = ftp.advanceOutput();
	CHECK(err,0);
	CHECK(ftp.isOutputReady(), false);
	CHECK(ftp.getStatus(sbuf,SLEN), 0);			/* command shouldn't end... */

	/* Simulate server's response to PASS */
	CHECK(ftp.isInputReady(), true);
	err = ftp.giveInput("230 Login successful.");
	CHECK(err,0);
	CHECK(ftp.isOutputReady(), false);
	CHECK(ftp.getStatus(sbuf, SLEN), 230);		/* ... until 230 comes back. */
	STRCHECK(sbuf, "230 Login successful.");

	/*------------------*/
	/* Test cd() method */
	err = ftp.cd("pub");
	CHECK(err,0);
	CHECK(ftp.isOutputReady(), true);
	p = ftp.getOutput();
	STRCHECK(p, "CWD pub\r\n");
	err = ftp.advanceOutput();
	CHECK(err,0);
	CHECK(ftp.getStatus(sbuf, SLEN), 0);
	CHECK(ftp.isOutputReady(), false);

	/* Simulate server's response to CWD */
	CHECK(ftp.isInputReady(), true);
	err = ftp.giveInput("250 CWD command successful.");
	CHECK(err,0);
	CHECK(ftp.getStatus(sbuf, SLEN), 250);
	STRCHECK(sbuf, "250 CWD command successful.");

	/*--------------------*/
	/* Test type() method */
	err = ftp.type("I");
	CHECK(err,0);
	CHECK(ftp.isOutputReady(), true);
	p = ftp.getOutput();
	STRCHECK(p, "TYPE I\r\n");
	err = ftp.advanceOutput();
	CHECK(err,0);
	CHECK(ftp.getStatus(sbuf, SLEN), 0);
	CHECK(ftp.isOutputReady(), false);

	/* Simulate server's response to TYPE */
	CHECK(ftp.isInputReady(), true);
	err = ftp.giveInput("200 TYPE command successful.");
	CHECK(err,0);
	CHECK(ftp.getStatus(sbuf, SLEN), 200);
	STRCHECK(sbuf, "200 TYPE command successful.");

	/*------------------*/
	/* Test ls() method */
	/* not written yet */

	/* etc. */

	printf("No tests failed.\n");
	return 0;
}

