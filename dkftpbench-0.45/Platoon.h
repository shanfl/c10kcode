#ifndef Platoon_H
#define Platoon_H
#include "robouser.h"
#include <list.h>

/* FIXME: 'reasonable' upper bound on number of users CPU can handle */
#define Platoon_MAXUSERS (1 << 16)

enum robouser_t::state_t;

/**----------------------------------------------------------------------
 Platoon of robousers.
----------------------------------------------------------------------*/
class Platoon : public SkedClient {
public:
	/**----------------------------------------------------------------------
	 Set up parameters common to all instances of this class,
	 and initialize the container for all instances.
	 sked is a pointer to the Sked object to use to schedule delayed actions,
	 and whose runAll() method should be called periodically by main().

	 filename is what file to fetch from the remote server (will be a template
	 later).
	 maxBytesPerSec sets how much bandwidth each client tries to use.
	 minBytesPerSec is the lowest bandwidth per client we accept (else abort).
	 bytesPerRead sets how many bytes to read each time.
	 servername and port specify the server and port the user will connect to.
	 username and passwd are the user's username and password.
	 @param local_addrs 0, or array of local addresses to assign to users
	 @param n_local_addrs number of elements in local_addrs
	----------------------------------------------------------------------*/
	void init(Poller *poller, Sked *sked, const char *filename,
		int maxBytesPerSec, int minBytesPerSec, int bytesPerRead, 
		const char *servername, int port, 
		const char *username, const char *passwd, 
		struct sockaddr_in *local_addrs, int n_local_addrs);

	/**----------------------------------------------------------------------
	 Stops any activity.  After this call, you may call init again.
	----------------------------------------------------------------------*/
	void reset();

	/**----------------------------------------------------------------------
	 Set desired number of simulated users.  Platoon will ramp up to this.
	----------------------------------------------------------------------*/
	void set_nuserTarget(int utarget) {
		m_nuserTarget = utarget;
		wakeUp();
	}

	/**----------------------------------------------------------------------
	 How many simulated users should try to connect at once?
	 Default is 1.  Higher values make Platoon ramp up more quickly.
	----------------------------------------------------------------------*/
	void set_nconnectingTarget(int ctarget) {
		m_nconnectingTarget = ctarget;
		wakeUp();
	}

	/**----------------------------------------------------------------------
	 What's the last state in which a robouser is considered to be connecting?
	 Default value is robouser_t::CONNECTING (after tcp connection complete).  
	 Other possible value is robouser_t::CONNECT (when tcp connection starts).
	 Lower numerical values make Platoon ramp up more quickly.
	----------------------------------------------------------------------*/
	void set_lastConnectingState(robouser_t::state_t s) {
		m_lastConnectingState = s;
		wakeUp();
	}

	/**----------------------------------------------------------------------
	 Call this periodically to check on status of the clients.
	 *nconnecting = # of robousers still trying to connect
	 *nalive      = # of robousers who connected ok and are in good shape
	 *ndead       = # of robousers who have failed, and are out of action

	 Returns total number of bytes transferred so far.
	----------------------------------------------------------------------*/
	long getStatus(int *nconnecting, int *nalive, int *ndead);

	/**----------------------------------------------------------------------
	 Reap any dead users.  Call this after each call to Sked->runAll().
	----------------------------------------------------------------------*/
	void reap();

	virtual ~Platoon() {}

// Private methods
private:
	/** Schedule a wake-up call to check if we should start a new connection. */
	void wakeUp() {
		m_sked->delClient(this);	/* just in case some event is pending */
		m_sked->addClient(this, eclock()); /* Prepare check */
	}

	/// Return the sum of m_numInState[first ... last]
	int sumInState(robouser_t::state_t first, robouser_t::state_t last);

	/**----------------------------------------------------------------------
	 Start another one of the robousers.
	 getStatus() should be called periodically to check on their status.
	----------------------------------------------------------------------*/
	int startUser();

private:

	/// How we get network events from the operating system
	Poller *m_poller;

	/// The server part of the URL to fetch
	char m_servername[128];
	/// The port number part of the URL to fetch
	int  m_port;
	/// The user name part of the URL to fetch
	char m_username[128];
	/// The password part of the URL to fetch
	char m_passwd[128];
	/// Requested Lower bound on bytes each user should read per second
	int m_minBytesPerSec;
	/// Requested Upper bound on bytes each user should read per second
	int m_maxBytesPerSec;
	/// Requested number of bytes each user should read at a time
	int m_bytesPerRead;
	/// Filename part of the URL to fetch
	const char *m_filename;

	/// whether to print stuff out
	int m_verbosity;

	/** How we schedule timeouts */
	Sked *m_sked;

	/// How we deterimine whether a session is still connecting
	robouser_t::state_t m_lastConnectingState;

	/** Desired number of sessions in process of connecting.
	    sum(m_nInState[CONNECT,CONNECTING]) aspires to reach this goal. 
	 */
	int m_nconnectingTarget;

	/** Desired number of users.
	    sum(m_nInState[CONNECT,CONNECTING,GET,GETTING]) aspires to reach this goal. 
	 */
	int m_nuserTarget;

	/// Count of users in each state
	int m_nInState[robouser_t::NUMSTATES];

	/* Each element is NULL or a nondead user */
	robouser_t *m_users[Platoon_MAXUSERS];

	/** List of dead robousers waiting to be reaped */
	list<robouser_t *> m_deadlist;

	/** Health: count of bytes fetched so far from all files */
	size_t m_bytesFetched;

	/** Health: count of reads performed so far from all files */
	int m_nreads;

	/** pool of local addresses, or NULL */
	struct sockaddr_in *m_local_addrs;

	/** size of pool */
	int m_n_local_addrs;

// Accessors for robouser use
public:
	struct sockaddr_in *getLocalAddrs() { return m_local_addrs; }
	int getNLocalAddrs() { return m_n_local_addrs; }
	Sked *getSked() { return m_sked; }
	int getMaxBytesPerSec() { return m_maxBytesPerSec; }
	Poller *getPoller() { return m_poller; }
	int getBytesPerRead() { return m_bytesPerRead; }
	const char *getFilename() { return m_filename; }
	int getMinBytesPerSec() { return m_minBytesPerSec; }
	const char *getServername() { return m_servername; }
	int getPort() { return m_port; }
	const char *getUsername() { return m_username; }
	const char *getPasswd() { return m_passwd; }
	void setVerbosity(int v) { m_verbosity = v; }
	int getVerbosity() { return m_verbosity; }

	/**----------------------------------------------------------------------
	 Update counts on state change.  For robouser use only.
	----------------------------------------------------------------------*/
	void countStateChange(int user, robouser_t::state_t oldstate, robouser_t::state_t newstate);

// Mutators
	void addToDeadlist(robouser_t *corpse) { m_deadlist.push_back(corpse); }
	void incBytesFetched(int nread) { m_bytesFetched += nread; }
	void incNReads() { m_nreads++; }

/* Callback functions - called by Sked only */

	/**----------------------------------------------------------------------
	 When the time specified by addClient() has elapsed, Sked calls this method.
	----------------------------------------------------------------------*/
	void skedCallback(clock_t now);
};
#endif
