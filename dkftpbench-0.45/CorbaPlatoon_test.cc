/* Platoon_test.cc
   tests the Platoon client.
   Usage: 
*/

#include <iostream.h>
#include <unistd.h>
#include "CorbaPlatoon.hh"

static int CorbaPlatoon_test(CorbaPlatoon_ptr p)
{
#if 0
	const char *filename = "/usenet/.message";
	const char *username = "anonymous";
	const char *password = "robouser@";
	const char *servername = "ftp.uu.net";
#else
	const char *filename= "pub/x10k.dat";
	const char *username = "ftp";
	const char *password = "me@";
	const char *servername = "192.168.123.4";
#endif

	unsigned long maxBytesPerSec = 1000;
	unsigned long minBytesPerSec = 50;
	unsigned long bytesPerRead = 1500;
	unsigned short port = 21;

	int retval = __LINE__;

	int useAllLocalInterfaces = 0;	// false

	p->init(filename, maxBytesPerSec, minBytesPerSec, bytesPerRead,
		servername, port, username, password, useAllLocalInterfaces);
	p->set_nuserTarget(10);
	p->set_verbosity(10);

	long totalBytesRead = 0;
	unsigned long numConnecting = 0;
	unsigned long numAlive = 0;
	unsigned long numDead = 0;

	// will sleep at most 10 sec
	for (int count = 0; count < 10; count++) {
		long lastTotalBytesRead;
		lastTotalBytesRead = p->getStatus(numConnecting, numAlive, numDead);
		cout << "connecting " << numConnecting << ", alive " << numAlive << ", dead " << numDead << endl;

		if (lastTotalBytesRead < totalBytesRead) {	// 
			cerr << "Total bytes read decreased, test failed. Aborting." << endl;
			retval = __LINE__;			//error return 
			break;
		} else if (numDead > 0) {
			cerr << "Somebody died. Abort! Abort!" << endl;
			retval = __LINE__;			//error return 
			break;
		} else if (numAlive == 10) {
			cout << "The Platoon has connected. Test successful." << endl;
			retval = 0;
			break;
		} else					// wait
			sleep(1);

		totalBytesRead = lastTotalBytesRead;
	}

	if (retval)
		cout << "FAIL: error at line " << retval << endl;
	return retval;
}

int main(int argc, char **argv)
{
	try {

		CORBA::ORB_var orb = CORBA::ORB_init(argc, argv, "omniORB3");

		CORBA::Object_var obj = orb->string_to_object(argv[1]);

		CorbaPlatoon_var Platoon_ref = CorbaPlatoon::_narrow(obj);
		if (CORBA::is_nil(Platoon_ref)) {
			cerr << "Platoon reference not found." << endl;
			return 1;
		}

		int returnStat = CorbaPlatoon_test(Platoon_ref);

		/* don't leave the sourcerer's apprentice fetching files forever */
		Platoon_ref->reset();

		orb->destroy();

		if (!returnStat)
			cout << "PASS" << endl;
		else
			cout << "FAIL " << returnStat << endl;

		return returnStat;
	}
	catch(CORBA::COMM_FAILURE & ex) {
		cerr << "CORBA COMM_FAILURE unable to contact the object" << endl;
	}
	catch(CORBA::SystemException &) {
		cerr << "CORBA::SystemException caught" << endl;
	}
	catch(CORBA::Exception &) {
		cerr << "CORBA::Exception caught" << endl;
	}
	catch(omniORB::fatalException & fe) {
		cerr << "Caught omniORB::fatalException:" << endl;
		cerr << "  file: " << fe.file() << endl;
		cerr << "  line: " << fe.line() << endl;
		cerr << "  mesg: " << fe.errmsg() << endl;
	}
	catch(...) {
		cerr << "Caught unknown exception." << endl;
	}

	return 0;
}
