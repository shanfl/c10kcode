#include "Platoon.h"
#include "dprint.h"

void Platoon::countStateChange(int user, robouser_t::state_t oldstate, robouser_t::state_t newstate)
{
	(void)user;
	char buf1[256];
	char buf2[256];
	int i, pos;

	for (pos=i=0; i<robouser_t::NUMSTATES; i++)
		pos += sprintf(buf1+pos, "%d ", m_nInState[i]);

	assert(oldstate >= robouser_t::UNINIT && oldstate < robouser_t::NUMSTATES);
	assert(newstate > robouser_t::UNINIT && newstate < robouser_t::NUMSTATES);

	if (oldstate != robouser_t::UNINIT)
		m_nInState[oldstate]--;

	m_nInState[newstate]++;

	for (pos=i=0; i<robouser_t::NUMSTATES; i++)
		pos += sprintf(buf2+pos, "%d ", m_nInState[i]);

	DPRINT(("countStateChange: user %d, oldstate %d, counts %s; newstate %d, counts %s\n",
		user, oldstate, buf1, newstate, buf2));

	// Defer real work until after this callback is over
	wakeUp();
}

/*----------------------------------------------------------------------
 Set up parameters common to all instances of this class,
 and initialize the container for all instances.
 sked is a pointer to the Sked object to use to schedule delayed actions,
 and whose runAll() method should be called periodically by main().

 filename is what file to fetch from the remote server (will be a template
 later)
 maxBytesPerSec sets how much bandwidth each client tries to use
 minBytesPerSec is the lowest bandwidth per client we accept (else abort)
 bytesPerRead sets how many bytes to read each time
 servername and port specify the server and port the user will connect to.
 username and passwd are the user's username and password.
 @param local_addrs 0, or array of local addresses to assign to users
 @param n_local_addrs number of elements in local_addrs
----------------------------------------------------------------------*/
void Platoon::init(Poller *poller, Sked *sked, const char *filename, 
	int maxBytesPerSec, int minBytesPerSec, int bytesPerRead, 
	const char *servername, int port, 
	const char *username, const char *passwd, 
	struct sockaddr_in *local_addrs, int n_local_addrs)
{
	int i;
	DPRINT(("Platoon::init: filename %s\n", filename));

	/* Save parameters */
	m_poller = poller;
	m_filename = filename;
	m_bytesPerRead = bytesPerRead;
	m_minBytesPerSec = minBytesPerSec;
	m_maxBytesPerSec = maxBytesPerSec;
	m_port = port;
	m_sked = sked;
	strcpy(m_passwd, passwd);
	strcpy(m_servername, servername);
	strcpy(m_username, username);
	m_verbosity = 0;
	m_local_addrs = local_addrs;
	m_n_local_addrs = n_local_addrs;
	m_lastConnectingState = robouser_t::CONNECTING;

	/* Initialize other members */
	for (i=0; i<Platoon_MAXUSERS; i++)
		m_users[i] = NULL;
	m_nconnectingTarget = 1;
	m_nuserTarget = 0;
	memset(m_nInState, 0, sizeof(m_nInState));

	/* FIXME is this necessary? */ m_deadlist.clear();
	m_bytesFetched = 0;
	m_nreads = 0;
}


/**----------------------------------------------------------------------
 Stops any activity.  After this call, you may call init again.
----------------------------------------------------------------------*/
void Platoon::reset()
{
	int i;

	reap();

	for (i=0; i<Platoon_MAXUSERS; i++) {
		robouser_t *r = m_users[i];
		if (r) {
			m_sked->delClient(r);	/* just in case some event is pending */
			r->shutDown();
			m_users[r->getUser()] = NULL;
			delete r;
			DPRINT(("Platoon::reset: done deleting r\n"));
		}
	}
	m_sked->delClient(this);
	assert(m_sked->empty());
}

/// Return the sum of m_nInState[first ... last]
int Platoon::sumInState(robouser_t::state_t first, robouser_t::state_t last)
{
	int i, num;
	for (num=0, i=first; i<=last; i++)
		num += m_nInState[i];
	return num;
}

/*----------------------------------------------------------------------
 Start another one of the robousers.
 getStatus() should be called periodically to check on their status.
 It's a good idea to wait until one connects before starting the next.
----------------------------------------------------------------------*/
int Platoon::startUser()
{
	int i;
	int n;

	/* Find an empty slot; make a good guess at where next one is */
	i = sumInState(robouser_t::CONNECT, robouser_t::GETTING);
	for (n=0; n<Platoon_MAXUSERS; n++, i++) {
		if (i == Platoon_MAXUSERS)
			i = 0;
		if (!m_users[i])
			break;
	}
	if (n == Platoon_MAXUSERS) {
		DPRINT(("Platoon::startUser: no free slots\n"));
		return EALREADY;
	}

	m_users[i] = new robouser_t(this);
	int err = m_users[i]->start(i);
	if (err) {
		DPRINT(("Platoon::startUser: init failed, err %d\n", err));
		return err;
	}

	return 0;
}

/*----------------------------------------------------------------------
 Reap any dead users.
----------------------------------------------------------------------*/
void Platoon::reap()
{
	/* Reap all dead (= stopped) users */
	while (! m_deadlist.empty()) {
		robouser_t *r = m_deadlist.front();
		m_deadlist.pop_front();
		DPRINT(("robouser%d::reap: RIP\n", r->getUser()));
		m_sked->delClient(r);	/* just in case some event is pending */
		r->shutDown();
		m_users[r->getUser()] = NULL;
		delete r;
		DPRINT(("Platoon::reap: done deleting r\n"));
	}
}

/*----------------------------------------------------------------------
 Call this periodically to check on status of the clients.
 *nconnecting = # of robousers still trying to connect
 *nalive      = # of robousers who connected ok and are in good shape
 *ndead       = # of robousers who have failed, and are out of action

 Returns total number of bytes transferred so far.
----------------------------------------------------------------------*/
long Platoon::getStatus(int *nconnecting, int *nalive, int *ndead)
{
	reap();
	if (nconnecting) *nconnecting = sumInState(robouser_t::CONNECT, m_lastConnectingState);
	if (nalive) *nalive = sumInState(robouser_t::CONNECT, robouser_t::GETTING);
	if (ndead) *ndead = sumInState(robouser_t::STOPPED, robouser_t::STOPPED);

	return m_bytesFetched;
}

/***************** Callback functions ************************************/

/*----------------------------------------------------------------------
 When the time specified by addClient() has elapsed, Sked calls this method.
----------------------------------------------------------------------*/
void Platoon::skedCallback(clock_t now)
{ 
	(void)now;
	while (sumInState(robouser_t::STOPPED, robouser_t::STOPPED) == 0
		&& sumInState(robouser_t::CONNECT, robouser_t::GETTING) < m_nuserTarget 
	    && sumInState(robouser_t::CONNECT, m_lastConnectingState) < m_nconnectingTarget) {
		// start another connection process
		DPRINT(("Platoon scb: connecting: %d/%d, alive: %d/%d, now %ld\n",
				sumInState(robouser_t::CONNECT, m_lastConnectingState), m_nconnectingTarget, 
				sumInState(robouser_t::CONNECT, robouser_t::GETTING), m_nuserTarget, now));
		int err = startUser();
		if (err) {
			DPRINT(("Platoon::skedCallback: Error %d while trying to start a new user.\n", err));
			return;
		}
	}
}
