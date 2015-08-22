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

/* FTP Client Protocol Handler */
#ifndef ftp_client_proto_h
#define ftp_client_proto_h

/* pick up size_t */
#include <stddef.h>

/* pick up sockaddr_in */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define ftp_client_proto_LINELEN 512
#define ftp_client_proto_MAXPASSWORD 32
#define ftp_client_proto_MAXFNAME 256
#define ftp_client_proto_STACKLEN 3

/**--------------------------------------------------------------------------
 Module to implement the protocol part of the client side of the 
 File Transfer Protocol, RFC 959.
 Responsible only for parsing and generating bytes on the FTP control
 connection.  Doesn't actually do any networking calls; that is left to the 
 bfb_pipe module.  This separation of duties makes writing self-test code
 easier.

 When the caller requests a file transfer or directory listing and
 doesn't specify a local port number, this code will issue PASV, and 
 getStatus() will indicate an address.
 The caller should connect to that address immediately, and begin using
 that connection for the file or directory transfer.

 Example:
  After calling the login method, calling getOutput() will return a buffer
  containing 'USER username\r\n' to be sent to the server. When the result line
  comes back from the server, it should be passed to the giveInput method.
  Calling getOutput() at that point will return a buffer containing
  'PASS password\r\n' to be sent to the server, etc. 
  The success or failure of the login call is then available by calling
  getStatus().
--------------------------------------------------------------------------*/

class ftp_client_proto_t {
	// RX state variables
	// At any point in time, we're either UNINIT (haven't started),
	// CONNECTED (waiting for user to log in),
	// IDLE (waiting from user to do something), or we're waiting
	// for the server's response to something we've sent.
	/// m_state says what our parser is awaiting a response to.
	enum state_t {
		FCPS_UNINIT,	// waiting for user to call init()
		FCPS_INIT,		// waiting for response to connection
		FCPS_CONNECTED,	// got server hello; waiting for user to call login()
		FCPS_USER,		// waiting for response to USER
		FCPS_PASS,		// waiting for response to PASS
		FCPS_IDLE,		// central logged-in state, waiting for user
		FCPS_CWD,		// waiting for response to CWD or CDUP
		FCPS_TYPE,		// waiting for response to TYPE
		FCPS_SIZE,		// waiting for response to SIZE
		FCPS_PORT,		// waiting for response to PORT
		FCPS_PASV,		// waiting for response to PASV
		FCPS_RETR,		// waiting for response to RETR
		FCPS_QUIT		// waiting for response to QUIT
	};
///
	state_t m_state;
///
	state_t m_stack[ftp_client_proto_STACKLEN];
///
	int m_stack_ptr;

	/// TX state variables
	bool m_sent_user;	// only place we allow sendahead is USER

	/// Current output line
	char m_obuf[ftp_client_proto_LINELEN+1];          /// +1 for NUL
	int m_obuflen;

	/// Current reply from server
	bool m_multiline;				/// keep reading 'til 2nd line with status
	int m_status;									/// numerical value of ...
	char m_response[ftp_client_proto_LINELEN+1];

	/// Info stored temporarily from methods for use during next state
	char m_password[ftp_client_proto_MAXPASSWORD];
///
	char m_fname[ftp_client_proto_MAXFNAME];

///
	bool m_address_ready;			/// when PASV result comes in, these are set
	struct sockaddr_in m_address;

	/*---------------------------------------------------------------------
	 Jump to the specified state.  
	 2nd argument is source line number, for debugging.  See SETSTATE macro.
	---------------------------------------------------------------------*/
	void setState(state_t newstate, int linenum);

	/*----------------------------------------------------------------------
	 save the given state on the stack, then call setState(jump_to).
	 Normally called with 2nd parameter of m_state, so returnState()
	 returns to calling state.
	 3nd argument is source line number, for debugging.  See CALLSTATE macro.
	----------------------------------------------------------------------*/
	void callState(state_t jump_to, state_t return_to, int linenum);
	
	/*----------------------------------------------------------------------
	 Pop a state off the stack and return it
	----------------------------------------------------------------------*/
	state_t popState();

	/*----------------------------------------------------------------------
	 return to the state saved in callState()
	----------------------------------------------------------------------*/
	void returnState();
		
public:

	/* Network control channel interface */
	/*---------------------------------------------------------------------
	 Returns whether giveInput would accept a line of input.
	---------------------------------------------------------------------*/
	bool isInputReady() { 
		// This module sometimes sends several commands ahead of
		// the server's responses, so it's possible to have both
		// isInputReady() and isOutputReady()
		// The only useful state in which we aren't waiting for server
		/// input is the idle state.
		return (m_state != FCPS_IDLE);
	}

	/**---------------------------------------------------------------------
	 Returns whether getOutput would have a line of output for us.
	---------------------------------------------------------------------*/
	bool isOutputReady() { return (m_obuflen != 0); }

	/**---------------------------------------------------------------------
	 If a GET, PUT, or LS is in progress, and the server needs to tell us 
	 the port number to connect to, this will return TRUE and give us the 
	 address and port.

	 i.e. if the server sends status 227, the result of the PASV command is 
	 parsed into the given struct sockaddr_in *, and the caller should 
	 arrange to transfer data.
	---------------------------------------------------------------------*/
	bool isPortReady(struct sockaddr_in *address) { if (!m_address_ready) return false; m_address_ready = false; *address = m_address; return true; }

	/**---------------------------------------------------------------------
	 Returns whether QUIT command has finished.
	 This should trigger a disconnect.
	---------------------------------------------------------------------*/
	bool isQuit() { return (m_state == FCPS_QUIT); }

	/**---------------------------------------------------------------------
	 Offer a null-terminated input line from the server to this session.
	 Returns 0 if accepted, Unix error code otherwise.
	 In particular, returns EWOULDBLOCK if not ready for the input.
	---------------------------------------------------------------------*/
	int giveInput(const char *ibuf);

	/**---------------------------------------------------------------------
	 Get the current null-terminated output line to the server
	 and its length in bytes.
	 The line is terminated by CR LF and a NUL; the length includes
	 the CR LF but not the NUL.
	 You may call it as many times as you like; it will return the
	 same data.  This lets you retry a send.  
	 After the send succeeds, call advanceOutput() to advance to the next 
	 line of output.
	 Returns NULL if no output is ready yet.
	---------------------------------------------------------------------*/
	const char *getOutput(int *plen = 0);

	/**---------------------------------------------------------------------
	 Advances to next line of output; then call getOutput() to get it.
	 Returns 0 on success, Unix error code on error.
	 In particular, returns EWOULDBLOCK if no more output is ready.
	---------------------------------------------------------------------*/
	int advanceOutput();

	/* user-callable interface */

	/**---------------------------------------------------------------------
	 Set up the initial state of this protocol session.
	---------------------------------------------------------------------*/
	void init();

	/**---------------------------------------------------------------------
	 Get the status of the last login, quit, cd, ls, or get call.
	 If the operation is still in progress, returns 0.
	 If the operation has succeeded, returns a value between 200 and 299.
	 ASCII version of result is returned in given buffer, if buf != NULL.
	---------------------------------------------------------------------*/
	int getStatus(char *buf, size_t buflen);

	/**---------------------------------------------------------------------
	 Log in to the server.  
	 Call getStatus() periodically until it returns nonzero to get whether
	 this command succeeded.
	---------------------------------------------------------------------*/
	int login(const char *username, const char *password);

	/**---------------------------------------------------------------------
	 Log out from the server.  This triggers the QUIT command.
	 Call getStatus() periodically until it returns nonzero to get whether
	 this command succeeded.
	---------------------------------------------------------------------*/
	int quit();

	/**---------------------------------------------------------------------
	 Change directories.
	 If dir is "..", the CDUP command is used instead of CD.
	 Call getStatus() periodically until it returns nonzero to get whether
	 this command succeeded.
	---------------------------------------------------------------------*/
	int cd(const char *dir);

	/**---------------------------------------------------------------------
	 Retrieve file size.
	 Call getStatus() periodically until it returns nonzero to get whether
	 this command succeeded, then parse the results out of the status 
	 buffer.
	---------------------------------------------------------------------*/
	int size(const char *fname);

	/**---------------------------------------------------------------------
	 Set the transfer type.
	 Call getStatus() periodically until it returns nonzero to get whether
	 this command succeeded, then parse the results out of the status 
	 buffer.
	---------------------------------------------------------------------*/
	int type(const char *ttype);

	/**---------------------------------------------------------------------
	 List the given directory's contents.   dirname may be NULL.
	 Result comes back on a data channel.
	 Call getStatus() periodically until it returns nonzero to get whether
	 this command succeeded.  

	 If address is 0, PASV mode is used, and isPortReady() should be called
	 periodically until it returns true and indicates where to connect to 
	 to retrieve the directory listing.
	 After retrieving the data from that address, continue calling
	 getStatus() to get the success status message.

	 If address is nonzero, PORT mode is used; it is assumed that a
	 port is already listening on the given address for a connection.
	---------------------------------------------------------------------*/
	int ls(const char *dirname, struct sockaddr_in *address);

	/**---------------------------------------------------------------------
	 Retrieve the given file's contents.  Result comes back on a data
	 channel.
	 Call getStatus() periodically until it returns nonzero to get whether
	 this command succeeded.  

	 If address is NULL, PASV mode is used, and isPortReady() should be called
	 periodically until it returns true and indicates where to connect to 
	 to retrieve the file.
	 After retrieving the data from that address, continue calling
	 getStatus() to get the success status message.

	 If address is not NULL, PORT mode is used; it is assumed that a
	 port is already listening on the given address for a connection.
	---------------------------------------------------------------------*/
	int get(const char *fname, struct sockaddr_in *address);

};

#endif
