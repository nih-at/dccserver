/* $NiH: dccserver.c,v 1.63 2003/05/14 09:45:10 wiz Exp $ */
/*-
 * Copyright (c) 2002, 2003 Thomas Klausner.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution. 
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.  
 *
 * THIS SOFTWARE IS PROVIDED BY THOMAS KLAUSNER ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"
#include "dccserver.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_ERR_H
#include <err.h>
#endif /* HAVE_ERR_H */
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#elif HAVE_SYS_POLL_H
#include <sys/poll.h>
#else
#include "poll.h"
#endif /* HAVE_POLL_H || HAVE_SYS_POLL_H */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_ERR
void err(int, const char *, ...);
#endif

#ifndef HAVE_ERRX
void errx(int, const char *, ...);
#endif

#ifndef HAVE_SOCKLEN_T
typedef unsigned int socklen_t;
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_WARN
void warn(const char *, ...);
#endif

#ifndef HAVE_WARNX
void warnx(const char *, ...);
#endif

#ifndef SIGINFO
#define SIGINFO SIGUSR1
#endif

/* backlog argument for listen() */
#define BACKLOG 10

/* option arguments/toggles */
int filter_control_chars;
char nickname[1024];

/* signal handler variables */

volatile sig_atomic_t sigchld = 0;
volatile sig_atomic_t siginfo = 0;
volatile sig_atomic_t sigint = 0;


#define NO_OF_CHILDREN 100
typedef struct child_s {
    int sock;
    pid_t pid;
} child_t;

child_t children[NO_OF_CHILDREN];

/* XXX: could be dynamic but that's probably more trouble than it's worth */
#define NO_OF_LISTEN_PORTS 100
int listen_port[NO_OF_LISTEN_PORTS];
int listen_socket[NO_OF_LISTEN_PORTS];


/* signal handler */
void
sig_handle(int signo)
{
    switch(signo) {
    case SIGCHLD:
	sigchld = 1;
	break;
    case SIGINFO:
	siginfo = 1;
	break;
    case SIGINT:
	sigint = 1;
	break;
    default:
	break;
    }

    return;
}


/*
 * create child to handle connection and update structure tracking
 * children
 */
void
handle_connection(int sock)
{
    pid_t child;
    int i;

    /* find first free child ID */
    for (i=0; i<NO_OF_CHILDREN; i++)
	if (children[i].pid == -1)
	    break;

    /* none found */
    if (i >= NO_OF_CHILDREN) {
	warnx("too many children");
	close(sock);
	return;
    }

    if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
	(void)close(sock);
	err(1, "can't set socket to non-blocking");
    }

    switch(child=fork()) {
    case 0:
	child_loop(sock, i);
	/* UNREACHABLE */
	_exit(1);

    case -1:
	warn("fork error");
	break;
	
    default:
	break;
    }

    children[i].pid = child;
    children[i].sock = sock;
    warnx("child %d started (pid %d)", i, child);
    fflush(stderr);

    return;
}

void
collect_child(void)
{
    int i;
    int status;
    pid_t deadpid;

    while (((deadpid=waitpid(-1, &status, WNOHANG)) != -1)
	   && deadpid != 0) {
	sigchld = 0;
	for (i=0; i<NO_OF_CHILDREN; i++) {
	    if (children[i].pid == deadpid) {
		children[i].pid = -1;
		close(children[i].sock);
		warnx("child %d died", i);
		break;
	    }
	}
	if (i == NO_OF_CHILDREN)
	    warnx("child %ld found dead, but unknown", (long)deadpid);
    }
}

void
kill_children(void)
{
    int i;

    for (i=0; i<NO_OF_CHILDREN; i++) {
	if (children[i].pid != -1)
	    kill(children[i].pid, SIGINT);

	while (sigchld > 0)
	    collect_child();
    }
}

void
usage(const char *prg)
{

    fprintf(stderr, "%s: emulate mIRC's /dccserver command\n\n"
	    "Usage:\n"
	    "%s [-ehiv] [-n nickname] [-p port]\n"
	    "Where port is the port on which %s should listen,\n"
	    "and nickname the nick that should be used (defaults are 59 "
	    "and 'dccserver').\n", prg, prg, prg);
    exit(1);
}

/* handle input from user */
int
handle_input(int echo_input)
{
    char buf[8192];
    int child;
    char *end;

    if (fgets(buf, sizeof(buf), stdin) == NULL)
	return -1;

    if (echo_input) {
	fputs(buf, stdout);
	fflush(stdout);
    }

    child = strtol(buf, &end, 10);
    if ((child >= 0 && child < NO_OF_CHILDREN) && end[0] == ':'
	&& end[1] == ' ') {
	if (children[child].pid == -1) {
	    warnx("child %d is dead", child);
	    return 0;
	}

	write(children[child].sock, end+2, strlen(end+2));
    }
    else if (strncmp(buf, "close ", 6) == 0) {
	child = strtol(buf+6, &end, 10);
	if ((child >= 0 && child < NO_OF_CHILDREN)) {
	    if (children[child].pid == -1) {
		warnx("child %d is dead", child);
		return 0;
	    }

	    warnx("closing connection for child %d", child);
	    kill(children[child].pid, SIGINT);
	}
	return 0;
    }
    else if (strncmp(buf, "info", 4) == 0)
	kill(0, SIGINFO);
    else if (strncmp(buf, "quit", 4) == 0)
	return -1;

    return 0;
}

/* create a socket and bind it to port "port" */
int
create_and_bind_socket(int port)
{
    int sock, sock_opt;
    struct sockaddr_in laddr;

    if ((sock=socket(AF_INET, SOCK_STREAM, 0)) == -1)
	err(1, "can't open socket");

    if (fcntl(sock, F_SETFD, FD_CLOEXEC) == -1) {
	(void)close(sock);
	err(1, "can't set socket close-on-exec");
    }

    /* Tell system that local addresses can be re-used. */
    sock_opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&sock_opt,
		   sizeof(sock_opt)) == -1) {
	(void)close(sock);
	err(1, "setsockopt(SO_REUSEADDR) failed");
    }

    memset(&laddr, '\0', sizeof(laddr));
    laddr.sin_family = AF_INET;
    laddr.sin_port = htons(port);
    laddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&laddr, sizeof(laddr)) == -1) {
	(void)close(sock);
	err(1, "can't bind socket to port %d", port);
    }

    return sock;
}

/* open sockets for all ports on which we should listen. */
void
create_and_bind_all_sockets(void)
{
    int i;

    for (i=0; i < NO_OF_LISTEN_PORTS && listen_port[i] != -1; i++)
	listen_socket[i] = create_and_bind_socket(listen_port[i]);

    return;
}

/*
 * remember to listen on port "port" too; avoid listening on a port
 * twice.
 */
void
add_listen_port(int port)
{
    int i;

    for (i=0; i<NO_OF_LISTEN_PORTS; i++) {
	if (listen_port[i] == port) {
	    warnx("already listening on port %d", port);
	    return;
	}

	if (listen_port[i] == -1) {
	    listen_port[i] = port;
	    return;
	}
    }

    errx(1, "too many listening ports specified, maximum of %d allowed",
	 NO_OF_LISTEN_PORTS);
}

/* call listen() for each socket on which we should */
void
start_listening(void)
{
    int i;

    for (i=0; i < NO_OF_LISTEN_PORTS && listen_port[i] != -1; i++)
	if (listen(listen_socket[i], BACKLOG) == -1)
	    err(1, "can't listen on socket");

    return;
}

/*
 * set up server listening on connections and input from user, and
 * call appropriate functions to handle them
 */
int
main(int argc, char *argv[])
{
    char *endptr;
    int c, i;
    int echo_input;
    int pollret;
    int port, port_count;
    struct pollfd *pollset;
    struct sigaction sa;

    strlcpy(nickname, "dccserver", sizeof(nickname));
    /* do not echo lines entered by default */
    echo_input = 0;
    /* default to filtering out control characters */
    filter_control_chars = 1;
    for (i=0; i<NO_OF_LISTEN_PORTS; i++) {
	listen_port[i] = -1;
	listen_socket[i] = -1;
    }
    port_count = 0;

    while ((c=getopt(argc, argv, "ehin:p:v")) != -1) {
	switch(c) {
	case 'e':
	    echo_input = 1;
	    break;

	case 'h':
	    usage(argv[0]);

	case 'i':
	    filter_control_chars = 0;
	    break;

	case 'n':
	    strlcpy(nickname, optarg, sizeof(nickname));
	    break;

	case 'p':
	    port = strtol(optarg, &endptr, 10);
	    if (*optarg == '\0' || *endptr != '\0')
		usage(argv[0]);
	    if (port < 1 || port > 65535)
		errx(1, "invalid port argument (0 < port < 65536)");
	    add_listen_port(port);
	    port_count++;

	    break;

	case 'v':
	    puts(PACKAGE_STRING);
	    exit(0);

	default:
	    /* unreachable */
	    exit(1);
	}
    }

    /* listen on port 59 by default */
    if (listen_port[0] == -1) {
	listen_port[0] = 59;
	port_count = 1;
    }

    /* trying to imprison dccserver in a chroot */
    if (chroot(".") == -1) {
	if (errno == EPERM) {
	    fputs("dccserver is not setuid root.\n"
		  "For binding below port 1024 (like the default port 59),\n"
		  "root privileges are needed.  Additionally, dccserver\n"
		  "will chroot(2) itself to the current directory to reduce\n"
		  "effect of any exploits.\n", stderr);
	}
	warn("can't chroot");
    }
    else if (chdir("/") == -1)
	warn("can't chdir to \"/\" in chroot");

    create_and_bind_all_sockets();

    /* drop privileges */
    setuid(getuid());

    start_listening();

    for (c=0; c<NO_OF_CHILDREN; c++)
	children[c].pid = -1;

    sa.sa_handler = sig_handle;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGINFO, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* one for stdin, one for each listening socket */
    if ((pollset=malloc(sizeof(*pollset)*(port_count+1))) == NULL)
	err(1, "can't malloc pollset");

    while (1) {
	struct sockaddr_in raddr;
	socklen_t raddrlen;
	int new_sock;

	/* terminate orderly */
	if (sigint > 0) {
	    warnx("closing connections and exiting");
	    break;
	}

	/* clean up after dead children */
	while (sigchld > 0)
	    collect_child();

	/* display status if requested */
	if (siginfo > 0) {
	    int count;

	    siginfo = 0;
	    for (count=i=0; i<NO_OF_CHILDREN; i++) {
		if (children[i].pid != -1)
		    count++;
	    }
	    warnx("%d running child%s", count, count == 1 ? "" : "ren");
	}

	/* poll for events from network and user */
	/* user */
	pollset[port_count].fd = 0;
	pollset[port_count].events = POLLIN|POLLPRI;
	pollset[port_count].revents = 0;
	/* network */
	for (i=0; i<port_count; i++) {
	    pollset[i].fd = listen_socket[i];
	    pollset[i].events = POLLIN|POLLPRI;
	    pollset[i].revents = 0;
	}

	if ((pollret=poll(pollset, port_count+1, 500)) == -1) {
	    if (errno != EINTR)
		warn("poll error");
	    continue;
	}

	if (pollret == 0)
	    continue;

	if (pollset[port_count].revents != 0) {
	    /* some data from stdin */

	    if (handle_input(echo_input) < 0)
		break;
	}
	for (i=0; i<port_count; i++) {
	    if (pollset[i].revents != 0) {
		/* some data from network */
		raddrlen = sizeof(raddr);
		if ((new_sock=accept(listen_socket[i], (struct sockaddr *)&raddr,
				     &raddrlen)) == -1) {
		    if ((errno != EINTR) && (errno != ECONNABORTED))
			warn("accept failed");
		    continue;
		}

		(void)printf("Connection to port %d from %s/%d\n", listen_port[i],
			     inet_ntoa(raddr.sin_addr), ntohs(raddr.sin_port));
		fflush(stdout);

		/* do something */
		handle_connection(new_sock);
	    }
	}
    }

    kill_children();
    return 0;
}
