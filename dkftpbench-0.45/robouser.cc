/*--------------------------------------------------------------------------
 Copyright 1999, Dan Kegel http://www.kegel.com/
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
 Module to simulate FTP users.
--------------------------------------------------------------------------*/
#include "dprint.h"
#include "robouser.h"
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

#define STATUS_DISCONNECTED 999

/* return whether the given status code indicates a given FTP command status */
#define STATUS_OK(s)  (((s) >= 200) && ((s) <= 299))

/***************** Static Variable Initializations ************************/
int robouser_t::s_hertz = eclock_hertz();   /* hope it doesn't change during the run */

/***************** Private Member functions *******************************/

/*----------------------------------------------------------------------
 Jump to the given state.
 Adjust counts.
----------------------------------------------------------------------*/
void robouser_t::gotoState(state_t newstate)
{
	m_platoon->countStateChange(m_user, m_state, newstate);
	m_state = newstate;
}

/*----------------------------------------------------------------------
 Pick a random file from the set we know are on the server.
 For now, a bogus implementation.
 Later on, we will probably expand m_filename as a template.
----------------------------------------------------------------------*/
void robouser_t::pick_random_file(char *fname)
{
	strcpy(fname, m_platoon->getFilename());
}

/*----------------------------------------------------------------------
 Start an already-allocated robouser on its tiny-brained way.
 usernum is the index of this object in an outer container.

 Returns 0 on success, Unix error code on failure.
----------------------------------------------------------------------*/
int robouser_t::start(int usernum)
{
	struct sockaddr_in *local_addr = 0;

	m_user = usernum;
	DPRINT(("robouser%d::start:\n", m_user));

	/* For very large tests, need to use different local IP address for
	 * each client.  Port part of this address must be zero, so
	 * operating system will assign an ephemeral port.
	 */
	if (m_platoon->getLocalAddrs())
		local_addr = m_platoon->getLocalAddrs() +
			m_user % m_platoon->getNLocalAddrs();

	m_fcp.init(this, m_platoon->getSked(), m_platoon->getMaxBytesPerSec(), m_platoon->getPoller(), local_addr);
	m_startedAt = 0;
	m_bytesFetched = 0;
	m_reads = 0;
	/* schedule first event for now */
	m_platoon->getSked()->addClient(this, eclock());

	gotoState(CONNECT);

	return 0;
}

/*--------------------------------------------------------------------------
 Shut down one user.
--------------------------------------------------------------------------*/
void robouser_t::stop()
{
	DPRINT(("robouser%d::stop: skedIndex %d\n", m_user, skedIndex));
	/* Don't do anything here - too dangerous - might recurse. */
	gotoState(STOPPED);

	/* Insert ourselves in the list of dead users */
	m_platoon->addToDeadlist(this);
	DPRINT(("robouser%d::stop: done\n", m_user));
}

/***************** Callback functions ************************************/

/*----------------------------------------------------------------------
 When the time specified by addClient() has elapsed, Sked calls this method.
----------------------------------------------------------------------*/
void robouser_t::skedCallback(clock_t now)
{ 
	ftpCmdDone(0, -1, NULL);
	(void) now; /* will use now later */ 
}

/*----------------------------------------------------------------------
 Callback function.
 Called internally by ftp_client_pipe_t::handle_io().
 The operating system has told us that our data file descriptor
 is ready for I/O.  This function performs the desired I/O.
----------------------------------------------------------------------*/
int robouser_t::handle_data_io(int fd, short revents, clock_t now)
{
	char buf[16384];

	/*---------------------------------------------------------------------
	 It's an object-oriented callback function; a torturous path
	 of interface inheritance and interface pointers arranges for
	 this method to be called (for the right object even!) when there's
	 I/O to be done on the data channel.

	 Function must issue a single read or write on the fd (as appropriate 
	 for the call that triggered the transfer) with as large a buffer as 
	 is practical.
	 If read reads zero bytes, the transfer is over.
	 When the transfer is over, this routine must return '0' without
	 closing the file.
	 If the transfer is not over, this routine must return the
	 number of bytes tranferred during this call.
	 Returns negative Unix error code if error or no data available.
	---------------------------------------------------------------------*/

	/* bogus implementation for the moment */
	(void)revents;
	assert(m_platoon->getBytesPerRead() <= (int) sizeof(buf));
	DPRINT(("robouser%d::handle_data_io: fd %d, revents %x; calling read\n", 
		m_user, fd, revents));
	int nread = read(fd, buf, m_platoon->getBytesPerRead());
	m_reads++;
	if (nread == 0) {
		DPRINT(("robouser%d::handle_data_io: fd %d, revents %x; transfer over\n", m_user, fd, revents));
		return 0;
	}
	if (nread > 0) {
		if (m_bytesFetched == 0) {
			/* Start measuring bandwidth with first packet received.
			 * This avoids dying when fetching short files due to 
			 * confusing transfer startup time with bandwidth.
			 * There should be a separate measurement of startup time.
			 */
			m_startedAt = eclock();
		}
		m_bytesFetched += nread;
		m_platoon->incBytesFetched(nread);
		m_platoon->incNReads();
		if ((int)m_bytesFetched > (10 * m_platoon->getBytesPerRead())) {
			/* Don't kill users early until ten packets have come in. */
			float bytesPerSec;
			bytesPerSec = (1.0 * m_bytesFetched * s_hertz) / (now - m_startedAt);
			DPRINT(("robouser%d::handle_data_io: fd %d, revents %x; read %d bytes, %d bytes per second\n", m_user, fd, revents, nread, (int)bytesPerSec));
			if (bytesPerSec < m_platoon->getMinBytesPerSec()) {
				DPRINT(("robouser%d::handle_data_io: Test failed early (%d bytes), too slow (%d bytes/sec)\n", m_user, m_bytesFetched, (int) bytesPerSec));
				printf("User%d: Test failed early (%d bytes), too slow (%d bytes/sec)\n", m_user, m_bytesFetched, (int) bytesPerSec);

				stop();
				return 0;
			}
		} else {
			DPRINT(("robouser%d::handle_data_io: fd %d, revents %x; read %d bytes\n", m_user, fd, revents, nread));
		}
	} else if (errno == EWOULDBLOCK) {
		/* Operating system thought there was data ready on this fd,
		 * but there wasn't.  This can happen especially when using Linux's
		 * sigio/F_SETSIG model of I/O.  It's not harmful; just ignore it.
		 */
		DPRINT(("robouser_t::handle_data_io: fd %d: EWOULDBLOCK on read\n",fd));
		return -EWOULDBLOCK;
	} else {
		/* Any other error is probably fatal to this connection. */
		int err = errno;
		DPRINT(("robouser_t::handle_data_io: read failed, errno %d\n", err));
		return -err;
	}
	m_platoon->reap();
	return nread;
}

/*----------------------------------------------------------------------
 Callback function.  When any command (cd, ls, get, put, ...) finishes,
 ftp_client_pipe_t::handle_io() calls this function called to alert us.
----------------------------------------------------------------------*/
void robouser_t::ftpCmdDone(int xerr, int status, const char *buf)
{
	int err = -1;

	(void) buf;

	DPRINT(("robouser%d::ftpCmdDone: xerr %d, status %d\n", m_user, xerr, status));
	if (xerr) {
		EDPRINT(("robouser%d::ftpCmdDone: xerr %d, aborting\n", m_user, xerr));
		stop();
		return;
	}

	/*------------------------------------------------------------------
	 If xerr is zero, the local part of the command succeeded; the
	 server's numerical response is in status.  
	 If xerr is nonzero, the local part of the command failed (perhaps
	 a data connection could not be established), and we should
	 give up on this session (by calling stop() and starting over).

	 Allowed to call ftp_client_pipe methods.
	------------------------------------------------------------------*/

	/* Loop until internal state transitions are finished.
	 * A state that wants to immediately jump to another state sets m_state
	 * and breaks out of the switch, bringing it back to the loop.
	 * A state that wants to send a command to the server and wait for it to
	 * finish will submit the command and then return to break out of the loop.
	 * This function will be called again when the server responds to the 
	 * command or the connection is lost.
	 * A state that wants to wait a certain amount of time calls 
	 * m_sked->addClient() and returns to break out of the loop.
	 */
	for (;;) {
		DPRINT(("robouser%d::ftpCmdDone: m_state %d\n", m_user, m_state));
		switch (m_state) {

		case CONNECT:
			/* not connected yet - connect him */
			err = m_fcp.connect(m_platoon->getServername(), m_platoon->getPort());
			if (err) {
				EDPRINT(("robouser%d::ftpCmdDone:CONNECT: connect returns %d\n", m_user, err));
				stop();
				return;
			}

			err = m_fcp.login(m_platoon->getUsername(), m_platoon->getPasswd());
			if (err) {
				EDPRINT(("robouser%d::ftpCmdDone:CONNECT: login returns %d\n",m_user, err));
				stop();
				return;
			}
			gotoState(CONNECTING);
			return;

		case CONNECTING:
			if (!STATUS_OK(status)) {
				EDPRINT(("robouser%d::ftpCmdDone:CONNECTING: status %d, aborting\n",m_user, status));
				stop();
				return;
			}
			DPRINT(("robouser%d::ftpCmdDone:CONNECTING: connect succeeded, jumping to state %d\n", m_user, GET));
			gotoState(START_TYPE);
			break;

		case START_TYPE: {
			DPRINT(("robouser%d::ftpCmdDone:START_TYPE: setting TYPE I\n", m_user));
			err = m_fcp.type("I");
			if (err) {
				EDPRINT(("robouser%d::ftpCmdDone:START_TYPE: type() returns error %d\n", m_user, err));
				stop();
				return;
			}
			gotoState(FINISH_TYPE);
			return;
		}

		case FINISH_TYPE: {
			// see if the command had an error
			if (xerr || !STATUS_OK(status)) {
				EDPRINT(("robouser%d::ftpCmdDone:FINISH_TYPE: xerr %d, status %d, aborting\n",m_user, xerr, status));
				stop();
				return;
			}

			gotoState(GET);	/* start fetching files */
			break;
		}

		case GET: {
			char fetchee_name[1024];
			pick_random_file(fetchee_name);
			DPRINT(("robouser%d::ftpCmdDone:GET: fetching file %s\n", m_user, fetchee_name));
			err = m_fcp.get(fetchee_name, true);	/* FIXME */
			if (err) {
				EDPRINT(("robouser%d::ftpCmdDone:GET: get() returns error %d\n", m_user, err));
				stop();
				return;
			}
			m_startedAt = eclock();	/* note: reset when first packet comes in */
			m_bytesFetched = 0;
			m_reads = 0;
			gotoState(GETTING);
			return;
		}

		case GETTING: {
			// see if the command had an error
			if (xerr || !STATUS_OK(status)) {
				EDPRINT(("robouser%d::ftpCmdDone:GETTING: xerr %d, status %d, aborting\n",m_user, xerr, status));
				stop();
				return;
			}

			clock_t now = eclock();
			clock_t total = now - m_startedAt;
			if (total > 0) {
				float bytesPerSec;
				bytesPerSec = (1.0 * m_bytesFetched * s_hertz) / total;
				// FIXME: should verify correct # of bytes fetched
				if (m_platoon->getVerbosity()) {
					printf("User%d: fetching %d bytes took %f seconds, %d bytes per second\n", m_user, m_bytesFetched, total * 1.0/s_hertz, (int)bytesPerSec);
				}
				if (bytesPerSec < m_platoon->getMinBytesPerSec()) {
					DPRINT(("robouser%d::ftpCmdDone: Test failed at end (%d bytes), too slow (%d bytes/sec)\n", m_user, m_bytesFetched, (int) bytesPerSec));
					printf("User%d: Test failed at end (%d bytes), too slow (%d bytes/sec)\n", m_user, m_bytesFetched, (int) bytesPerSec);
					stop();
					return;
				}
			} else {
				printf("User%d: fetching %d bytes took 0 seconds\n", m_user, m_bytesFetched);
			}
			gotoState(GET);	/* loop back */
			break;
		}

		case STOPPED:
			/* should never be reached */
			return;

		default:
			return;
		}
	}
	/* notreached */
	return;
}

