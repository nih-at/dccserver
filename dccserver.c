/* $NiH: dccserver.c,v 1.48 2003/04/15 13:11:34 wiz Exp $ */
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
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
#endif /* HAVE_POLL_H || HAVE_SYS_POLL_H */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_SOCKLEN_T
typedef unsigned int socklen_t;
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_ERR
void err(int, const char *, ...);
#endif

#ifndef HAVE_WARN
void warn(const char *, ...);
#endif

#ifndef HAVE_WARNX
void warnx(const char *, ...);
#endif

#define BACKLOG 10

typedef enum state_e { ST_NONE, ST_CHAT, ST_FSERVE, ST_SEND, ST_GET,
		       ST_END } state_t;

static int echo_input;
static char filename[1024];
static long filesize;
static int filter_control_chars;
static char nickname[1024];
static char partner[100];
static const char *prg;

volatile int sigchld = 0;

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

void
say(const char *line, FILE *fp)
{
    fwrite(line, strlen(line), 1, fp);
    fflush(fp);
    return;
}

void
tell_client(FILE *fp, int retcode, char *fmt, ...)
{
    va_list ap;

    /* safety flush, needed on at least Solaris */
    fflush(fp);
    fprintf(fp, "%03d %s", retcode, nickname);
    if (fmt != NULL) {
	fputs(" ", fp);
	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
    }
    fputs("\n", fp);
    fflush(fp);

    return;
}

/* get and save file from network */
int
get_file(int id, FILE *fp)
{
    char buf[8192];
    int len;
    long rem;
    int out;
    struct stat sb;
    long offset;
    long time_divisor;
    double transfer_rate;
    int exceed_warning_shown = 0;
    struct timeval before, after;

    if (stat(filename, &sb) == 0) {
	/* file exists */
	if ((sb.st_mode & S_IFMT) != S_IFREG) {
	    warnx("existing directory entry for `%s' not a file", filename);
	    tell_client(fp, 151, NULL);
	    return -1;
	}
	/* append (resume) */
	if ((sb.st_size >= 0) && (sb.st_size < filesize)) {
	    offset = sb.st_size;
	    warnx("file exists, resuming after %ld bytes", offset);
	}
	else if (sb.st_size == filesize) {
	    warnx("already have complete file, denying");
	    tell_client(fp, 151, NULL);
	    return -1;
	}
	else {
	    warnx("already have more bytes than client willing to send, denying");
	    tell_client(fp, 151, NULL);
	    return -1;
	}
	if ((out=open(filename, O_WRONLY, 0644)) == -1) {
	    warn("can't open file `%s' for appending",  filename);
	    tell_client(fp, 151, NULL);
	    return -1;
	}
    }
    else {
	offset = 0;
	if ((out=open(filename, O_WRONLY|O_CREAT, 0644)) == -1) {
	    warn("can't open file `%s' for writing",  filename);
	    tell_client(fp, 151, NULL);
	    return -1;
	}
    }

    if (lseek(out, offset, SEEK_SET) != offset) {
	warn("can't seek to offset %ld", offset);
	tell_client(fp, 151, NULL);
    }

    tell_client(fp, 121, "%ld", offset);

    gettimeofday(&before, NULL);

    /* get file data into local file */
    rem = filesize - offset;
    while((len=fread(buf, 1, sizeof(buf), fp)) > 0) {
	if (write(out, buf, len) < len) {
	    warn("write error on `%s'", filename);
	    close(out);
	    return -1;
	}
	rem -= len;

	if (rem < 0 && exceed_warning_shown == 0) {
	    exceed_warning_shown = 1;
	    warnx("getting more than %ld bytes for `%s'",
		  filesize, filename);
	}
    }

    gettimeofday(&after, NULL);

    if (close(out) == -1)
	warn("close error for `%s'", filename);

    if (before.tv_usec > after.tv_usec) {
	after.tv_usec += 1000000;
	after.tv_sec--;
    }
    after.tv_usec -= before.tv_usec;
    after.tv_sec -= before.tv_sec;
#if 0
    warnx("time taken: %lds %ldus", after.tv_sec, after.tv_usec);
#endif

    time_divisor = (after.tv_sec*1000+(after.tv_usec+500)/1000);
    /* avoid division by zero in _very_ fast (or small) transfers */
    if (time_divisor == 0)
	time_divisor = 1;
    transfer_rate = (((double)(filesize-rem-offset))/1024*1000)/time_divisor;
    if (rem <= 0) {
	warnx("child %d: client %s completed sending `%s' (%ld/%ld bytes got, %ld new)",
	      id, partner, filename, filesize-rem, filesize, filesize-rem-offset);
	warnx("child %d: time %ld.%ld seconds, transfer rate %.1fKb/s", id,
	      after.tv_sec, (after.tv_usec+50000)/100000, transfer_rate);
	return 0;
    }
    else {
	warnx("child %d: client %s closed connection for `%s' (%ld/%ld bytes got, "
	      "%ld new)", id, partner, filename, filesize-rem, filesize,
	      filesize-offset-rem);
	warnx("child %d: time %ld.%ld seconds, transfer rate %.1fKb/s", id,
	      after.tv_sec, (after.tv_usec+50000)/100000, transfer_rate);
    }    

    return -1;
}

/* some fserves incorrectly include the complete path -- */
/* strip it off */
static char *
strip_path(char *p)
{
    char *q;

    if ((q=strrchr(p, '/')) != NULL)
	p = ++q;
    if ((q=strrchr(p, '\\')) != NULL)
	p = ++q;

    return p;
}
/* parse line given by remote client */
int
parse_get_line(char *line)
{
    char *p, *q, *endptr;

    if ((p=strchr(line+4, ' ')) == NULL)
	return -1;
    *p = '\0';
    strlcpy(partner, line+4, sizeof(partner));

    q = p+1;
    if ((p=strchr(q, ' ')) == NULL)
	return -1;
    *p = '\0';

    filesize = strtol(q, &endptr, 10);
    if (*q == '\0' || *endptr != '\0' || (filesize <=0))
	return -1;

    /* remove path components in file name */
    q = strip_path(p+1);
    strlcpy(filename, q, sizeof(filename));
    if (strlen(filename) == 0)
	return -1;

    return 0;
}    

/* display line given from remote; filter out some characters */
/* assumes ASCII text */
void
display_remote_line(int id, const unsigned char *p)
{
    printf("<%d: %s> ", id, partner);
    while (*p) {
	if (*p > 0x7f) {
	    putchar('.');
	    p++;
	    continue;
	}
	if (filter_control_chars) {
	    switch (*p) {
	    case 0x03:
		/* skip control-C */
		p++;
		/*
		 * syntax: ^CN[,M] -- 0<=N<=99, 0<=M<=99, N fg, M bg,
		 *         or ^C to turn color off
		 */
		/* fg */
		if ('0' <= *p && *p <= '9') {
		    if ('0' <= p[1] && p[1] <= '9') {
			p++;
		    }
		    p++;
		    /* bg */
		    if (*p == ',' && '0' <= p[1] && p[1] <='9') {
			p+=2;
			if ('0' <= *p && *p <= '9') {
			    p++;
			}
		    }
		}
		continue;

	    case 0x02:
		/* bold */
	    case 0x0f:
		/* plain text */
	    case 0x12:
		/* reverse */
	    case 0x15:
		/* underlined */
	    case 0x1f:
		break;
		
	    default:
		putchar(*p);
		break;
	    }
	} else {
	    putchar(*p);
	}
	p++;
    }
    putchar('\n');
    fflush(stdout);
}

/* signal handler */
void
sig_handle(int signal)
{
    switch(signal) {
    case SIGCHLD:
	sigchld = 1;
	break;
    default:
	break;
    }

    return;
}

/* parse line from client, update state machine, and reply */
state_t
converse_with_client(FILE *fp, state_t state, char *line, int id)
{
    state_t ret;

    ret = state;
    switch(state) {
    case ST_NONE:
	if (strncmp("100 ", line, 4) == 0) {
	    strlcpy(partner, line+4, sizeof(partner));
	    tell_client(fp, 101, NULL);
	    warnx("starting chat with %d: %s", id, partner);
	    ret = ST_CHAT;
	}
	else if (strncmp("110 ", line, 4) == 0) {
	    strlcpy(partner, line+4, sizeof(partner));
#ifdef NOT_YET
	    tell_client(fp, 111, NULL);
	    warnx("starting fserve session with %d: %s", id, partner);
	    ret = ST_FSERVE;
#endif
	    tell_client(fp, 151, NULL);
	    ret = ST_END;
	}
	else if (strncmp("120 ", line, 4) == 0) {
	    /* Client sending file */
	    if (parse_get_line(line) < 0) {
		tell_client(fp, 151, NULL);
		ret = ST_END;
		break;
	    }

	    warnx("getting file `%s' (%ld bytes) from %d: %s", filename,
		  filesize, id, partner);
	    fflush(stderr);

	    get_file(id, fp);
	    ret = ST_END;
	}
	else if (strncmp("130 ", line, 4) == 0) {
#ifdef NOT_YET
	    tell_client(fp, 131, NULL);
	    ret = ST_GET;
#endif
	    tell_client(fp, 151, NULL);
	    ret = ST_END;
	}
	else {
	    tell_client(fp, 151, NULL);
	    ret = ST_END;
	    break;
	}
	break;

    case ST_CHAT:
	display_remote_line(id, line);
	break;

    case ST_FSERVE:
	if (strcasecmp(line, "quit") == 0 ||
	    strcasecmp(line, "exit") == 0) {
	    say("Goodbye!", fp);
	    ret = ST_END;
	}
	break;
    case ST_GET:
	break;   

    case ST_SEND:
    case ST_END:
    default:
	    tell_client(fp, 151, NULL);
	    ret = ST_END;
    }

    return ret;
}

/* main child routine: read line from client and call parser */
void
communicate_with_client(int sock, int id)
{
    FILE *fp;
    char buf[8192];
    state_t state;

    state = ST_NONE;

    if ((fp=fdopen(sock, "r+")) == NULL) {
	(void)close(sock);
	err(1, "[child] can't fdopen");
    }

    while ((state != ST_END) && (fgets(buf, sizeof(buf), fp) != NULL)) {
	if ((*buf == '\n') || (*buf == '\r')) {
	    /* empty line */
	    *buf = '\0';
	}
	else if (strtok(buf, "\n\r") == NULL) {
	    warn("client sent too long line");
	    tell_client(fp, 151, NULL);
	    state = ST_END;
	    continue;
	}
	state = converse_with_client(fp, state, buf, id);
    }

    if (state != ST_END)
	warnx("closing connection with %d: %s", id, partner);

    (void)fclose(fp);
    exit(0);
}

/*
 * create child to handle connection and update structure tracking
 * children
 */
void
handle_connection(int sock, int oldsock)
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

    switch(child=fork()) {
    case 0:
	close(oldsock);
	communicate_with_client(sock, i);
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
usage(void)
{

    fprintf(stderr, "%s: emulate mirc's /dccserver command\n\n"
	    "Usage:\n"
	    "%s [-ehiv] [-n nickname] [-p port]\n"
	    "Where port is the port on which %s should listen,\n"
	    "and nickname the nick that should be used (defaults are 59 "
	    "and 'dccserver').\n", prg, prg, prg);
    exit(1);
}

/* handle input from user */
int
handle_input(void)
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
    int pollret;
    int port, port_count;
    struct pollfd *pollset;

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
    prg = argv[0];

    while ((c=getopt(argc, argv, "ehin:p:v")) != -1) {
	switch(c) {
	case 'e':
	    echo_input = 1;
	    break;

	case 'h':
	    usage();

	case 'i':
	    filter_control_chars = 0;
	    break;

	case 'n':
	    strlcpy(nickname, optarg, sizeof(nickname));
	    break;

	case 'p':
	    port = strtol(optarg, &endptr, 10);
	    if (*optarg == '\0' || *endptr != '\0')
		usage();
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
    signal(SIGCHLD, sig_handle);

    /* one for stdin, one for each listening socket */
    if ((pollset=malloc(sizeof(*pollset)*(port_count+1))) == NULL)
	err(1, "can't malloc pollset");

    while (1) {
	struct sockaddr_in raddr;
	socklen_t raddrlen;
	int new_sock;

	/* clean up after dead children */
	while (sigchld > 0) {
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

	    if (handle_input() < 0)
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

		(void)printf("Connection from %s/%d\n",
			     inet_ntoa(raddr.sin_addr), ntohs(raddr.sin_port));
		fflush(stdout);

		/* do something */
		handle_connection(new_sock, listen_socket[i]);
	    }
	}
    }

    return 0;
}
