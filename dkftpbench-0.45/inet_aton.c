// horrible kludge for solaris
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>      

#ifndef HAVE_INET_ATON

#ifdef __cplusplus
extern "C" {
#endif

int     inet_aton(const char *cp, struct in_addr *inp)
{
	inp->s_addr = inet_addr(cp);
	return (inp->s_addr != (in_addr_t)-1);
}

#ifdef __cplusplus
}
#endif

#endif
