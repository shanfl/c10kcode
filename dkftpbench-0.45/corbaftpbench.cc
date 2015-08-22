/* FTP benchmark controller */

#include "CorbaPlatoon.hh"
#include "dprint.h"
#include "eclock.h"
#include <assert.h>
#include <iostream.h>
#include <stdlib.h>
#include <unistd.h>
#include CORBA_H
#include COSNAMING_H

static void usage()
{
	printf("\
Usage: bench [-options]\n\
Options:\n\
 -hHOSTNAME host name of ftp server\n\
 -P# port number of ftp server\n\
 -n# number of users\n\
 -c# target number of simultaneous connection attempts\n\
 -t# length of run (in seconds)\n\
 -b# desired per-client bandwidth (in bytes per second)\n\
 -B# min acceptable per-client bandwidth (in bytes per second)\n\
 -uUSERNAME user name\n\
 -pPASSWORD user password\n\
 -fFILENAME file to fetch\n\
 -m# bytes per 'packet'\n\
 -v# set verbosity (0=none, 1=some, 2=lots)\n\
 -v increase verbosity (-v -v for very verbose)\n\
 -s# selector (p=poll, s=select, d=/dev/poll, k=kqueue, r=rtsig, f=sig-per-fd)\n\
 -a use all local IP interfaces\n\
");
	 exit(-1);
}

int main(int argc, char **argv)
{
	int arg_users = 0;
	int arg_nconnectingTarget = 1;
	int arg_duration = 0;
	int arg_clientBandwidth = 28800/8;
	int arg_minClientBandwidth = 0;
	int arg_useAllLocalIfs = 0;
	const char *arg_hostname = "";
	const char *arg_username = "anonymous";
	const char *arg_password = "robouser@";
	const char *arg_filename = "pub/x10k.dat";
	char arg_selector = 'p';
	short arg_portnum = 21;
	int arg_verbosity = 0;
	int arg_mtu = 1500;
	int tix_per_second = eclock_hertz();

	int old_nalive, old_ndead;
	clock_t test_end;

	DPRINT_ENABLE(false);

	/* setlinebuf(stdout) */
	setvbuf(stdout, (char *)NULL, _IOLBF, 0);

	for (int i=0; i<argc; i++) {
		if (!strncmp(argv[i], "-h", 2)) {
			arg_hostname = &argv[i][2];
		} else if (!strncmp(argv[i], "-P", 2)) {
			arg_portnum = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-n", 2)) {
			arg_users = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-c", 2)) {
			arg_nconnectingTarget = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-t", 2)) {
			arg_duration = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-b", 2)) {
			arg_clientBandwidth = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-B", 2)) {
			arg_minClientBandwidth = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-u", 2)) {
			arg_username = &argv[i][2];
		} else if (!strncmp(argv[i], "-p", 2)) {
			arg_password = &argv[i][2];
		} else if (!strncmp(argv[i], "-f", 2)) {
			arg_filename = &argv[i][2];
		} else if (!strncmp(argv[i], "-m", 2)) {
			arg_mtu = atoi(&argv[i][2]);
		} else if (!strncmp(argv[i], "-s", 2)) {
			arg_selector = argv[i][2];
		} else if (!strncmp(argv[i], "-v", 2)) {
			if (strlen(argv[i]) > 2)
				arg_verbosity = atoi(&argv[i][2]);
			else
				arg_verbosity++;
		} else if (!strcmp(argv[i], "-a")) {
			arg_useAllLocalIfs = 1;
		}
	}

	if (arg_users == 0) {
		printf("Invalid number of users.\n");
		usage();
	} else if (arg_clientBandwidth == 0) {
		printf("Invalid bandwidth.\n");
		usage();
	} else if (strlen(arg_hostname) == 0) {
		printf("Invalid host name.\n");
		usage();
	}

	if (!arg_minClientBandwidth)
		arg_minClientBandwidth = (arg_clientBandwidth * 3)/4;

	try {

		CORBA::ORB_var orb = CORBA::ORB_init(argc, argv, "omniORB3");

		CORBA::Object_var nsov=orb->resolve_initial_references("NameService"); 
		CosNaming::NamingContext_var nsv = CosNaming::NamingContext::_narrow(nsov); 
		assert(!CORBA::is_nil(nsv));

		/* Get a list of all the CorbaPlatoons */
		CosNaming::BindingIterator_var it;
		CosNaming::BindingList_var bl;
		const CORBA::ULong CHUNK = 1000;
		nsv->list(CHUNK, bl, it);	/* get first and only chunk */
		CorbaPlatoon_var *platoons = new CorbaPlatoon_var[bl->length()];

		int nPlatoons=0;
		int i;
		printf("Got %d names:\n", (int) bl->length());
		for (i=0; i < (int)bl->length(); i++) {
			/* fixme: is it possible to have more than one component? */
			if (strcmp(bl[i].binding_name[0].kind, "CorbaPlatoon"))
				continue;
			CORBA::Object_var obj = nsv->resolve(bl[i].binding_name);
			CorbaPlatoon_ptr p = CorbaPlatoon::_narrow(obj);
			if (!CORBA::is_nil(p)) {
				const char *name = bl[i].binding_name[0].id;
				printf("%s,", name);
				try {
					p->init(arg_filename, 
						arg_clientBandwidth, arg_minClientBandwidth, arg_mtu,
						arg_hostname, arg_portnum, arg_username, arg_password, 
						arg_useAllLocalIfs);
					p->set_nuserTarget(arg_users);
					p->set_verbosity(arg_verbosity);
					platoons[nPlatoons++] = p;
				}
				catch(CORBA::COMM_FAILURE & ex) {
					cerr << "CORBA COMM_FAILURE unable to contact " 
						 << name << endl;
				}
			}
		}
		if (nPlatoons == 0) {
			printf("\nNo references alive.  Aborting.\n");
			exit(1);
		}

		printf("\n%d references were live.  Starting test.\n", nPlatoons);

		old_nalive = 0;
		old_ndead = 0;
		test_end = eclock() + arg_duration * tix_per_second;

		while (1) {
			clock_t now = eclock();

			if (arg_duration && eclock_after(now, test_end))
				break;

			int nconnecting = 0;
			int nalive = 0;
			int ndead = 0;
			for (i=0; i<nPlatoons; i++) {
				CORBA::ULong my_nconnecting;
				CORBA::ULong my_nalive;
				CORBA::ULong my_ndead;
				platoons[i]->getStatus(my_nconnecting, my_nalive, my_ndead);
				nconnecting += my_nconnecting;
				nalive += my_nalive;
				ndead += my_ndead;
			}
			if ((nalive != old_nalive) || (ndead != old_ndead)) {
				if (ndead != old_ndead) {
					if ((nalive == 0) && (nconnecting == 0)) {
						printf("All users dead.  Test failed.\n");
						exit(1);
					}
				}
				test_end = now + arg_duration * tix_per_second;

				printf("Total: %d users alive, %d connecting, %d users dead; at least %d seconds to end of test\n", 
					nalive, nconnecting, ndead, (int) ((test_end-now)/tix_per_second));
				old_nalive = nalive;
				old_ndead = ndead;
			}
			sleep(1);
		}
		printf("Test over.  %d users left standing.\n", old_nalive);

		/* don't leave the sourcerer's apprentice fetching files forever */
		for (i=0; i<nPlatoons; i++) {
			platoons[i]->reset();
		}

		orb->destroy();
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
