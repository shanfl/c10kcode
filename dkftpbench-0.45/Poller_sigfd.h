#ifndef Poller_sigfd_h
#define Poller_sigfd_h

#if HAVE_F_SETSIG && HAVE_F_SETAUXFL

#include "Poller_sigio.h"

/// Implementation of Poller for Vitaly Luban's sig-per-fd (O_ONESIGFD) patch.
class Poller_sigfd : public Poller_sigio {
public:
	/**
	 Add a file descriptor to the set we monitor. 
	 Caller should already have established a handler for SIGIO.
	 @param fd file descriptor to add
	 @param client object to handle events for this fd.  May use same client with more than one fd.
	 @param eventmask initial event mask for this fd
	 */
	virtual int add(int fd, Client *client, short eventmask);

	/// Remove a file descriptor.
	virtual int del(int fd);
};

#endif
#endif
