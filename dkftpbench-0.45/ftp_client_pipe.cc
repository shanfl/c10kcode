/*--------------------------------------------------------------------------
 Copyright 1999, 2000 by Dan Kegel http://www.kegel.com/
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
 Module to handle the networking calls for the client side of the FTP
 protocol.  Delegates the work of parsing and generating messages to
 the ftp_client_protocol module.
--------------------------------------------------------------------------*/
#include "dprint.h"
#include "ftp_client_pipe.h"

#include <sys/socket.h> 	/* for AF_INET */
#include <arpa/inet.h> 
#include <assert.h> 
#include <errno.h> 
#include <fcntl.h> 
#include <netdb.h> 
#include <poll.h> 
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h> 

/* Or this flag into the 'events' argument of handle_io()
 * to indicate a kickstart call from inside a wrapper function
 */
#define KICKSTART 0x8000

/* return whether the given status code indicates a given FTP command status */
#define STATUS_OK(s)  (((s) >= 200) && ((s) <= 299))

#define GOTO_STATE(fn, s) do { DPRINT((fn ":goto_state(%d): old state %d, cfd %d; line %d\n", s, m_state, m_cfd, __LINE__)); m_state = s; } while (0)

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

/*----------------------------------------------------------------------
 Initialize this object, and mark it as not connected.
 The calling object should implement the ftp_client_pipe_datainterface_t 
 interface and pass a pointer to itself.
 The remining arguments are a pointer to the shared global scheduler,
 and the maximum bytes per second to allow this client.
 After each call to handle_data_read(), this module will use the
 scheduler to sleep long enough to stay under the specified bandwidth.
 @param local_addr if not 0, local_addr specifies the local IP address
 to use for the local side of the connection.  Must be stable pointer.
 ----------------------------------------------------------------------*/
void ftp_client_pipe_t::init(ftp_client_pipe_datainterface_t * datainterface, Sked *sked, int max_bytes_per_sec, Poller *poller, struct sockaddr_in *local_addr)
{ 
	m_local_addr = local_addr;
	m_poller = poller;
	m_proto.init();
	m_ibuf.init();
	m_obuf.init();
	m_in_ftpCmdDone = 0;
	m_cfd = -1;
	m_dfd = -1;
	m_iline[0] = 0;
	m_cfd_connecting = m_dfd_connecting = false;
	m_datainterface = datainterface;
	m_sked = sked;
	m_bytesPerSec = max_bytes_per_sec;
	m_bytesPerTick = max_bytes_per_sec / eclock_hertz();
	//if (m_bytesPerTick < 1) {
		//printf("warning: max_bytes_per_sec %d < eclock_hertz() %d; clamping to 1 byte per tick\n",
			//max_bytes_per_sec, eclock_hertz());
		//m_bytesPerTick = 1;
	//}
	m_dfd_events = 0;
	GOTO_STATE("init", IDLE);
	DPRINT(("ftp_client_pipe_t::init(%p, %p, %d): bytes_per_tick %d\n",
		datainterface, sked, max_bytes_per_sec, m_bytesPerTick));
}

/* 
 Do a connect with the given local address, and pick an ephemeral port by 
 hand.  Yuck! 
 @param dest where to connect to
 @param src NULL, or IP address to connect *from*.  Note: src->port is updated!
 */
static int ephemeral_connect(int sock, struct sockaddr_in *dest, struct sockaddr_in *src)
{
	const int minport = 1024;
	const int maxport = 51023;

	DPRINT(("ephemeral_connect(fd %d, dest %p, src %p)\n",
		sock, dest, src));
	if (!src)
		return ::connect(sock, (struct sockaddr *) dest, sizeof(*dest));

	int remaining = maxport - minport;
	while (remaining--) {
		/* Increment the ephemeral address for that address */
		unsigned short port = ntohs(src->sin_port) + 1;
		if ((port < minport) || (port > maxport)) 
			port = minport;
		src->sin_port = htons(port);

		DPRINT(("ephemeral_connect: trying port %d\n", port));
		if (bind(sock, (struct sockaddr *)src, sizeof(*src))) {
			if (errno != EADDRINUSE) {
				DPRINT(("ephemeral_connect: bind failed, errno %d\n", errno));
				return -1;
			}
		} else
			break;
	}

	return ::connect(sock, (struct sockaddr *)dest, sizeof(*dest));
}

/*----------------------------------------------------------------------
 Initialize this object and start a connection to the given server.
----------------------------------------------------------------------*/
int ftp_client_pipe_t::connect(const char *hostname, int port)
{
	DPRINT(("ftp_client_pipe_t::connect(%s, %d)\n", hostname, port));

	/* reinit all fields but m_datainterface */
	init(m_datainterface, m_sked, m_bytesPerSec, m_poller, m_local_addr);	

	struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    /* If the argument is a numerical IP address, parse it directly;
	 * else try to look it up in DNS. 
	 */
    if (!inet_aton(hostname, &address.sin_addr)) {
		struct hostent * host;
        host = gethostbyname(hostname);

		if (!host) {
			/* We can't find an IP number */
			int err;
			switch (h_errno) {
			case HOST_NOT_FOUND:
				err = ENOENT; DPRINT(("gethostby*: HOST_NOT_FOUND\n")); break;
			case NO_DATA:
				err = ENOENT; DPRINT(("gethostby*: NO_DATA\n")); break;
			case NO_RECOVERY:
				err = ENOENT; DPRINT(("gethostby*: NO_RECOVERY\n")); break;
			case TRY_AGAIN:
				err = ENOENT; DPRINT(("gethostby*: TRY_AGAIN\n")); break;
			default:
				err = ENOENT; DPRINT(("gethostby*: h_errno %d???\n", h_errno)); break;
			}
			DPRINT(("error looking up host, returning %d\n", err));
			return err;
		}
		/* Take the first IP address associated with this hostname */
		memcpy(&address.sin_addr, host->h_addr_list[0],
			   sizeof(address.sin_addr));
	}

	int sock;
	int err;
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		err = errno;
		DPRINT(("ftp_client_pipe_t::connect: socket failed, returning %d\n", err));
        return err;
	}

	if (setNonblocking(sock) == -1) {
		err = errno;
		DPRINT(("ftp_client_pipe_t::init: setNonblocking failed, returning %d\n", err));
		close(sock);
        return err;
	}

    if (ephemeral_connect(sock, &address, m_local_addr)) {
		err = errno;
		if (errno != EINPROGRESS) {
			DPRINT(("ftp_client_pipe_t::init: Connect failed, returning %d\n", err));
			close(sock);
			return err;
		}
		m_cfd_connecting = true;
		DPRINT(("ftp_client_pipe_t::init: Waiting for connect to finish, m_cfd %d\n", sock));
	} else {
		DPRINT(("ftp_client_pipe_t::init: Connect succeded early, m_cfd %d\n", sock));
		m_cfd_connecting = false;
	}

	m_cfd = sock;
	err = m_poller->add(m_cfd, this, POLLIN|POLLOUT);
	if (err) {
		EDPRINT(("ftp_client_pipe_t::init: add failed\n"));
		return err;
	}

	m_iline_full = false;

	/* Abort if no response in five seconds */
	m_sked->addClient(this, eclock()+ (5 * eclock_hertz()));

	return 0;
}

/*----------------------------------------------------------------------
 Call this when done with the session.
 Closes the file descriptors.
 Returns 0 on success, else unix error code.
----------------------------------------------------------------------*/
int ftp_client_pipe_t::shutdown()
{
	int err1 = 0, err2 = 0;
	int cfd, dfd;

	EDPRINT(("ftp_client_pipe_t::shutdown(): m_state %d, cfd %d, dfd %d, id %d\n", 
		m_state, m_cfd, m_dfd, m_datainterface->getID()));

	if ((m_cfd == -1) && (m_dfd == -1))
		return 0;				/* don't do anything if already shut down */

	m_sked->delClient(this);	/* just in case some event is pending */

	GOTO_STATE("shutdown", IDLE);

	m_cfd_connecting = false;
	m_dfd_connecting = false;

	dfd = m_dfd;
	cfd = m_cfd;
	m_dfd = -1;
	m_cfd = -1;

	if (cfd != -1) {
		m_poller->del(cfd);
		err1 = close(cfd);
	}
	if (dfd != -1) {
		m_poller->del(dfd);
		err2 = close(dfd);
	}

	return err1 ? err1 : err2;
}

/**
 Call m_datainterface->ftpCmdDone() if not already in it; avoids re-entry.
 Ah, the joys of immediate callbacks.
*/
void ftp_client_pipe_t::call_ftpCmdDone(int xerr, int status, const char *statusbuf) 
{
	if (m_in_ftpCmdDone == 0) {
		m_in_ftpCmdDone++;
		m_datainterface->ftpCmdDone(xerr, status, statusbuf);
		m_in_ftpCmdDone--;
	}
}

/// Handle events on the data file descriptor.  Returns EALREADY if we might be done.
int ftp_client_pipe_t::notifyPollEvent_dfd(Poller::PollEvent *event)
{
	int xerr = 0;		/* error to call ftpClientDone() with */
	int fd = event->fd;
	int revents = event->revents;
	clock_t now = eclock();

	int nxfer = 0;
	bool hup = false;

	assert(m_state != IDLE);
	if (m_state == IDLE)
		return 0;

	if (m_dfd_connecting && ((revents & KICKSTART|POLLOUT) == POLLOUT)) {
		/* check to see if connect succeeded */
		int connecterr = -1;
		socklen_t len = sizeof(connecterr);
		if (getsockopt(m_dfd, SOL_SOCKET, SO_ERROR, (char *)&connecterr, &len) < 0) {
			xerr = errno;
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_dfd: getsockopt dfd %d fails, errno %d\n", m_dfd, xerr));
			/* Caller will call stop() */
			return xerr;
		}
		if (connecterr == EINPROGRESS) {
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_dfd: connect dfd %d EINPROGRESS\n", m_dfd));
			m_poller->clearReadiness(m_dfd, POLLOUT);
		} else if (connecterr) {
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_dfd: connect dfd %d failed, errno %d\n", m_dfd, connecterr));
			/* Caller will call stop() */
			return connecterr;
		} else {
			m_dfd_connecting = false;
			DPRINT(("ftp_client_pipe_t::notifyPollEvent_dfd: Connect dfd %d succeeded\n", m_dfd));
		}
	}

	/* If the OS is ready before we are, sleep */
	/* Skip this if KICKSTART, because we don't want to sleep at first */
	/* Skip this if POLLHUP, because the socket is about to close, no point sleeping */
	if ((m_state == GETTING) 
	&& !(revents & (KICKSTART|POLLHUP))
	&& eclock_after(m_wakeup, now)) {
		DPRINT(("ftp_client_pipe_t::notifyPollEvent_dfd: dfd %d still needs to snooze for %d ticks\n", m_dfd, m_wakeup - now));
		assert(skedIndex == 0);		/* we shouldn't be scheduled yet */
		m_sked->addClient(this, m_wakeup);
		GOTO_STATE("notifyPollEvent_dfd", SLEEPING);
	}

	/* If sleeping, and it's just a read event, just remember it */
	if (m_state == SLEEPING) {
		m_dfd_events |= revents;
		DPRINT(("ftp_client_pipe_t::notifyPollEvent_dfd: dfd %d asleep; revents %x, m_dfd_events %x\n", m_dfd, revents, m_dfd_events));
	} else if (((m_state == GETTING) && (revents & POLLIN)) 
		   ||  ((m_state == PUTTING) && (revents & POLLOUT))) {
		/* cancel timeout */
		m_sked->delClient(this);
		/* If appropriate, transfer a packet */
		DPRINT(("ftp_client_pipe_t::notifyPollEvent_dfd: calling subclass\n"));
		nxfer = m_datainterface->handle_data_io(fd, revents, now);
		if (nxfer == 0) 
			hup = true;

		if (nxfer == -EWOULDBLOCK) {
			// You must tell poller about EWOULDBLOCK -- it has no other way to
			// know that a socket is no longer ready for I/O!
			m_poller->clearReadiness(m_dfd, (m_state == GETTING) ? POLLIN : POLLOUT);
			nxfer = 0;
		}
		if (nxfer < 0) {
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_dfd: subclass returns error %d!  Returning EPIPE.\n", -nxfer));
			/* Caller will call stop() */
			return EPIPE;
		}
	} else if (revents & POLLHUP) {
		hup = true;
	}

	/* If handle is done (either via POLLHUP or nxfer = 0), close it */
	if (hup) {
		GOTO_STATE("notifyPollEvent_dfd", IDLE);
		DPRINT(("ftp_client_pipe_t::notifyPollEvent_dfd: closing dfd %d\n", m_dfd));
		m_sked->delClient(this);	/* just in case some event is pending */
		int dfd = m_dfd;
		m_dfd = -1;
		m_poller->del(dfd);
		close(dfd);
		/* If a data connection is finished, so are we, and
		 * we might need to notify app that the command is done.
		 */
		return EALREADY;
	} 

	if (m_state == GETTING) {
		/* Normal read.  Throttle. */
		nxfer += m_bytesUnsleptFor;
		clock_t howlong;
		static int hertz = eclock_hertz();
		if (m_bytesPerTick < 5) {
			howlong = (nxfer * hertz) / m_bytesPerSec;
			m_bytesUnsleptFor = nxfer - (howlong * m_bytesPerSec) / hertz;
		} else {
			howlong = nxfer / m_bytesPerTick;
			m_bytesUnsleptFor = nxfer % m_bytesPerTick;
		}
		m_wakeup = now + howlong;
		DPRINT(("ftp_client_pipe_t::notifyPollEvent_dfd: dfd %d read %d bytes, won't read again for %d ticks\n", m_dfd, nxfer, howlong));
		m_dfd_events = 0;
	}
	return 0;
}

/// Handle events on the control file descriptor.  Returns EALREADY if we might be done.
int ftp_client_pipe_t::notifyPollEvent_cfd(Poller::PollEvent *event)
{
	int xerr = 0;		/* error to call ftpClientDone() with */
	int status = 0;
	int fd = event->fd;
	int revents = event->revents;

	DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd(fd %d, %x): state %d, cning %d\n",
		fd, revents, m_state, m_cfd_connecting));
	assert(fd != -1);
	if (revents & (POLLERR | POLLHUP)) {
		EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: poll indicates fd %d screwed up, revents %x\n", m_cfd, revents));
		return EPIPE; 
	}

	if (m_cfd_connecting && ((revents & (POLLOUT|KICKSTART)) == POLLOUT)) {
		/* check to see if connect succeeded */
		int connecterr = -1;
		socklen_t len = sizeof(connecterr);
		if (getsockopt(m_cfd, SOL_SOCKET, SO_ERROR, (char *)&connecterr, &len) < 0) {
			int err = errno;
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: getsockopt cfd %d fails, errno %d\n", m_cfd, err));
			return err;
		}
		if (connecterr == EINPROGRESS) {
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: connect cfd %d EINPROGRESS\n", m_cfd));
			m_poller->clearReadiness(m_cfd, POLLOUT);
		} else if (connecterr) {
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: cfd %d connect failed, errno %d\n", m_cfd, connecterr));
			/* Caller will call stop() */
			return connecterr;
		} else {
			m_cfd_connecting = false;
			DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: Connect cfd %d succeeded\n", m_cfd));
		}
	}
	if (revents & POLLIN) {
		/* Read it */
		int err = m_ibuf.fillFrom(m_cfd);
		if (err && (err != EWOULDBLOCK)) {
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: fillFrom returns %d?\n", err));
			return err;
		}
		// You must tell poller about EWOULDBLOCK -- it has no other way to
		// know that a socket is no longer ready for I/O!
		if (err == EWOULDBLOCK)
			m_poller->clearReadiness(m_cfd, POLLIN);
	}
	//DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd(fd %d, %d): before loop: m_proto.isInputReady() %d, m_proto.isOutputReady() %d\n", fd, (int) revents, m_proto.isInputReady(), m_proto.isOutputReady())); 
	/* Process all lines in m_ibuf */
	int puterr = 0;
	int geterr = 0;
	while ((m_proto.isInputReady() && (geterr != EWOULDBLOCK))
	    || (m_proto.isOutputReady() && (puterr != EWOULDBLOCK))) {
		//DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd(%d): m_proto.isInputReady() %d, geterr %d, m_proto.isOutputReady() %d, puterr %d\n", revents, m_proto.isInputReady(), geterr, m_proto.isOutputReady(), puterr)); 
		if (!m_iline_full) {
			geterr = m_ibuf.readline(m_iline, sizeof(m_iline));
			if (!geterr) {
				m_iline_full = true;
				DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd fd %d got '%s'\n", m_cfd, m_iline));
			} else if (geterr != EWOULDBLOCK)
				return geterr;
		}
		if (m_iline_full) {
			if (m_state == IDLE) {
				/* cancel timeout*/
				m_sked->delClient(this);
			}

			/* A line is ready.  Process it. */
			int err = m_proto.giveInput(m_iline);
			if (!err) {
				m_iline_full = false;
			} else if (err != EWOULDBLOCK) {
				EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: m_proto.giveInput returns %d\n",err));
				return err;
			}
			status = m_proto.getStatus(NULL, 0);
			if (status) {
				/* If a command is finished, so are we. */
				return EALREADY;
			}
		}
		int len;
		const char *p;
		while ((p = m_proto.getOutput(&len)) != NULL) {
			DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd fd %d put '%s'\n", m_cfd, p));
			puterr = m_obuf.put(p, len);
			if (puterr == EWOULDBLOCK) {
				DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: put returns %d?\n", puterr));
				break;
			} else if (puterr) {
				EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: put returns %d?\n", puterr));
				return puterr;
			}
			m_proto.advanceOutput();
		}
	}
	// Try writing even if we don't know that we're writable --
	// since we didn't express interest in writing before,
	// this should save us one notification cycle.
	// 'Tis easier to ask forgiveness than permission.
	//if (revents & POLLOUT) {
		if (!m_cfd_connecting) {
			/* Write buffer to network. */
			int err = m_obuf.flushTo(m_cfd);
			if (err && (err != EWOULDBLOCK)) {
				EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: flushTo returns %d?\n", err));
				return err;
			}
			// You must tell poller about EWOULDBLOCK -- it has no other way to
			// know that a socket is no longer ready for I/O!
			if (err == EWOULDBLOCK)
				m_poller->clearReadiness(m_cfd, POLLOUT);
		}
		if (m_obuf.isEmpty() && m_proto.isQuit()) {
			DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: session quit, output done, please shut me down\n"));
			return EPIPE;
		}
	//}

	/* If it's time for us to open a data connection to a port on the
	 * server, do so
	 */
#define GETBYTE3(x) (((x) >> 24) & 255)
#define GETBYTE2(x) (((x) >> 16) & 255)
#define GETBYTE1(x) (((x) >> 8) & 255)
#define GETBYTE0(x) ((x) & 255)
	struct sockaddr_in address;
	if (m_proto.isPortReady(&address)) {
		int sock;

#ifdef USE_DPRINT
		int addr = ntohl(address.sin_addr.s_addr);
		int port = ntohs(address.sin_port);

		DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: isPortReady TRUE, address %d.%d.%d.%d:%d\n", 
			GETBYTE3(addr), 
			GETBYTE2(addr), 
			GETBYTE1(addr), 
			GETBYTE0(addr), 
			ntohs(port)));
#endif

		if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
			xerr = errno;
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: connect failed, errno %d\n", xerr));
			/* Caller will call stop() */
			return xerr;
		}

		if (setNonblocking(sock) == -1) {
			xerr = errno;
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: setNonblocking failed, returning %d\n", xerr));
			/* Caller will call stop() */
			return xerr;
		}
		m_dfd_connecting = true;
		m_dfd = sock;
		int err = m_poller->add(m_dfd, this, POLLIN|POLLOUT);
		if (err) {
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: add failed\n"));
			return err;
		}
		if (ephemeral_connect(sock, &address, m_local_addr)) {
			if (errno != EINPROGRESS) {
				m_dfd_connecting = false;
				xerr = errno;
				EDPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: Connect fd %d failed, returning %d\n", sock, xerr));
				/* Caller will call stop() */
				return xerr;
			}
		} else {
			DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: Connect fd %d succeeded early\n", sock));
			m_dfd_connecting = false;
		}
		DPRINT(("ftp_client_pipe_t::notifyPollEvent_cfd: socket returns dfd %d\n", m_dfd));
	}

	return 0;
}

/*----------------------------------------------------------------------
 The operating system has told us that one of our file descriptors
 is ready for I/O.  Deal with it.
 fd and revents are from poll()'s output array or sigtimedwait();

 Returns 0 on success, Unix error code on failure.
 If this returns an error, call shutdown() to close this session.

 This is normally called by the app after a call to poll() or 
 sigtimedwait(), or internally by skedCallback().
----------------------------------------------------------------------*/
int ftp_client_pipe_t::notifyPollEvent(Poller::PollEvent *event)
{
	int xerr;		/* error to call ftpClientDone() with */
	int err;

	DPRINT(("ftp_client_pipe_t::notifyPollEvent(fd %d, %x): state %d\n", event->fd, event->revents, m_state));
	assert(event->fd != -1);

	if (event->fd == m_dfd)
		xerr = notifyPollEvent_dfd(event);
	else if (event->fd == m_cfd)
		xerr = notifyPollEvent_cfd(event);
	else {
		EDPRINT(("ftp_client_pipe_t::notifyPollEvent: fd %d != cfd %d or dfd %d!\n", event->fd, m_cfd, m_dfd));
		return EINVAL;
	}
	DPRINT(("ftp_client_pipe_t::notifyPollEvent: xerr %d\n", xerr));

	/* Update interest mask for Control */
	if (m_cfd != -1) {
		short cevents = 0;
		if (!m_ibuf.isFull())
			cevents |= POLLIN;
		if (!m_obuf.isEmpty())
			cevents |= POLLOUT;
		DPRINT(("ftp_client_pipe_t::notifyPollEvent: adding m_cfd %d, events %x\n", m_cfd, cevents));
		err = m_poller->setMask(m_cfd, cevents);
		if (err) {
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent: setMask failed\n"));
			exit(1);
		}
	}

	/* Update interest mask for Data */
	if (m_dfd != -1) {
		short devents = 0;
		/* If we're sleeping, don't ask for events if we already have them */
		if ((m_state == GETTING) || ((m_state == SLEEPING) && !m_dfd_events))
			devents = POLLIN;
		else if (m_state == PUTTING)
			devents = POLLOUT;
		DPRINT(("ftp_client_pipe_t::notifyPollEvent: adding m_dfd %d, events %x\n", m_dfd, devents));
		err = m_poller->setMask(m_dfd, devents);
		if (err) {
			EDPRINT(("ftp_client_pipe_t::notifyPollEvent: setMask failed\n"));
			exit(1);
		}
	}

	/* If no error, just return. */
	if (xerr == 0)
		return 0;
	// ok to have EALREADY; that's what is returned if one channel done
	if (xerr == EALREADY)
		xerr = 0;

	/* Special jail for tail recursion.
	 * Either the data channel closed, or the server has a result for us.
	 * If both are true, notify the caller that the command is done.
	 * Return immediately thereafter -- tail recursion is more palatable
	 * than any other kind.  If this isn't ok, we'll have to
	 * queue callbacks somehow.
	 */
	char statusbuf[256];
	int status;
	status = m_proto.getStatus(statusbuf, sizeof(statusbuf));
	if (status && !STATUS_OK(status) && (m_dfd != -1)) {
		/* If command failed, close any open data connection */
		DPRINT(("ftp_client_pipe_t::notifyPollEvent:notify_app: closing dfd %d\n", m_dfd));
		m_dfd_connecting = false;
		int dfd = m_dfd;
		m_dfd = -1;
		m_poller->del(dfd);
		close(dfd);
	}
	DPRINT(("ftp_client_pipe_t::notifyPollEvent:notify_app: status %d, dfd_conn %d, dfd %d, xerr %d\n", 
		status, m_dfd_connecting, m_dfd, xerr));
	if (xerr || (status && (m_dfd == -1))) {
		/* warning: this usually calls notifyPollEvent, i.e. recursion! */
		call_ftpCmdDone(xerr, status, statusbuf);
		/* no statements after here. */
	}
	return 0;
}

/*----------------------------------------------------------------------
 Callback function.
 When the specified time has elapsed, Sked::runAll calls this method,
 which takes care of any read request posted by a call to notifyPollEvent
 while we were asleep.  Any errors that happen during this call
 are reported via ftpCommandDone().
----------------------------------------------------------------------*/
void ftp_client_pipe_t::skedCallback(clock_t now)
{
	switch (m_state) {
	case IDLE:
		if (m_cfd_connecting) {
			EDPRINT(("ftp_client_pipe_t::skedCallback: connect timeout, calling shutdown()\n"));
			shutdownAndNotify();
			return;
		}
		EDPRINT(("ftp_client_pipe_t::skedCallback: command timeout, calling shutdown(), id %d\n",
			m_datainterface->getID()));
		shutdownAndNotify();
		break;

	case GETTING:
		EDPRINT(("ftp_client_pipe_t::skedCallback: get timeout, calling shutdown()\n"));
		shutdownAndNotify();
		break;

	case SLEEPING:
		DPRINT(("ftp_client_pipe_t::skedCallback: waking up fd %d, events %x\n", m_dfd, m_dfd_events));
		GOTO_STATE("skedCallback", GETTING);
		if (m_dfd_events) {
			short events = m_dfd_events;
			m_dfd_events = 0;
			assert(!eclock_after(m_wakeup, now));
			m_wakeup = now;		/* prevent resleep just in case */
			Poller::PollEvent event;
			event.fd = m_dfd;
			event.revents = events;
			event.client = 0;
			int err = notifyPollEvent(&event);
			if (err) {
				EDPRINT(("ftp_client_pipe_t::skedCallback: errno %d waking up, calling shutdown()\n", err));
				shutdownAndNotify();
			}
		}
		break;

	default:
		assert(false);
	}
}

int ftp_client_pipe_t::kickstart(void)
{
	Poller::PollEvent event;
	event.fd = m_cfd;
	event.revents = POLLOUT|KICKSTART;
	event.client = 0;
	return notifyPollEvent(&event);
}

/*---------------------------------------------------------------------
 Log in to the server.  
 Call getStatus() periodically until it returns nonzero to get whether
 this command succeeded.
---------------------------------------------------------------------*/
int ftp_client_pipe_t::login(const char *username, const char *password)
{
	DPRINT(("ftp_client_pipe_t::login(%s, %s)\n", username, password));
	int err = m_proto.login(username, password);
	if (err) return err;
	return kickstart();
}

/*---------------------------------------------------------------------
 Log out from the server.  This triggers the QUIT command.
 Call getStatus() periodically until it returns nonzero to get whether
 this command succeeded.
---------------------------------------------------------------------*/
int ftp_client_pipe_t::quit()
{
	DPRINT(("ftp_client_pipe_t::quit()\n"));
	int err = m_proto.quit();
	if (err) return err;
	return kickstart();
}

/*---------------------------------------------------------------------
 Change directories.
 If dir is "..", the CDUP command is used instead of CD.
 Call getStatus() periodically until it returns nonzero to get whether
 this command succeeded.
---------------------------------------------------------------------*/
int ftp_client_pipe_t::cd(const char *dir)
{
	DPRINT(("ftp_client_pipe_t::cd(%s)\n", dir));
	int err = m_proto.cd(dir);
	if (err) return err;
	return kickstart();
}

/*---------------------------------------------------------------------
 Set the transfer type.
 Call getStatus() periodically until it returns nonzero to get whether
 this command succeeded, then parse the results out of the status 
 buffer.
---------------------------------------------------------------------*/
int ftp_client_pipe_t::type(const char *ttype)
{
	DPRINT(("ftp_client_pipe_t::type(%s)\n", ttype));
	int err = m_proto.type(ttype);
	if (err) return err;
	return kickstart();
}

/*---------------------------------------------------------------------
 Retrieve file size.
 Call getStatus() periodically until it returns nonzero to get whether
 this command succeeded, then parse the results out of the status 
 buffer.
---------------------------------------------------------------------*/
int ftp_client_pipe_t::size(const char *fname)
{
	DPRINT(("ftp_client_pipe_t::size(%s)\n", fname));
	int err = m_proto.size(fname);
	if (err) return err;
	return kickstart();
}

/*---------------------------------------------------------------------
 List the given directory's contents.   
 If dirname is NULL, the current directory is listed.
 If passive is true, PASV mode is used, else PORT mode is used.
 datainterface->ftpCmdDone() will be called when done.
---------------------------------------------------------------------*/
int ftp_client_pipe_t::ls(const char *dirname, bool passive)
{
	DPRINT(("ftp_client_pipe_t::ls(%s,%d)\n", dirname, passive));

	/* not implemented */
	assert(passive);
	(void) passive;

	int err = m_proto.ls(dirname, 0);
	if (err) return err;
	GOTO_STATE("ls", GETTING);
	return kickstart();
}

/*---------------------------------------------------------------------
 Retrieve the given file's contents.  
 If passive is true, PASV mode is used, else PORT mode is used.
 datainterface->ftpCmdDone() will be called when done.
---------------------------------------------------------------------*/
int ftp_client_pipe_t::get(const char *fname, bool passive)
{
	DPRINT(("ftp_client_pipe_t::get(%s,%d)\n", fname, passive));

	/* not implemented */
	assert(passive);
	(void) passive;

	int err = m_proto.get(fname, NULL);
	if (err) return err;
	GOTO_STATE("get", GETTING);
	m_bytesUnsleptFor = 0;
	m_wakeup = eclock();	/* now */

	/* Abort if no response in five seconds */
	m_sked->addClient(this, m_wakeup + (5 * eclock_hertz()));

	return kickstart();
}

/* other methods' implementations would go here, but haven't been written yet */

