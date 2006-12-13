/*
 * Open connection for network block device
 *
 * Copyright 1997,1998 Pavel Machek, distribute under GPL
 *  <pavel@atrey.karlin.mff.cuni.cz>
 *
 * Version 1.0 - 64bit issues should be fixed, now
 * Version 1.1 - added bs (blocksize) option (Alexey Guzeev, aga@permonline.ru)
 * Version 1.2 - I added new option '-d' to send the disconnect request
 * Version 2.0 - Version synchronised with server
 * Version 2.1 - Check for disconnection before INIT_PASSWD is received
 * 	to make errormsg a bit more helpful in case the server can't
 * 	open the exported file.
 */

/* This header file is shared by client & server. They really have
 * something to share...
 * */
#include <asm/page.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netinet/in.h>		/* sockaddr_in, htons, in_addr */
#include <netdb.h>		/* hostent, gethostby*, getservby* */
#include <stdio.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdlib.h>
#include <endian.h>
#include <string.h>
#include <errno.h>
#include <linux/ioctl.h>

#define u32 uint32_t
#define u64 uint64_t

#include "nbd.h"
#include "busybox.h"

/* Client/server protocol is as follows:
   Send INIT_PASSWD
   Send 64-bit cliserv_magic
   Send 64-bit size of exported device
   Send 128 bytes of zeros (reserved for future use)
 */

static const u64 cliserv_magic = 0x00420281861253LL;
#define INIT_PASSWD "NBDMAGIC"

static void err(const char *s)
{
	const int maxlen = 150;
	char s1[maxlen], *s2;

	strncpy(s1, s, maxlen);
	if ((s2 = strstr(s, "%m"))) {
		strcpy(s1 + (s2 - s), strerror(errno));
		s2 += 2;
		strcpy(s1 + strlen(s1), s2);
	}
	else if ((s2 = strstr(s, "%h"))) {
		strcpy(s1 + (s2 - s), hstrerror(h_errno));
		s2 += 2;
		strcpy(s1 + strlen(s1), s2);
	}

	s1[maxlen-1] = '\0';
	fprintf(stderr, "Error: %s\n", s1);
	exit(1);
}

static void setmysockopt(int sock)
{
	int size = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &size, sizeof(int)) < 0)
		 err("(no sockopt/1: %m)");
	size = 1;
	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &size, sizeof(int)) < 0)
		 err("(no sockopt/2: %m)");
}

#if __BYTE_ORDER == __BIG_ENDIAN
static u64 ntohll(u64 a)
{
	return a;
}
#else
static u64 ntohll(u64 a)
{
	u32 lo = a & 0xffffffff;
	u32 hi = a >> 32U;
	lo = ntohl(lo);
	hi = ntohl(hi);
	return ((u64) lo) << 32U | hi;
}
#endif
#define htonll ntohll

#define PACKAGE_VERSION ""

static int opennet(char *host, int port)
{
	int sock;
	int res;
        struct addrinfo *ai;
        struct addrinfo hint = {.ai_family = PF_UNSPEC, .ai_socktype = SOCK_STREAM};
        char port_s[10];

        snprintf(port_s,sizeof(port_s),"%u",port);
        if ((res = getaddrinfo(host,port_s,&hint,&ai)) != 0)
                err("getaddrinfo failed");
	if ((sock = socket(ai->ai_family, ai->ai_socktype,
			   ai->ai_protocol)) < 0)
		err("Socket failed: %m");
	if ((connect(sock, ai->ai_addr,ai->ai_addrlen) < 0))
		err("Connect: %m");
        freeaddrinfo(ai);

	setmysockopt(sock);
	return sock;
}

extern int nbd_client_main(int argc, char **argv)
{
	int port, sock, nbd;
	u64 magic, size64;
	unsigned long size;
	char buf[256] = "\0\0\0\0\0\0\0\0\0";
	int blocksize=1024;
	char *hostname;
	int swap=0;

	if (argc < 3) {
	errmsg:
		bb_show_usage();
	}

	++argv; --argc; /* skip programname */

	if (strcmp(argv[0], "-d")==0) {
		nbd = open(argv[1], O_RDWR);
		if (nbd < 0)
			err("Can not open NBD: %m");
		printf("Disconnecting: que, ");
		if (ioctl(nbd, NBD_CLEAR_QUE)< 0)
			err("Ioctl failed: %m\n");
		printf("disconnect, ");
		if (ioctl(nbd, NBD_DISCONNECT)<0)
			err("Ioctl failed: %m\n");
		printf("sock, ");
		if (ioctl(nbd, NBD_CLEAR_SOCK)<0)
			err("Ioctl failed: %m\n");
		printf("done\n");
		return 0;
	}

	if (strncmp(argv[0], "bs=", 3)==0) {
		blocksize=atoi(argv[0]+3);
		++argv; --argc; /* skip blocksize */
	}

	if (argc==0) goto errmsg;
	hostname=argv[0];
	++argv; --argc; /* skip hostname */

	if (argc==0) goto errmsg;
	port = atoi(argv[0]);
	++argv; --argc; /* skip port */

	if (argc==0) goto errmsg;
	sock = opennet(hostname, port);
	nbd = open(argv[0], O_RDWR);
	if (nbd < 0)
	  err("Can not open NBD: %m");
	++argv; --argc; /* skip device */

	if (argc>1) goto errmsg;
	if (argc!=0) swap=1;
	argv=NULL; argc=0; /* don't use it later suddenly */

	printf("Negotiation: ");
	if (read(sock, buf, 8) < 0)
		err("Failed/1: %m");
	if (strlen(buf)==0)
		err("Server closed connection");
	if (strcmp(buf, INIT_PASSWD))
		err("INIT_PASSWD bad");
	printf(".");
	if (read(sock, &magic, sizeof(magic)) < 0)
		err("Failed/2: %m");
	magic = ntohll(magic);
	if (magic != cliserv_magic)
		err("Not enough cliserv_magic");
	printf(".");

	if (read(sock, &size64, sizeof(size64)) < 0)
		err("Failed/3: %m\n");
	size64 = ntohll(size64);

	if ((size64>>10) > (~0UL >> 1)) {
		printf("size = %luMB", (unsigned long)(size64>>20));
		err("Exported device is too big for me. Get 64-bit machine :-(\n");
	} else
		printf("size = %luKB", (unsigned long)(size64>>10));

	if (read(sock, &buf, 128) < 0)
		err("Failed/4: %m\n");
	printf("\n");

	if (size64/blocksize > (~0UL >> 1))
		err("Device too large.\n");
	else {
		int er;
		if (ioctl(nbd, NBD_SET_BLKSIZE, (unsigned long)blocksize) < 0)
			err("Ioctl/1.1a failed: %m\n");
		size = (unsigned long)(size64/blocksize);
		if ((er = ioctl(nbd, NBD_SET_SIZE_BLOCKS, size)) < 0)
			err("Ioctl/1.1b failed: %m\n");
		fprintf(stderr, "bs=%d, sz=%lu\n", blocksize, size);
	}

	ioctl(nbd, NBD_CLEAR_SOCK);
	if (ioctl(nbd, NBD_SET_SOCK, sock) < 0)
		err("Ioctl/2 failed: %m\n");

	if (swap)
		err("You have to compile me on machine with swapping patch enabled in order to use it later.");

	/* Go daemon */

	chdir("/");
	if (fork())
		exit(0);

	if (ioctl(nbd, NBD_DO_IT) < 0)
		fprintf(stderr, "Kernel call returned: %m");
	else
		fprintf(stderr, "Kernel call returned.");
	printf("Closing: que, ");
	ioctl(nbd, NBD_CLEAR_QUE);
	printf("sock, ");
	ioctl(nbd, NBD_CLEAR_SOCK);
	printf("done\n");
	return 0;
}
