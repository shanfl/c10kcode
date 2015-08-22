#if HAVE_F_SETSIG && HAVE_F_SETAUXFL

#include "Poller_sigfd.h"
#include "dprint.h"
#include <unistd.h>
#include <fcntl.h>

/**
 Add a file descriptor to the set we monitor. 
 Caller should already have established a handler for SIGIO.
 @param fd file descriptor to add
 @param client object to handle events for this fd.  May use same client with more than one fd.
 @param eventmask initial event mask for this fd
 */
int Poller_sigfd::add(int fd, Client *client, short eventmask) 
{
	int flags = O_ONESIGFD;
	// FIXME: want to do GETAUXFL too if any other aux flags get defined
	if (fcntl(fd, F_SETAUXFL, flags) < 0) {
		int err = errno;
		LOG_ERROR(("add: fcntl(fd %d, F_SETAUXFL, 0x%x) returns err %d\n",
				fd, flags, err));
		return err;
	}
	return Poller_sigio::add(fd, client, eventmask);
}

/// Remove a file descriptor.
int Poller_sigfd::del(int fd)
{
	int flags = 0;
	// FIXME: want to do GETAUXFL too if any other aux flags get defined
	if (fcntl(fd, F_SETAUXFL, flags) < 0) {
		int err = errno;
		LOG_ERROR(("del: fcntl(fd %d, F_SETAUXFL, 0x%x) returns err %d\n",
				fd, flags, err));
		return err;
	}
	return Poller_sigio::del(fd);
}

#endif
