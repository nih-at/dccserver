/* $NiH: dccserver.c,v 1.2 2002/10/12 20:13:26 wiz Exp $ */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PROGRAM "dccserver"
#define VERSION "0.1"

#define BACKLOG 10

static const char *prg;

void
usage(void)
{

    fprintf(stderr, "%s: emulate mirc's /dccserver command\n\n"
	    "Usage:\n"
	    "%s [-n nickname] [-p port]\n"
	    "Where port is the port on which %s should listen,\n"
	    "and nickname the nick that should be used (defaults are 59 "
	    "and 'dccserver').", prg, prg, prg);
    exit(1);
}

int
main(int argc, char *argv[])
{
    char *endptr;
    char nickname[1024];
    int sock, sock_opt;
    int c;
    long port;
#ifdef IPV6
    struct sockaddr_in6 laddr;
#else
    struct sockaddr_in laddr;
#endif

    snprintf(nickname, sizeof(nickname), "dccserver");
    port = 59;
    prg = argv[0];

    while ((c=getopt(argc, argv, "hn:p:v")) != -1) {
	switch(c) {
	case 'h':
	    usage();

	case 'v':
	    printf(PROGRAM " " VERSION);
	    exit(0);

	case 'n':
	    snprintf(nickname, sizeof(nickname), "%s", optarg);
	    break;

	case 'p':
	    port = strtol(optarg, &endptr, 10);
	    if (optarg[1] == '\0' || *endptr != '\0')
		usage();

	    break;

	default:
	    /* unreachable */
	    exit(1);
	}
    }

#ifdef IPV6
    if ((sock=socket(AF_INET6, SOCK_STREAM, 0)) == -1)
	err(1, "can't open socket");
#else
    if ((sock=socket(AF_INET, SOCK_STREAM, 0)) == -1)
	err(1, "can't open socket");
#endif

    /* Tell system that local addresses can be reused. */
    sock_opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&sock_opt,
		   sizeof(sock_opt)) == -1) {
	(void)close(sock);
	err(1, "setsockopt(SO_REUSEADDR) failed");
    }

    memset(&laddr, '\0', sizeof(laddr));
#ifdef IPV6
    laddr.sin6_family = AF_INET6;
    laddr.sin6_port = htons(port);
    laddr.sin6_addr = in6addr_any;
#else
    laddr.sin_family = AF_INET;
    laddr.sin_port = htons(port);
    laddr.sin_addr.s_addr = htonl(INADDR_ANY);
#endif

    if (bind(sock, (struct sockaddr *)&laddr, sizeof(laddr)) == -1) {
	(void)close(sock);
	err(1, "can't bind socket to port %ld", port);
    }

    if (listen(sock, BACKLOG) == -1)
	err(1, "can't listen on socket");

    while (1) {
#ifdef IPV6
	struct sockaddr_in6 raddr;
	char addrbuf[INET6_ADDRSTRLEN];
#else
	struct sockaddr_in raddr;
#endif
	socklen_t raddrlen;
	int new_sock;

	raddrlen = sizeof(raddr);
	if ((new_sock=accept(sock, (struct sockaddr *)&raddr,
			     &raddrlen))  == -1) {
	    if ((errno != EINTR) && (errno != ECONNABORTED))
		warn("accept failed");
	    continue;
	}

#ifdef IPV6
	(void)printf("Connection from %s/%d\n",
		     inet_ntop(AF_INET6, (void *)&raddr.sin6_addr,
			       addrbuf, sizeof(addrbuf)),
		     ntohs(raddr.sin6_port));
#else
	(void)printf("Connection from %s/%d\n",
		     inet_ntoa(raddr.sin_addr), ntohs(raddr.sin_port));
#endif

	/* do something */
	close(new_sock);
    }

    return 0;
}
