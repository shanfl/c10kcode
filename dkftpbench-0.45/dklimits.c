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

/*--------------------------------------------------------------------------
 Program to measure limits on network sockets, file descriptors, etc.
--------------------------------------------------------------------------*/

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif

#define MAX_TEST 100000

int fds[MAX_TEST];

static int count_fds(int want)
{
	int i;
	int nfds;
	for (i=0; i<want; i++) {
		fds[i] = dup(0);
		if (fds[i] == -1)
			break;
	}
	nfds = i;
	for (i=0; i<nfds; i++)
		close(fds[i]);
	return nfds;
}

static int count_files(int want)
{
	int i;
	int maxfiles;

	for (i=0; i<want; i++) {
		char buf[1024];
		sprintf(buf, "/tmp/fd%d", i);
		fds[i] = creat(buf, S_IRUSR | S_IWUSR);
		if(fds[i] == -1)
			break;
	}
	maxfiles = i;
	for (i=0; i<maxfiles; i++) {
		char buf[1024];
		close(fds[i]);
		sprintf(buf, "/tmp/fd%d", i);
		unlink(buf);
	}
	return maxfiles;
}

static int count_sockets(int want)
{
	int max_socks;
	int i;

	for (i = 0; i < want; i++) {
		struct sockaddr_in srv_addr;

		fds[i] = socket(AF_INET, SOCK_STREAM, 0);
		if (fds[i] < 0)
			break;

		memset((char *) &srv_addr, 0, sizeof(srv_addr));
		srv_addr.sin_family = AF_INET;
		srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		srv_addr.sin_port = 0;

		if (bind(fds[i], (struct sockaddr *) &srv_addr, sizeof(srv_addr)) < 0) 
			break;
	}
	max_socks = i;

	for (i = 0; i < max_socks; i++)
		close(fds[i]);

	return max_socks;
}

static int count_socketpairs(int addressFamily, int want)
{
	int max_socks;
	int i;

	if (want & 1)
		want--;
	for (i = 0; i < want; i+=2) {
		int err = socketpair(addressFamily, SOCK_STREAM, 0, fds+i);
		if (err)
			break;
	}
	max_socks = i;

	for (i = 0; i < max_socks; i++)
		close(fds[i]);

	return max_socks;
}

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

static int count_poll(int want)
{
	struct pollfd pfds[MAX_TEST];
	int i;
	int j;

	for (i = 0; i < want; i++) {
		pfds[i].fd = 1;
		pfds[i].events = POLLOUT;
	}
	for (i = 1; i <= want; i = i * 2)
		if (poll(pfds, i, 10) < 0)
			break;
	for (j = i/2; (j < i) && (j < want); j++)
		if (poll(pfds, j, 10) < 0)
			break;
	return j;
}

static int count_connects(int ipadr, int portnum, int want)
{
	int i;
	int maxfd;
	int nopen;
	struct pollfd pfds[65536];
	int nok;
	int nrej;
	time_t end;

#ifdef VERBOSE
	printf("Starting connections until we can't start any more; will print what stops us to stderr.\n");
#endif
	for (i = 0; i < want; i++) {
		struct sockaddr_in srv_addr;
		int err;

		fds[i] = socket(AF_INET, SOCK_STREAM, 0);
		if (fds[i] < 0) {
#ifdef VERBOSE
			perror("socket");
#endif
			break;
		}
		setNonblocking(fds[i]);	/* else connect will wait until resources avail */

		memset((char *) &srv_addr, 0, sizeof(srv_addr));
		srv_addr.sin_family = AF_INET;
		srv_addr.sin_addr.s_addr = ipadr;
		srv_addr.sin_port = htons(portnum);	/* daytime service */

		err = connect(fds[i], (struct sockaddr *) &srv_addr, sizeof(srv_addr));
		if ((err != 0) && (errno != EINPROGRESS)) {
#ifdef VERBOSE
			perror("connect");
#endif
			break;
		}
		
		pfds[i].fd = fds[i];
		pfds[i].events = POLLOUT;
	}
	maxfd = i;
#ifdef VERBOSE
	printf("%d connections in progress...\n", maxfd);
#endif

	/* Now sit until they all connect or fail, or until five seconds
	 * pass, whichever comes first.
	 */
	end = time(0) + 6;
	for (nrej=nok=0, nopen=maxfd; nopen; ) {
		int nready;
		if ((end - time(0)) < 0)
			break;
		nready = poll(pfds, maxfd, 1000);
		if (nready < 0) {
			perror("poll");
			exit(1);
		}
		for (i=0; i<maxfd; i++) {
			if ((pfds[i].fd == -1) || !pfds[i].revents)
				continue;

			/* question: if a connect fails, will it show up first
			 * as a POLLERR, POLLHUP, or POLLOUT?
			 */
			if (pfds[i].revents & (POLLHUP|POLLERR)) {
				/* connect failed? */
				close(pfds[i].fd);
				nrej++;
			} else if (pfds[i].revents & POLLOUT) {
				/* check to see if connect succeeded */
				int connecterr = -1;
				socklen_t len = sizeof(connecterr);
				if (getsockopt(pfds[i].fd, SOL_SOCKET, SO_ERROR, (char *)&connecterr, &len) < 0) {
					perror("getsockopt");
					exit(1);
				}
				if (!connecterr) {
					nok++;
					/* keep socket open */
				} else if (connecterr ==  ECONNREFUSED) {
					close(pfds[i].fd);
					nrej++;
				}
			} else {
				printf("bad poll result: pfds[%d].fd %d, .revents %x\n", i, pfds[i].fd, pfds[i].revents);
				exit(1);
			}
			pfds[i].fd = -1;
			nopen--;
		}
	}

	/* Close 'em (might close a few extra, but that's ok) */
	for (i = 0; i < maxfd; i++)
		close(fds[i]);

#ifdef VERBOSE
	printf("%d connections accepted, %d rejected, %d pending\n", nok, nrej, nopen);
#endif
	return nok + nrej + nopen;
}

int main(int argc, char **argv)
{
	const char *ipadrstr;
	int ipadr;
	int portnum;
	int want;

	if (argc < 2) {
		printf("Usage: %s num_wanted [ipadr portnum]\n\
Tries to open the specified number of sockets in various ways.\n\
", argv[0]);
		exit(1);
	}
	want = atoi(argv[1]);
	if (want < 1 || want > MAX_TEST) {
		printf("Sorry, want must be between 1 and %d\n", MAX_TEST);
		exit(1);
	}

	if (argc < 3)
		ipadrstr = "127.0.0.1";
	else
		ipadrstr = argv[2];
	ipadr = inet_addr(ipadrstr);
	if (ipadr == -1) {
		printf("Bad ip address %s.\n", ipadrstr);
		exit(1);
	}

	if (argc < 4)
		portnum = 80;
	else {
		const char *portnumstr;
		portnumstr = argv[3];
		portnum = atoi(portnumstr);
		if ((portnum <= 0) || (portnum > 65535)) {
			printf("Bad port number '%s'.  Must be number between 1 and 65535.\n", portnumstr);
			exit(1);
		}
	}

#ifdef LINUX
	/* Linux-specific system tuning data */
	printf("/proc/sys/net/ipv4/ip_local_port_range is: ");
	fflush(stdout);
	system("cat /proc/sys/net/ipv4/ip_local_port_range");

	printf("/proc/sys/fs/file-nr is: ");
	fflush(stdout);
	system("cat /proc/sys/fs/file-nr");

	printf("/proc/sys/fs/file-max is: ");
	fflush(stdout);
	system("cat /proc/sys/fs/file-max");

	printf("/proc/sys/fs/inode-nr is: ");
	fflush(stdout);
	system("cat /proc/sys/fs/inode-nr");

	printf("/proc/sys/fs/inode-max is: ");
	fflush(stdout);
	system("cat /proc/sys/fs/inode-max");
#endif

	printf("Can open %d AF_LOCAL sockets with socketpair\n", count_socketpairs(AF_LOCAL, want));
	printf("Can open %d AF_INET sockets with socketpair\n", count_socketpairs(AF_INET, want));
	printf("Can open %d fds\n", count_fds(want));
	printf("Can open %d files\n", count_files(want));
	printf("Can poll %d sockets\n", count_poll(want));
	printf("Can bind %d ephemeral ports\n", count_sockets(want));
	//sleep(3);
	//printf("Can start %d nonblocking connect()'s\n", count_connects(ipadr, portnum, want));
	return 0;
}
