/*--------------------------------------------------------------------------
 Copyright 1999,2000, Dan Kegel http://www.kegel.com/
 See the file COPYING
 (Also freely licensed to Disappearing, Inc. under a separate license
 which allows them to do absolutely anything they want with it, without
 regard to the GPL.)  

 This module is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This module is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
--------------------------------------------------------------------------*/
#if HAVE_DEVPOLL

#include "dprint.h"
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/devpoll.h>
#include "Poller_devpoll.h"

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <fcntl.h>

#ifndef POLLREMOVE
#define POLLREMOVE 0x1000
#endif

int Poller_devpoll::init()
{
	DPRINT(("init()\n"));

	m_rfds = 0;

	// Allocate things indexed by file descriptor.
	m_clivent_alloc = 16;
	m_clivents = (struct clivent *)malloc(sizeof(struct clivent) * m_clivent_alloc);
	if (!m_clivents)
		return ENOMEM;
	memset(m_clivents, 0, m_clivent_alloc * sizeof(struct clivent));

	// Allocate array of pollfds
	m_pfds_used = 0;
	m_pfds_alloc = 16;
	m_pfds = (struct pollfd *)malloc(sizeof(struct pollfd) * m_pfds_alloc);
	if (!m_pfds)
		return ENOMEM;

	// Open /dev/poll driver
	if ((m_dpfd = open("/dev/poll", O_RDWR)) < 0)
		return ENOENT;

	Poller::init();
	return 0;
}

void Poller_devpoll::shutdown()
{
	if (m_clivents) {
		close(m_dpfd);
		free(m_clivents);
		m_clivents = NULL;
		free(m_pfds);
		m_pfds = NULL;
	}
	Poller::shutdown();
}

int Poller_devpoll::add(int fd, Client *client, short eventmask)
{
	if (fd < 0) {
		LOG_ERROR(("add(fd %d): fd out of range\n", fd));
		return EINVAL;
	}
	if ((fd < m_clivent_alloc) && m_clivents[fd].client) {
		LOG_ERROR(("add(fd %d, %p,): already monitoring that fd!\n", fd, m_clivents[fd].client));
		return EINVAL;
	}
	
	int i, n;

	// Resize arrays indexed by fd if fd is beyond what we've seen.
	if (fd >= m_clivent_alloc) {
		n = m_clivent_alloc * 2;
		if (n < fd + 1)
			n = fd + 1;

		struct clivent *pcv = (struct clivent *) realloc(m_clivents, n * sizeof(struct clivent));
		if (!pcv)
			return ENOMEM;
		// Clear new elements
		for (i=m_clivent_alloc; i<n; i++)
			pcv[i].client = NULL;
		m_clivents = pcv;

		m_clivent_alloc = n;
	}
 

	// Resize things indexed by client number if we've run out of spots.
	if (m_pfds_used == m_pfds_alloc) {
		n = m_pfds_alloc * 2;

		struct pollfd *pfds = (struct pollfd *) realloc(m_pfds, n * sizeof(struct pollfd));
		if (!pfds)
			return ENOMEM;
		m_pfds = pfds;

		m_pfds_alloc = n;
	}

	// Update things indexed by file descriptor.
	m_clivents[fd].client = client;
	m_clivents[fd].events = eventmask;

	// Prepare a pollfd to write to /dev/poll
	struct pollfd tmp_pfd;

	tmp_pfd.fd = fd;
	tmp_pfd.events = eventmask;

	// Write pollfd to /dev/poll
	if (write(m_dpfd, &tmp_pfd, sizeof(struct pollfd)) != sizeof(struct pollfd)) {
		LOG_ERROR(("add(fd %d): could not write fd to dev/poll", fd));
		return EINVAL;
	}

	// Update limits.
	m_pfds_used++;

	DPRINT(("add(%d, %p, %x) m_pfds_used %d\n",
		fd, client, eventmask, m_pfds_used));
	return 0;
}

int Poller_devpoll::del(int fd)
{
	// Sanity checks
	if (fd < 0 || fd >= m_clivent_alloc) {
		LOG_ERROR(("del(%d): fd out of range\n", fd));
		return EINVAL;
	}
	if (!m_clivents[fd].client) {
		LOG_ERROR(("del(fd %d): not monitoring that fd!\n", fd));
		return EINVAL;
	}
	assert(m_pfds_used > 0);

	DPRINT(("del(%d): m_pfds_used %d on entry\n", fd, m_pfds_used));

	// Remove from set of pollfds monitored by /dev/poll
	struct pollfd tmp_pfd;

	tmp_pfd.fd = fd;
	tmp_pfd.events = POLLREMOVE;

	// Write pollfd to /dev/poll
	if (write(m_dpfd, &tmp_pfd, sizeof(struct pollfd)) != sizeof(struct pollfd)) {
		LOG_ERROR(("add(fd %d): could not write fd to dev/poll", fd));
		return EINVAL;
	}
	
	// Remove from arrays indexed by fd.
	m_clivents[fd].client = NULL;

	// Update limits
	m_pfds_used--;

	return 0;
}

int Poller_devpoll::setMask(int fd, short eventmask)
{
	// Sanity checks
	if ((fd < 0) || (fd >= m_clivent_alloc)) {
		LOG_ERROR(("setMask(fd %d): fd out of range\n", fd));
		return EINVAL;
	}
	if (!m_clivents[fd].client) {
		LOG_ERROR(("setMask(fd %d): not monitoring that fd!\n", fd));
		return EINVAL;
	}

	m_clivents[fd].events = eventmask;

	// Prepare a pollfd to write to /dev/poll
#ifdef SOLARIS 
	
	struct pollfd tmp_pfd[2];

	tmp_pfd[0].fd = fd;
	tmp_pfd[0].events = POLLREMOVE;	
	tmp_pfd[1].fd = fd;
	tmp_pfd[1].events = m_clivents[fd].events;

#else
	struct pollfd tmp_pfd[1];

	tmp_pfd[0].fd = fd;
	tmp_pfd[0].events = m_clivents[fd].events;

#endif
	// Write pollfd to /dev/poll
	if (write(m_dpfd, tmp_pfd, sizeof(tmp_pfd)) != sizeof(tmp_pfd)) {
		LOG_ERROR(("setMask(fd %d): could not write fd to dev/poll", fd));
		return EINVAL;
	}

	DPRINT(("setMask(%d, %x): new mask %x\n", fd, eventmask, tmp_pfd.events));

	return 0;
}

int Poller_devpoll::orMask(int fd, short eventmask)
{
	// Sanity checks
	if ((fd < 0) || (fd >= m_clivent_alloc)) {
		LOG_ERROR(("orMask(fd %d): fd out of range\n", fd));
		return EINVAL;
	}
	if (!m_clivents[fd].client) {
		LOG_ERROR(("orMask(fd %d): not monitoring that fd!\n", fd));
		return EINVAL;
	}

	m_clivents[fd].events |= eventmask;

	// Prepare a pollfd to write to /dev/poll
	struct pollfd tmp_pfd;

	tmp_pfd.fd = fd;
	tmp_pfd.events = m_clivents[fd].events;

	// Write pollfd to /dev/poll
	if (write(m_dpfd, &tmp_pfd, sizeof(struct pollfd)) != sizeof(struct pollfd)) {
		LOG_ERROR(("orMask(fd %d): could not write fd to dev/poll", fd));
		return EINVAL;
	}

	DPRINT(("orMask(%d, %x): new mask %x\n", fd, eventmask, tmp_pfd.events));

	return 0;
}

int Poller_devpoll::andMask(int fd, short eventmask)
{
	// Sanity checks
	if ((fd < 0) || (fd >= m_clivent_alloc)) {
		LOG_ERROR(("andMask(fd %d): fd out of range\n", fd));
		return EINVAL;
	}
	if (!m_clivents[fd].client) {
		LOG_ERROR(("andMask(fd %d): not monitoring that fd!\n", fd));
		return EINVAL;
	}

	m_clivents[fd].events &= eventmask;

	// Prepare a pollfd to write to /dev/poll
#ifdef SOLARIS 
	
	struct pollfd tmp_pfd[2];

	tmp_pfd[0].fd = fd;
	tmp_pfd[0].events = POLLREMOVE;	
	tmp_pfd[1].fd = fd;
	tmp_pfd[1].events = m_clivents[fd].events;

#else
	struct pollfd tmp_pfd[1];

	tmp_pfd[0].fd = fd;
	tmp_pfd[0].events = m_clivents[fd].events;

#endif
	// Write pollfd to /dev/poll
	if (write(m_dpfd, tmp_pfd, sizeof(tmp_pfd)) != sizeof(tmp_pfd)) {
		LOG_ERROR(("andMask(fd %d): could not write fd to dev/poll", fd));
		return EINVAL;
	}

	DPRINT(("andMask(%d, %x): new mask %x\n", fd, eventmask, tmp_pfd.events));

	return 0;
}

/**
 Sleep at most timeout_millisec waiting for an I/O readiness event
 on the file descriptors we're watching.  Fills internal array
 of readiness events.  Call getNextEvent() repeatedly to read its
 contents.
 @return 0 on success, EWOULDBLOCK if no events ready
 */
int Poller_devpoll::waitForEvents(int timeout_millisec)
{
	int err;
	struct dvpoll dopoll;

	dopoll.dp_timeout = timeout_millisec;
	dopoll.dp_nfds = m_pfds_used;
	dopoll.dp_fds = m_pfds;

	// Wait for I/O events the clients are interested in.
	m_rfds = ioctl(m_dpfd, DP_POLL, &dopoll);
	if (m_rfds == -1) {
		err = errno;
		DPRINT(("waitForEvents: poll() returned -1, errno %d\n", err));
		return err;
	}

	LOG_TRACE(("waitForEvents: got %d events\n", m_rfds));

	return m_rfds ? 0 : EWOULDBLOCK;
}

/**
 Get the next event that was found by waitForEvents.
 @return 0 on success, EWOULDBLOCK if no more events
 */
int Poller_devpoll::getNextEvent(PollEvent *e)
{
	while (m_rfds >= 1) {
		m_rfds--;
		assert(m_pfds[m_rfds].revents > 0);

		int fd = m_pfds[m_rfds].fd;
		assert((0 <= fd) && (fd < m_clivent_alloc));

		// Check to make sure that del() hasnt been called on this fd
		if (!m_clivents[fd].client)
			continue;

		e->fd = fd;
		e->revents = m_pfds[m_rfds].revents;
		e->client = m_clivents[fd].client;

		LOG_TRACE(("getNextEvent: fd %d revents %x m_rfds %d\n", e->fd, e->revents,  m_rfds));

		return 0;
	}
	return EWOULDBLOCK;
}


int Poller_devpoll::waitAndDispatchEvents(int timeout_millisec)
{
	int err;
	PollEvent event;

	err = waitForEvents(timeout_millisec);
	if (err)
		return err;

	// Pump any network traffic into the appropriate Clients
	while (m_rfds > 0) {
		err = getNextEvent(&event);
		if (err) {
			if (err != EWOULDBLOCK)
				DPRINT(("waitAndDispatchEvents: getNextEvent() returned %d\n", err));
			break;
		}
		err = event.client->notifyPollEvent(&event);
		if (err) {
			DPRINT(("waitAndDispatchEvents: %p->notifyPollEvent(fd %d) returned %d, deleting\n",
				event.client, event.fd, err));
			del(event.fd);
		}
	}

	return 0;
}

#endif
