/*--------------------------------------------------------------------------
 FTP client server :-)
 A single program can control one or more of these via CORBA to implement
 a distributed load generator.
--------------------------------------------------------------------------*/
#include "CHECK.h"
#include "dprint.h"
#include "getifaddrs.h"
#include "Platoon.h"
#include "CorbaPlatoon_srv.hh"

/* FIXME: orbit-specific */
#include "ORBitservices/CosNaming.hh"

#include "Poller_devpoll.h"
#include "Poller.h"
#include "Poller_kqueue.h"
#include "Poller_poll.h"
#include "Poller_select.h"
#include "Poller_sigfd.h"
#include "Poller_sigio.h"
#include "robouser.h"
#include "Sked.h"

#include <pthread.h>
#include <stdlib.h>
#include <arpa/inet.h>

/// handle fatal error; should do better than this...
static void die(int status, const char *error)
{
	cerr << error << "exit status: " << status << endl;
	exit(status);
}

/// What kind of Poller to use
char g_arg_selector = 'p';

/**--------------------------------------------------------------------------
 Retrieve an array of local IP addresses from the operating system.
 Caller may free the array using cfree().
 @param local_addrs - where to put array of addresses.
 @return number of addresses in array
--------------------------------------------------------------------------*/
static int getLocalAddrs(struct sockaddr_in **local_addrs)
{
	struct ifaddrs *addrs;
	struct ifaddrs *p;

    /* ask the OS for the addresses */
	getifaddrs(&addrs);

	/* Allocate an array to hold 'em */
	int i = 0;
	for (p = addrs; p; p = p->ifa_next) {
		if (!p->ifa_addr)
			continue;
		i++;
	}
	*local_addrs = (struct sockaddr_in *) calloc(i, sizeof(struct sockaddr_in));

	/* Copy any we like into the array */
	i = 0;
	in_addr_t localhost = inet_addr("127.0.0.1");
	for (p = addrs; p; p = p->ifa_next) {
		if (!p->ifa_addr)
			continue;
		if (((struct sockaddr_in *) p->ifa_addr)->sin_addr.s_addr == localhost)
			continue;
		(*local_addrs)[i].sin_family = AF_INET;
		(*local_addrs)[i].sin_port = 0;	/* ephemeral */
		(*local_addrs)[i].sin_addr = ((struct sockaddr_in *) p->ifa_addr)->sin_addr;
		i++;
	}

	return i;
}


/**--------------------------------------------------------------------------
 CORBA wrapper around existing C++ class "Platoon".
--------------------------------------------------------------------------*/
class CorbaPlatoon_impl : public POA_CorbaPlatoon {

  public:

    CorbaPlatoon_impl();

    virtual void init(
		const char* filename,
		CORBA::ULong maxBytesPerSec,
		CORBA::ULong minBytesPerSec,
		CORBA::ULong bytesPerRead,
		const char* servername,
		CORBA::UShort port,
		const char* username,
		const char* passwd,
		CORBA::Boolean useAllLocalInterfaces) throw ();

    virtual void reset() throw ();

    virtual CORBA::Long getStatus(CORBA::ULong& nconnecting,
				  CORBA::ULong& nalive,
				  CORBA::ULong& ndead) throw ();

	virtual void set_verbosity(CORBA::Short verbosity) throw ()
	{
		lock();
		m_platoon.setVerbosity((int)verbosity);
		unlock();
	}

	virtual void set_nuserTarget(CORBA::ULong utarget) throw ()
	{
		lock();
		m_platoon.set_nuserTarget((int)utarget);
		unlock();
	}

	virtual void set_nconnectingTarget(CORBA::ULong ctarget) throw ()
	{
		lock();
		m_platoon.set_nconnectingTarget((int)ctarget);
		unlock();
	}

	virtual void set_lastConnectingState(CorbaPlatoon::sessionState_t lstate) throw ()
	{
		robouser_t::state_t rstate = robouser_t::UNINIT;
		switch (lstate)	{
		case CorbaPlatoon::CONNECT_START: rstate = robouser_t::CONNECTING; break;
		case CorbaPlatoon::CONNECT_FINISH: rstate = robouser_t::CONNECT; break;
		}
		lock();
		m_platoon.set_lastConnectingState(rstate);
		unlock();
	}


	/// Call this from main(); it's the body of the Platoon event loop
    void perform_work();

    ~CorbaPlatoon_impl() throw ();

  private:

    Platoon m_platoon;
    Poller * m_poller;
    Sked m_sked;
    pthread_mutex_t m_mutex;
	int m_tix_per_second;

	/// Local copy of filename (Platoon expects a stable pointer)
	CORBA::String_var m_filename;
	/// Local copy of servername (Platoon expects a stable pointer)
	CORBA::String_var m_servername;
	/// Local copy of username (Platoon expects a stable pointer)
	CORBA::String_var m_username;
	/// Local copy of password (Platoon expects a stable pointer)
	CORBA::String_var m_passwd;

    void lock();
    void unlock();
};

// Public Method Implementations:

CorbaPlatoon_impl::CorbaPlatoon_impl()
{
	pthread_mutex_init(&m_mutex, NULL);

	if (m_sked.init()) {
		die(1, "Can't init scheduler.\n");
	}
	m_tix_per_second = eclock_hertz();

	m_poller = NULL;
}

void CorbaPlatoon_impl::init(const char *filename,
	CORBA::ULong maxBytesPerSec,
	CORBA::ULong minBytesPerSec,
	CORBA::ULong bytesPerRead,
	const char *servername,
	CORBA::UShort port,
	const char *username, 
	const char *passwd,
	CORBA::Boolean useAllLocalInterfaces) throw ()
{
	struct sockaddr_in *local_addrs = 0;
	int n_local_addrs = 0;

	if (useAllLocalInterfaces)
		n_local_addrs = getLocalAddrs(&local_addrs);

	lock();
	switch (g_arg_selector) {
	case 'p':
		DPRINT(("Using poll()\n"));
		m_poller = new Poller_poll();
		break;

	case 's':
		DPRINT(("Using select()\n"));
		m_poller = new Poller_select();
		break;

#if HAVE_DEVPOLL
	case 'd':
		DPRINT(("Using Solaris /dev/poll\n"));
		m_poller = new Poller_devpoll();
		break;
#endif

#if HAVE_KQUEUE
	case 'k':
		DPRINT(("Using BSD kqueue()\n"));
		m_poller = new Poller_kqueue();
		break;
#endif

#if HAVE_F_SETSIG
	case 'r':
		DPRINT(("Using Linux rtsignals / F_SETSIG / O_ASYNC\n"));
		m_poller = new Poller_sigio();
		break;
#endif

#if HAVE_F_SETAUXFL
	case 'f':
		DPRINT(("Using Linux rtsignals / O_SIGPERFD\n"));
		poller = new Poller_sigfd();
		break;
#endif

	default:
		printf("Selector %c unsupported on this platform.\n", g_arg_selector);
		exit(1);
	}

	// Initialize the poller
	int err = m_poller->init();
	CHECK(err, 0);

#ifdef SIGRTMIN

	/* Tell it which signal number to use.  (Only need to do this for case 'r',
	 * really.) */
	err = poller->setSignum(SIGRTMIN);
	CHECK(err, 0);

#endif

	m_filename = filename;
	m_servername = servername;
	m_username = username;
	m_passwd = passwd;
	m_platoon.init(m_poller, &m_sked, m_filename, 
		(int)maxBytesPerSec, (int)minBytesPerSec,
		(int)bytesPerRead, m_servername, (int)port, m_username, m_passwd, 
		local_addrs, n_local_addrs);
	m_platoon.setVerbosity(3);
	unlock();
}

void CorbaPlatoon_impl::reset() throw()
{
	lock();
	m_platoon.reset();
	m_poller->shutdown();
	delete m_poller;
	m_poller = NULL;
	unlock();
}

CORBA::Long CorbaPlatoon_impl::getStatus(CORBA::ULong & nconnecting,
	CORBA::ULong & nalive, CORBA::ULong & ndead) throw()
{
	lock();
	int nc, na, nd;
	long returnStatus = m_platoon.getStatus(&nc, &na, &nd);
	unlock();
	nconnecting = nc;
	nalive = na;
	ndead = nd;
	return returnStatus;
}

void CorbaPlatoon_impl::perform_work()
{
	lock();
	if (!m_poller) {
		unlock();
		sleep(1);
		return;
	}

	clock_t now = eclock();

	/* Let the scheduler run the robots that need it */
	m_sked.runAll(now);

	/* Service any clients that might be ready. */
	for (;;) {
		Poller::PollEvent event;
		int err;
		err = m_poller->getNextEvent(&event);
		if (err == EWOULDBLOCK)
			break;
		CHECK(0, err);

		err = event.client->notifyPollEvent(&event);
		CHECK(0, err);
		m_platoon.reap();
	}

	/* Call poller->waitForEvents() to find out what handles are ready 
	 * for read or write.
	 * Don't sleep too long here, or you'll interfere with robouser's
	 * bandwidth throttling.
	 */
	now = eclock();
	clock_t tixUntilNextEvent = m_sked.nextTime(now + m_tix_per_second) - now;
	int msUntilNextEvent = (tixUntilNextEvent * 1000) / m_tix_per_second + 1;
	int err = m_poller->waitForEvents(msUntilNextEvent);
	if (err && (err != EINTR) && (err != EWOULDBLOCK)) {
		errno = err;
		die(err, "poll");
	}
	unlock();
}

CorbaPlatoon_impl::~CorbaPlatoon_impl() throw()
{
	lock();

	m_poller->shutdown();
	delete m_poller;

	unlock();
	int status = pthread_mutex_destroy(&m_mutex);
	if (status != 0)
		die(status, "Failed to properly destroy mutex");
}

// Private Method Implementations
void CorbaPlatoon_impl::lock()
{
	int status = pthread_mutex_lock(&m_mutex);
	if (status != 0)
		die(status, "Unable to obtain lock on mutex");
}

void CorbaPlatoon_impl::unlock()
{
	int status = pthread_mutex_unlock(&m_mutex);
	if (status != 0)
		die(status, "Unable to unlock mutex.");
}


int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	DPRINT_ENABLE(1);
	try {

		CORBA::ORB_var orb = CORBA::ORB_init(argc, argv, "omniORB3");

		CORBA::Object_var obj = orb->resolve_initial_references("RootPOA");
		PortableServer::POA_var poa = PortableServer::POA::_narrow(obj);

		CorbaPlatoon_impl * myPlatoon = new CorbaPlatoon_impl ();

		PortableServer::ObjectId_var myPlatoonId = poa->activate_object(myPlatoon);

		// Obtain a reference to the object, and print it out as a
		// stringified IOR.
		CORBA::Object_var myobj = myPlatoon->_this();
		CORBA::String_var sior(orb->object_to_string(myobj));
		cerr << "'" << (const char*)sior << "'" << endl;

		// Get the root naming context 
		CORBA::Object_var nsov=orb->resolve_initial_references("NameService"); 
		CosNaming::NamingContext_var nsv = CosNaming::NamingContext::_narrow(nsov); 
		if (!CORBA::is_nil(nsv)) {
			// Bind the object reference in naming 
			CosNaming::Name name;
			name.length(1);
			char hostname[256];
			gethostname(hostname, sizeof(hostname));
			name[0].id = CORBA::string_dup(hostname);
			name[0].kind = CORBA::string_dup("CorbaPlatoon");
			nsv->rebind(name, myobj); 
			printf("Binding to name service as %s\n", hostname);
		} else {
			printf("No name service to bind to.\n");
		}

		myPlatoon->_remove_ref();

		PortableServer::POAManager_var pman = poa->the_POAManager();
		pman->activate();

		for (;;) {
			if (orb->work_pending())
				orb->perform_work();
			myPlatoon->perform_work();
		}
#if 0
		/* doesn't compile with orbitcpp */
		orb->destroy();
#endif
	} catch(CORBA::SystemException &) {
		cerr << "Caught CORBA::SystemException. " << endl;
	} catch(CORBA::Exception &) {
		cerr << "Caught CORBA::Exception." << endl;
#ifdef OMNIORB
	} catch(omniORB::fatalException & fe) {
		cerr << "Caught omniORB::fatalException:" << endl;
		cerr << " file: " << fe.file() << endl;
		cerr << " line: " << fe.line() << endl;
		cerr << " mesg: " << fe.errmsg() << endl;
#endif
	} catch(...) {
		cerr << "Caught unknown exception." << endl;
	}

	return 0;

}
