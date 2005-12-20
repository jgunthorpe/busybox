/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Connect to host at port using address resolution from getaddrinfo
 *
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "libbb.h"

/* Return network byte ordered port number for a service.
 * If "port" is a number use it as the port.
 * If "port" is a name it is looked up in /etc/services, if it isnt found return
 * default_port
 * Note this returns in host byte order.
 */
unsigned short bb_lookup_port(const char *port, const char *protocol, unsigned short default_port)
{
	unsigned short port_nr = default_port;
	if (port) {
		char *endptr;
		int old_errno;
		long port_long;

		/* Since this is a lib function, we're not allowed to reset errno to 0.
		 * Doing so could break an app that is deferring checking of errno. */
		old_errno = errno;
		errno = 0;
		port_long = strtol(port, &endptr, 10);
		if (errno != 0 || *endptr!='\0' || endptr==port || port_long < 0 || port_long > 65535) {
			struct servent *tserv = getservbyname(port, protocol);
			if (tserv) {
				port_nr = ntohs(tserv->s_port);
			}
		} else {
			port_nr = port_long;
		}
		errno = old_errno;
	}
	return port_nr;
}

/* This is like getaddrinfo, except it only returns 1 AI and into caller
   allocated memory. If IPv6 is turned off then this is small, otherwise it
   calls getaddrinfo.. */
void bb_lookup_host(struct bb_addrinfo *s_ai, const char *host,
		    unsigned int port)
{
#ifndef CONFIG_FEATURE_IPV6
	struct hostent *he;
	struct sockaddr_in *s_in = (struct sockaddr_in *)&s_ai->ai_addr;

	memset(s_ai, 0, sizeof(*s_ai));
	s_ai->ai_family = s_ai->ai_addr.ss_family = AF_INET;
	s_ai->ai_socktype = SOCK_STREAM;
	s_ai->ai_protocol = 0;
	he = xgethostbyname(host);
	memcpy(&s_in->sin_addr, he->h_addr_list[0], he->h_length);
	s_ai->ai_addrlen = sizeof(struct sockaddr_in);
	s_in->sin_port = htons(port);
#else
	int res;
	struct addrinfo *ai;
	struct addrinfo hint = {.ai_family = PF_UNSPEC, .ai_socktype = SOCK_STREAM};
	char port_s[10];

	snprintf(port_s,sizeof(port_s),"%u",port);
	if ((res = getaddrinfo(host,port_s,&hint,&ai)) != 0)
		bb_error_msg_and_die("getaddrinfo failed %s",
				     gai_strerror(res));
	s_ai->ai_family = ai->ai_family;
	s_ai->ai_socktype = ai->ai_socktype;
	s_ai->ai_protocol = ai->ai_protocol;
	s_ai->ai_addrlen = ai->ai_addrlen;
	memcpy(&s_ai->ai_addr,ai->ai_addr,ai->ai_addrlen);
	freeaddrinfo(ai);
#endif
}

unsigned int bb_getport(const struct bb_addrinfo *s_ai)
{
#ifdef CONFIG_FEATURE_IPV6
	if (s_ai->ai_family == AF_INET6)
		return ntohs(((struct sockaddr_in6 *)&s_ai->ai_addr)->sin6_port);
#endif
	return ntohs(((struct sockaddr_in *)&s_ai->ai_addr)->sin_port);
}

const char *bb_ptoa(const struct bb_addrinfo *s_ai)
{
#ifndef CONFIG_FEATURE_IPV6
	return inet_ntoa(((struct sockaddr_in *)&s_ai->ai_addr)->sin_addr);
#else
	static char buf[100];
	buf[0] = 0;
	getnameinfo((struct sockaddr *)&s_ai->ai_addr,s_ai->ai_addrlen,
		    buf,sizeof(buf),0,0,NI_NUMERICHOST);
	return buf;
#endif
}

int xconnect(const struct bb_addrinfo *s_ai)
{
	int s = socket(s_ai->ai_family, s_ai->ai_socktype, s_ai->ai_protocol);
	if (connect(s,(struct sockaddr *)&s_ai->ai_addr,s_ai->ai_addrlen) < 0)
	{
		if (ENABLE_FEATURE_CLEAN_UP) close(s);
		bb_perror_msg_and_die("Unable to connect to remote host (%s %u)",
				bb_ptoa(s_ai),bb_getport(s_ai));
	}
	return s;
}
