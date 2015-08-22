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

#ifndef ftp_client_pipe_h
#define ftp_client_pipe_h

#include "eclock.h"
#include "nbbio.h"
#include "ftp_client_proto.h"
#include "Poller.h"
#include "Sked.h"

#define ftp_client_pipe_LINELEN 500

class ftp_client_pipe_datainterface_t;

/**--------------------------------------------------------------------------
 Module to handle the networking calls for the client side of the FTP
 protocol.  Delegates the work of parsing and generating messages to
 the ftp_client_protocol module.
--------------------------------------------------------------------------*/

class ftp_client_pipe_t : public SkedClient, Poller::Client {
	enum state_t { IDLE, GETTING, SLEEPING, PUTTING };
	state_t m_state;

	/// control socket's file descriptor, or -1
	int m_cfd;				

	/// whether nonblocking connect() in progress on cfd
	bool m_cfd_connecting;	

	/// data socket's file descriptor, or -1
	int m_dfd;

	// whether nonblocking connect() in progress on dfd
	bool m_dfd_connecting;

	/// A circular buffer for writing data to m_cfd
	nbbio m_obuf;

	/// A circular buffer for reading data from m_cfd
	nbbio m_ibuf;

	/// A line buffer for use with m_ibuf.readline()
	char m_iline[ftp_client_pipe_LINELEN+1];		// +1 for NUL
	/// true if unused line ready in m_iline
	bool m_iline_full;				

	/*
	 Bandwidth usage throttling.  Roughly, after each read,
	 a dead time is set up equal to the min desired interval for
	 that many bits to be read, and the next read is not allowed
	 to happen until that dead time has passed.
	 Only reads are throttled.  Only the data channel is throttled yet.
	 At 28kbits/sec, and MTU of 700 bytes, each TU takes 5600/28
	 = 200 msec.  If eclock_hertz is 100, that's 20 ticks, which is ok.
	 At 300kbits/sec, and MTU of 1500 bytes, each TU takes 1500*8/300
	 = 40 ms, or 4 ticks, which is a little too coarse for comfort.
	 Throttling should not be used for speeds above 64kbits/sec until we
	 implement fractional tick accumulation. 
	*/

	/// How many bytes to read per eclock tick
	int m_bytesPerTick;			
	/// How many bytes to read per second
	int m_bytesPerSec;			
	/// How many bytes from last read we haven't slept for
	int m_bytesUnsleptFor;		
	/// scheduler to manage sleeping
	Sked *m_sked;				
	/// events accumulated since we fell asleep
	short m_dfd_events;			
	/// desired wake up time
	clock_t m_wakeup;			

	/** m_datainterface is what generates and consumes ftp payload data
	 * on our command; it's implemented by the app.
	 * We simply set up connections and call him to transfer each chunk.
	 */
	ftp_client_pipe_datainterface_t *m_datainterface;

	/// Kludge: avoid recursive calls to m_datainterface->ftpCmdDone()
	int m_in_ftpCmdDone;

	/**
	 Call m_datainterface->ftpCmdDone() if not already in it; avoids re-entry.
	 Ah, the joys of immediate callbacks.
	*/
	void call_ftpCmdDone(int xerr, int status, const char *statusbuf);

	/// Shutdown and notify the client that we have an error.
	void shutdownAndNotify() {
		shutdown();
		call_ftpCmdDone(EPIPE, 0, "shutdown");
	}

	/** a reference to the parent's poller */
	Poller *m_poller;

	/** 0, or a reference to the local address to use for all connections */
	struct sockaddr_in *m_local_addr;

	/** m_proto is what generates and consumes ftp control messages;
	 * it's controlled by the app (via our get(), put(), etc. wrappers).
	 * We simply ferry those messages to and from the server.
	 */
	ftp_client_proto_t m_proto;

	/**----------------------------------------------------------------------
	 Callback function.
	 When the specified time has elapsed, Sked::runAll calls this method,
	 which takes care of any read request posted by a call to handle_io
	 while we were asleep.  Any errors that happen during this call
	 are reported via ftpCommandDone().
	----------------------------------------------------------------------*/
	void skedCallback(clock_t now);

	/// Call this to get the durn thing to start outputting stuff to server
	int kickstart(void);

	/// Handle events on the data file descriptor.
	int notifyPollEvent_dfd(Poller::PollEvent *event);

	/// Handle events on the control file descriptor.
	int notifyPollEvent_cfd(Poller::PollEvent *event);

public:
	/**----------------------------------------------------------------------
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
	void init(ftp_client_pipe_datainterface_t * datainterface, Sked *sked, 
		int max_bytes_per_sec, Poller *poller, struct sockaddr_in *local_addr);

	/*----------------------------------------------------------------------
	 Initialize this object and start a connection to the given server.
	----------------------------------------------------------------------*/
	int connect(const char *hostname, int port);

	/**----------------------------------------------------------------------
	 Call this when done with the session.
	 Closes the file descriptors.
	 Returns 0 on success, else unix error code.
	----------------------------------------------------------------------*/
	int shutdown();

	/**----------------------------------------------------------------------
	 The operating system has told us that one of our file descriptors
	 is ready for I/O.  Deal with it.

	 Returns 0 on success, Unix error code on failure.
	 If this returns an error, call shutdown() to close this session.

	 This is normally called by the app after a call to poll() or 
	 sigtimedwait(), or internally by skedCallback().
	----------------------------------------------------------------------*/
	int notifyPollEvent(Poller::PollEvent *event);

/* Wrappers for protocol routines - these call the protocol 
 * routine, then kickstart the I/O
 */
	/**---------------------------------------------------------------------
	 Get the status of the last login, quit, cd, ls, or get call.
	 If the operation is still in progress, returns 0.
	 If the operation has succeeded, returns a value between 200 and 299.
	 ASCII version of result is returned in given buffer, if buf != NULL.
	---------------------------------------------------------------------*/
	int getStatus(char *buf, size_t buflen) { return m_proto.getStatus(buf, buflen); }

	/**---------------------------------------------------------------------
	 Log in to the server.  
	 datainterface->ftpCmdDone() will be called when done.
	---------------------------------------------------------------------*/
	int login(const char *username, const char *password);

	/**---------------------------------------------------------------------
	 Log out from the server.  This triggers the QUIT command.
	 datainterface->ftpCmdDone() will be called when done.
	---------------------------------------------------------------------*/
	int quit();

	/**---------------------------------------------------------------------
	 Change directories.
	 If dir is "..", the CDUP command is used instead of CD.
	 datainterface->ftpCmdDone() will be called when done.
	---------------------------------------------------------------------*/
	int cd(const char *dir);

	/**---------------------------------------------------------------------
	 Setting transfer type.
	 datainterface->ftpCmdDone() will be called when done.
	---------------------------------------------------------------------*/
	int type(const char *ttype);

	/**---------------------------------------------------------------------
	 Retrieve file size.
	 datainterface->ftpCmdDone() will be called when done; the size 
	 of the file can be parsed out of the third parameter.
	---------------------------------------------------------------------*/
	int size(const char *fname);

	/**---------------------------------------------------------------------
	 List the given directory's contents.   
	 If dirname is NULL, the current directory is listed.
	 If passive is true, PASV mode is used, else PORT mode is used.
	 datainterface->ftpCmdDone() will be called when done.
	---------------------------------------------------------------------*/
	int ls(const char *dirname, bool passive);

	/**---------------------------------------------------------------------
	 Retrieve the given file's contents.  
	 If passive is true, PASV mode is used, else PORT mode is used.
	 datainterface->ftpCmdDone() will be called when done.
	---------------------------------------------------------------------*/
	int get(const char *fname, bool passive);
};

/**----------------------------------------------------------------------
 Interface that must be implemented by user code to handle FTP data
 blocks passed to it by this module.

 @see ftp_client_pipe_t
----------------------------------------------------------------------*/
class ftp_client_pipe_datainterface_t {
public:
	/**----------------------------------------------------------------------
	 Hook for application-supplied function.
	 ftp_client_pipe::handle_io() calls this to tell the app to do
	 a chunk of the current file transfer.

	 Function must issue a single read or write on the fd (as appropriate 
	 for the call that triggered the transfer).
	 If read reads zero bytes, or a fatal Unix error, the transfer is over.
	 When the transfer is over, this routine must return '0' without
	 closing the file.
	 If the transfer is not over, this routine must return the
	 number of bytes tranferred during this call, or -1 times the Unix
	 error code caused by read().
	 The app must not call any ftp_client_pipe_t method inside this callback.
	 On success, returns the number of bytes transferred, or 0 for EOF.
	 On error, returns -1 times the Unix error code.
	 Returning -EWOULDBLOCK will not terminate the transfer.
	----------------------------------------------------------------------*/
	virtual int handle_data_io(int fd, short revents, clock_t now) = 0;

	/**----------------------------------------------------------------------
	 Hook for application-supplied function.
	 When any command (cd, ls, get, put, ...) finishes, this function is
	 called to alert the app.  

	 If xerr is zero, the local part of the command succeeded; the
	 server's numerical response is in status.  The app may call getStatus() 
	 to retrieve the text of the server's response.
	 If xerr is nonzero, the local part of the command failed (perhaps
	 a data connection could not be established), and the app should
	 give up on this session (by calling shutdown() and starting over).

	 The app may call ftp_client_pipe_t methods inside this callback.
	----------------------------------------------------------------------*/
	virtual void ftpCmdDone(int xerr, int status, const char *statusbuf) = 0;

	/// Get an integer that identifies this object.  Used in log messages.
	virtual int getID(void) { return 0; }
};
#endif
