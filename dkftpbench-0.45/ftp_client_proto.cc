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
 Module to implement the protocol part of the client side of the 
 File Transfer Protocol, RFC959 (ftp://ftp.isi.edu/in-notes/rfc959.txt)
 Responsible only for parsing and generating bytes on the FTP control
 connection.  Doesn't actually do any networking calls; that is left to the 
 bfb_pipe module.  This separation of duties makes writing self-test code
 easier.

 FTP is an odd bird of a protocol, as it involves both an ASCII control
 connection of the usual sort (i.e. the server listens on the well-known port
 21 for connections), plus a data connection for each file transfered.
 By default, the data connection is established in the reverse direction 
 (from port 20 on the server to the port in use on the client; the client 
 listens for the connection).  
 This scheme is not NAT-friendly, and might not work well when transferring 
 multiple files per control connection.
 Instead, most clients either use the PASV command to force the data 
 connection to be initiated from the client end, or the PORT command
 to specify that the server should connecte to a non-default port on the
 client.

 When the caller requests a file transfer or directory listing and
 doesn't specify a local port number, this code will issue PASV, and 
 getStatus() will indicate an address.
 The caller should connect to that address immediately, and begin using
 that connection for the file or directory transfer.

 Example:
  After calling the login method, calling getOutput() will return a buffer
  containing "USER username\r\n" to be sent to the server. When the result line
  comes back from the server, it should be passed to the giveInput method.
  Calling getOutput() at that point will return a buffer containing
  "PASS password\r\n" to be sent to the server, etc. 
  The success or failure of the login call is then available by calling
  getStatus().
--------------------------------------------------------------------------*/

#include "dprint.h"
#include "ftp_client_proto.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*---------------------------------------------------------------------
 Jump to the specified state.  
 2nd argument is source line number, for debugging.  See SETSTATE macro.
---------------------------------------------------------------------*/
void ftp_client_proto_t::setState(state_t newstate, int linenum)
{
	DPRINT(("ftp_client_proto_t::setState: line %d: m_state was %d, now %d\n", linenum, m_state, newstate));
	(void) linenum;

	m_state = newstate;
}
#define SETSTATE(s) setState(s, __LINE__)

/*----------------------------------------------------------------------
 save the given state on the stack, then call setState(jump_to).
 Normally called with 2nd parameter of m_state, so returnState()
 returns to calling state.
 3nd argument is source line number, for debugging.  See CALLSTATE macro.
----------------------------------------------------------------------*/
void ftp_client_proto_t::callState(state_t jump_to, state_t return_to, int linenum)
{
	if (m_stack_ptr >= ftp_client_proto_STACKLEN) {
		DPRINT(("ftp_client_proto_t::callState: stack full; calling line %d\n", linenum));
		assert(0);
		exit(1);
	}
	m_stack[m_stack_ptr++] = return_to;
	DPRINT(("ftp_client_proto_t::callState: line %d: m_state was %d, now %d\n", linenum, m_state, jump_to));
	(void) linenum;
	m_state = jump_to;
}
#define CALLSTATE(s, t) callState(s, t, __LINE__)

/*----------------------------------------------------------------------
 Pop a state off the stack and return it
----------------------------------------------------------------------*/
ftp_client_proto_t::state_t ftp_client_proto_t::popState()
{
	state_t s;
	if (m_stack_ptr <= 0) {
		DPRINT(("ftp_client_proto_t::popState: stack empty\n"));
		assert(0);
		exit(1);
	}
	s = m_stack[--m_stack_ptr];
	m_stack[m_stack_ptr] = FCPS_UNINIT;
	return s;
}

/*----------------------------------------------------------------------
 return to the state saved in callState()
----------------------------------------------------------------------*/
void ftp_client_proto_t::returnState()
{
	state_t s = popState();
	DPRINT(("ftp_client_proto_t::returnState: m_state was %d, now %d\n", m_state, s));
	m_state = s;
}

/*---------------------------------------------------------------------
 Set up the initial state of this protocol session.
---------------------------------------------------------------------*/
void ftp_client_proto_t::init()
{
	m_status = 0;
	m_state = FCPS_UNINIT;
	m_obuflen = 0;
	m_multiline = false;
	m_address_ready = false;
	m_sent_user = false;
	m_stack_ptr = 0;
	SETSTATE(FCPS_INIT);
}

/*---------------------------------------------------------------------
 Offer a null-terminated input line from the server to this session.
 Returns 0 if accepted, Unix error code otherwise.
 In particular, returns EWOULDBLOCK if not ready for the input.
---------------------------------------------------------------------*/
int ftp_client_proto_t::giveInput(const char *ibuf)
{
	/* Input lines from nbbio::readline() have CRLF stripped off already */
	assert(strchr(ibuf, '\n') == NULL);

	int status = atoi(ibuf);
	DPRINT(("ftp_client_proto_t::giveInput: state: %d oReady %d status %d, ibuf: '%s'\n", 
		m_state, isOutputReady(), status, ibuf));

	/* If we're still processing a previous multiline reply */
	if (m_multiline) {
		if ((ibuf[3] == 0x20) && (status == m_status)) {
			DPRINT(("ftp_client_proto_t::giveInput: multiline reply ends: '%s'\n", ibuf));
			m_multiline = false;
		} else {
			DPRINT(("ftp_client_proto_t::giveInput: multiline reply continues: '%s'\n", ibuf));
		}
		return 0;
	}

	/* Save status.  User won't see this unless we're done with cmd. */
	m_status = status;
	strncpy(m_response, ibuf, sizeof(m_response)-1);
	m_response[sizeof(m_response)-1] = 0;

	if (ibuf[3] == '-') {
		DPRINT(("ftp_client_proto_t::giveInput: multiline reply begins: '%s'\n", ibuf));
		m_multiline = true;
	}

	switch (m_state) {
	case FCPS_INIT:
		/* Server volunteers a hello.  */
		if ((status >= 200) && (status < 300)) {
			DPRINT(("ftp_client_proto_t::giveInput: server ok, %s\n", ibuf));
			if (m_sent_user) {
				SETSTATE(FCPS_USER);
				m_sent_user = false;
			} else {
				SETSTATE(FCPS_CONNECTED);
			}
		} else {
			EDPRINT(("ftp_client_proto_t::giveInput: server bad? '%s'\n", ibuf));
			SETSTATE(FCPS_UNINIT);	/* give up */
			return EACCES;
		}
		break;

	case FCPS_USER:
		if ((status >= 200) && (status < 300)) {
			DPRINT(("ftp_client_proto_t::giveInput: no password needed\n"));
			SETSTATE(FCPS_IDLE);
		} else if ((status >= 300) && (status < 400)) {
			/* Send the password.  Actually, this could have been
			 * queued at the same time as the connect and the USER,
			 * if we're sure we need one.  Which would be fairer?
			 */
			m_obuflen = sprintf(m_obuf, "PASS %s\r\n", m_password);
			SETSTATE(FCPS_PASS);
		} else {
			DPRINT(("ftp_client_proto_t::giveInput: USER failed, %s\n", ibuf));
			SETSTATE(FCPS_INIT);
		}
		break;

	case FCPS_PASS:
		if ((status >= 200) && (status < 300)) {
			DPRINT(("ftp_client_proto_t::giveInput::PASS ok, %s\n", ibuf));
			SETSTATE(FCPS_IDLE);
		} else {
			DPRINT(("ftp_client_proto_t::giveInput::PASS failed, %s\n", ibuf));
			SETSTATE(FCPS_INIT);
		}
		break;

	case FCPS_CWD:
		if ((status >= 200) && (status < 300)) {
			DPRINT(("ftp_client_proto_t::giveInput::CWD ok, %s\n", ibuf));
		} else {
			DPRINT(("ftp_client_proto_t::giveInput::CWD failed, %s\n", ibuf));
		}
		SETSTATE(FCPS_IDLE);
		break;

	case FCPS_TYPE:
		if ((status >= 200) && (status < 300)) {
			DPRINT(("ftp_client_proto_t::giveInput::TYPE ok, %s\n", ibuf));
		} else {
			DPRINT(("ftp_client_proto_t::giveInput::TYPE failed, %s\n", ibuf));
		}
		SETSTATE(FCPS_IDLE);
		break;

	case FCPS_SIZE:
		if ((status >= 200) && (status < 300)) {
			DPRINT(("ftp_client_proto_t::giveInput::SIZE ok, %s\n", ibuf));
		} else {
			DPRINT(("ftp_client_proto_t::giveInput::SIZE failed, %s\n", ibuf));
		}
		SETSTATE(FCPS_IDLE);
		break;

	case FCPS_PORT:
		// This is a substate.  Called from RETR, STOR, LIST, or...
		if ((status >= 200) && (status < 300)) {
			DPRINT(("ftp_client_proto_t::giveInput::PORT ok, %s\n", ibuf));
			returnState();
		} else {
			DPRINT(("ftp_client_proto_t::giveInput::PORT failed, %s\n", ibuf));
			popState();
			SETSTATE(FCPS_IDLE);
		}
		break;

	case FCPS_PASV:
		// This is a substate.  Called from RETR, STOR, LIST, or...
		if ((status >= 200) && (status < 300)) {
			// Parse the port from the response.  Possible responses:
			// 227 Entering Passive Mode. A1,A2,A3,A4,a1,a2
			// 227 Entering Passive Mode (192,48,96,9,184,102)  
			const char *p = strchr(ibuf, ',');
			if (p) {
				/* step backwards to beginning of address */
				while (isdigit(p[-1]))
					p--;
#define MAKE_INT32(a, b, c, d) (\
		(((unsigned char)(a)) << 24) + \
		(((unsigned char)(b)) << 16) + \
		(((unsigned char)(c)) << 8) + \
		(((unsigned char)(d))))
				int a1, a2, a3, a4, p1, p2;
				int n = sscanf(p,"%d,%d,%d,%d,%d,%d", &a1,&a2,&a3,&a4,&p1,&p2);
				if (n == 6) {
					DPRINT(("ftp_client_proto_t::giveInput::PASV ok, %s\n", ibuf));
					memset(&m_address, 0, sizeof(m_address));
					m_address.sin_family = AF_INET;
					m_address.sin_addr.s_addr = htonl(MAKE_INT32(a1,a2,a3,a4));
					m_address.sin_port = htons(MAKE_INT32(0,0,p1,p2));
					m_address_ready = true;
					returnState();
					/* success */
					if (m_state == FCPS_RETR) {
						DPRINT(("ftp_client_proto_t::giveInput::PASV: sending RETR\n"));
						m_obuflen = sprintf(m_obuf, "RETR %s\r\n", m_fname);
					}
					break;
				}
				/* fall thru to error case */
			}
			/* fall thru to error case */
		}
		DPRINT(("ftp_client_proto_t::giveInput::PASV failed, %s\n", ibuf));
		popState();
		SETSTATE(FCPS_IDLE);
		break;

	case FCPS_RETR:
		if ((status >= 100) && (status < 200)) {
			DPRINT(("ftp_client_proto_t::giveInput::RETR in progress\n", ibuf));
		} else if ((status >= 200) && (status < 300)) {
			DPRINT(("ftp_client_proto_t::giveInput::RETR server finished, %s\n", ibuf));
			SETSTATE(FCPS_IDLE);
		} else {
			DPRINT(("ftp_client_proto_t::giveInput::RETR failed, %s\n", ibuf));
			SETSTATE(FCPS_IDLE);
		}
		break;

	default:
		DPRINT(("ftp_client_proto_t::giveInput: Unexpected input from server: '%s'\n", ibuf));
		return EINVAL;
		break;
	}
	return 0;
}

/*---------------------------------------------------------------------
 Get the status of the last login, quit, cd, ls, or get call.
 If the operation is still in progress, returns 0.
 If the operation has succeeded, returns a value between 200 and 299.
 ASCII version of result is returned in given buffer, if buf != NULL.
---------------------------------------------------------------------*/
int ftp_client_proto_t::getStatus(char *buf, size_t buflen)
{
	if ((m_state != FCPS_IDLE) && (m_state != FCPS_INIT))
		return 0;
	if (buf) {
		strncpy(buf, m_response, buflen);
		buf[buflen-1] = 0;
	}
	return m_status;
}

/*---------------------------------------------------------------------
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
const char *ftp_client_proto_t::getOutput(int *plen)
{
	if (!isOutputReady())
		return NULL;
	DPRINT(("ftp_client_proto_t::getOutput: Sending '%s'\n", m_obuf));
	if (plen)
		*plen = m_obuflen;
	return m_obuf;
}

/*---------------------------------------------------------------------
 Advances to next line of output; then call getOutput() to get it.
 Returns 0 on success, Unix error code on error.
 In particular, returns EWOULDBLOCK if no more output is ready.
---------------------------------------------------------------------*/
int ftp_client_proto_t::advanceOutput()
{
	m_obuflen = 0;
	return 0;
}

/*---------------------------------------------------------------------
 Log in to the server.  
 Call getStatus() periodically until it returns nonzero to get whether
 this command succeeded.
---------------------------------------------------------------------*/
int ftp_client_proto_t::login(const char *username, const char *password)
{
	if ((m_state != FCPS_INIT) && (m_state != FCPS_CONNECTED)) {
		DPRINT(("ftp_client_proto_t::login: State %d not init or connected\n", m_state));
		return EISCONN;
	}
	if (m_sent_user) {
		DPRINT(("ftp_client_proto_t::login: already logged in?!\n"));
		return EISCONN;
	}
	if (isOutputReady()) {
		DPRINT(("ftp_client_proto_t::login: obuf not empty?!\n"));
		return EWOULDBLOCK;
	}
	DPRINT(("ftp_client_proto_t::login(%s,%s)\n", username, password));
	m_obuflen = sprintf(m_obuf, "USER %s\r\n", username);
	strncpy(m_password, password, sizeof(m_password));
	m_password[sizeof(m_password)-1] = 0;

	if (m_state == FCPS_CONNECTED) {
		SETSTATE(FCPS_USER);
	} else {
		// FCPS_INIT will go to FCPS_USER when it sees m_sent_user true
		m_sent_user = true;
	}
	return 0;
}

/* other methods' implementations would go here, but haven't been written yet */
/*---------------------------------------------------------------------
 Log out from the server.  This triggers the QUIT command.
 Call getStatus() periodically until it returns nonzero to get whether
 this command succeeded.
---------------------------------------------------------------------*/
int ftp_client_proto_t::quit()
{
	/* unimplemented */
	return ENOSYS;
}

/*---------------------------------------------------------------------
 Change directories.
 If dir is "..", the CDUP command is used instead of CD.
 Call getStatus() periodically until it returns nonzero to get whether
 this command succeeded.
---------------------------------------------------------------------*/
int ftp_client_proto_t::cd(const char *dir)
{
	if (m_state != FCPS_IDLE) {
		DPRINT(("ftp_client_proto_t::cd: State %d not idle\n", m_state));
		return EISCONN;
	}
	if (isOutputReady()) {
		DPRINT(("ftp_client_proto_t::cd: obuf not empty?!\n"));
		return EWOULDBLOCK;
	}

	if (strcmp("..", dir) == 0) {
		m_obuflen = sprintf(m_obuf, "CDUP\r\n");
	}  else {
		m_obuflen = sprintf(m_obuf, "CWD %s\r\n", dir );
	}
	SETSTATE(FCPS_CWD);
	return 0;
}

/**---------------------------------------------------------------------
 Set the transfer type.
 Call getStatus() periodically until it returns nonzero to get whether
 this command succeeded, then parse the results out of the status 
 buffer.
---------------------------------------------------------------------*/
int ftp_client_proto_t::type(const char *ttype)
{
	if (m_state != FCPS_IDLE) {
		DPRINT(("ftp_client_proto_t::type: State %d not idle\n", m_state));
		return EISCONN;
	}
	if (isOutputReady()) {
		DPRINT(("ftp_client_proto_t::type: obuf not empty?!\n"));
		return EWOULDBLOCK;
	}

	m_obuflen = sprintf(m_obuf, "TYPE %s\r\n", ttype );

	SETSTATE(FCPS_TYPE);
	return 0;
}

/*---------------------------------------------------------------------
 Retrieve file size.
 Call getStatus() periodically until it returns nonzero to get whether
 this command succeeded, then parse the results out of the status 
 buffer.
---------------------------------------------------------------------*/
int ftp_client_proto_t::size(const char *fname)
{
	if (m_state != FCPS_IDLE) {
		DPRINT(("ftp_client_proto_t::size: State %d not idle\n", m_state));
		return EISCONN;
	}
	if (isOutputReady()) {
		DPRINT(("ftp_client_proto_t::size: obuf not empty?!\n"));
		return EWOULDBLOCK;
	}

	m_obuflen = sprintf(m_obuf, "SIZE %s\r\n", fname );
	SETSTATE(FCPS_SIZE);
	return 0;
}

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
int ftp_client_proto_t::ls(const char *dirname, struct sockaddr_in *address)
{
	/* unimplemented */
	(void) dirname; (void) address;
	return ENOSYS;
}

/*---------------------------------------------------------------------
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

 adr must be in network byte order.
---------------------------------------------------------------------*/
int ftp_client_proto_t::get(const char *fname, struct sockaddr_in *adr)
{
	if (m_state != FCPS_IDLE) {
		DPRINT(("ftp_client_proto_t::get: State %d not idle\n", m_state));
		return EISCONN;
	}
	if (isOutputReady()) {
		DPRINT(("ftp_client_proto_t::get: obuf not empty?!\n"));
		return EWOULDBLOCK;
	}
	if (strlen(fname) >= ftp_client_proto_MAXFNAME) {
		DPRINT(("ftp_client_proto_t::get: filename too long\n"));
		return E2BIG;
	}
	strcpy(m_fname, fname);

#define GETBYTE3(x) (((x) >> 24) & 255)
#define GETBYTE2(x) (((x) >> 16) & 255)
#define GETBYTE1(x) (((x) >> 8) & 255)
#define GETBYTE0(x) ((x) & 255)

	if (adr) {
		DPRINT(("ftp_client_proto_t::get(%s,): telling server what port to connect to\n", fname));
		int addr = ntohl(adr->sin_addr.s_addr);
		short port = ntohs(adr->sin_addr.s_addr);
		m_obuflen = sprintf(m_obuf, "PORT %d,%d,%d,%d,%d,%d\r\n", 
			GETBYTE3(addr),
			GETBYTE2(addr),
			GETBYTE1(addr),
			GETBYTE0(addr),
			GETBYTE1(port),
			GETBYTE0(port));
		CALLSTATE(FCPS_PORT, FCPS_RETR);
	} else {
		DPRINT(("ftp_client_proto_t::get(%s,): requesting port from server\n", fname));
		m_obuflen = sprintf(m_obuf, "PASV\r\n");
		CALLSTATE(FCPS_PASV, FCPS_RETR);
	}
	return 0;
}


