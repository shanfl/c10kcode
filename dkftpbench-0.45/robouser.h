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

#ifndef robouser_h
#define robouser_h

#include "eclock.h"					/* for clock_t - should be eclock_t */
#include "ftp_client_pipe.h"
#include "Sked.h"
#include <stddef.h>
#include <stdio.h>

class Platoon;
/**--------------------------------------------------------------------------
 Module to simulate FTP users.
--------------------------------------------------------------------------*/
class robouser_t : public ftp_client_pipe_datainterface_t, public SkedClient {

public:
	/** stages in a robot's life cycle.
	 * Order is extremely important; see Platoon::sumInState() and
	 * Platoon::startUser(), which assume any state between CONNECT and
	 * GETTING (numerically) is alive...
	 */
	enum state_t {UNINIT,CONNECT,CONNECTING,START_TYPE,FINISH_TYPE,GET,GETTING,
		STOPPED};
	/// number of possible values of state_t
	static const int NUMSTATES = (((int) STOPPED)+1);

private:
/* Private data members - all start with m_ */
	/// this struct's index in outer container
	int m_user;					

	/** The Platoon to which this robot belongs */
	Platoon *m_platoon;

	/** where the robot is in its life cycle */
	state_t m_state;

/* variables to support statistics gathering */
	/** when current fetch started */
	clock_t m_startedAt;			
	/** so far from current file */
	size_t m_bytesFetched;			
	/** number of 'packets' read so far */
	int m_reads;					

	/** The FTP connection to the server. */
	ftp_client_pipe_t m_fcp;


	/// cached value of eclock_hertz()
	static int s_hertz;
/* Public instance methods (needed by Platoon) */
public:
	/**----------------------------------------------------------------------
	 Constructor is public, but only Platoon::startUser should call it.
	----------------------------------------------------------------------*/
	robouser_t(Platoon *platoon) { m_state = UNINIT; m_platoon = platoon; }
	
	/**----------------------------------------------------------------------
	 Start an already-allocated robouser on its tiny-brained way.
	 usernum is the index of this object in an outer container.

	 Returns 0 on success, Unix error code on failure.
	----------------------------------------------------------------------*/
	int start(int usernum);

	/**----------------------------------------------------------------------
	 Shut down the fcp connection.
	----------------------------------------------------------------------*/
	void shutDown() { m_fcp.shutdown(); }

	int getUser() { return m_user; }

/* Private instance methods */
private:
	/**----------------------------------------------------------------------
	 Pick a random file from the set we know are on the server.
	 For now, a bogus implementation.
	 Later on, we will probably expand m_filename as a template.
	----------------------------------------------------------------------*/
	void pick_random_file(char *fname);

	/**----------------------------------------------------------------------
	 Jump to the given state.
	 Adjust counts.
	----------------------------------------------------------------------*/
	void gotoState(state_t newstate);

	/**----------------------------------------------------------------------
	 Shut down one user.  Opposite of start().
	 Call after QUIT succeeds, or connection-fatal error detected.
	----------------------------------------------------------------------*/
	void stop();

/* Callback functions - called by Sked and ftp_client_proto only */

    /**----------------------------------------------------------------------
     When the time specified by addClient() has elapsed, Sked calls this method.
    ----------------------------------------------------------------------*/
    void skedCallback(clock_t now);

	/**----------------------------------------------------------------------
	 Callback function.
	 Called internally by ftp_client_pipe_t::notifyPollEvent().
	 The operating system has told us that our data file descriptor
	 is ready for I/O.  This function performs the desired I/O.
	----------------------------------------------------------------------*/
	int handle_data_io(int fd, short revents, clock_t now);

	/**----------------------------------------------------------------------
	 Callback function.  When any command (cd, ls, get, put, ...) finishes,
	 ftp_client_pipe_t::notifyPollEvent() calls this function called to alert us.
	----------------------------------------------------------------------*/
	void ftpCmdDone(int xerr, int status, const char *buf);

	/// Get an integer that identifies this object.  Used in log messages.
	int getID(void) { return m_user; }
};

#endif
