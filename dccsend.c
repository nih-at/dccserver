/* $NiH$ */
/*-
 * Copyright (c) 2003 Thomas Klausner.
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
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_ERR_H
#include <err.h>
#endif /* HAVE_ERR_H */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_SOCKLEN_T
typedef unsigned int socklen_t;
#endif

#ifndef HAVE_STRLZCPY
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

int
connect_to_server(char *host, char *port)
{
    struct addrinfo hints, *res0, *res;
    int error, s;
    char *cause = NULL;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    if ((error=getaddrinfo(host, port, &hints, &res0)) != 0) {
	errx(1, "cannot get host ``%s'' port %s: %s",
	     host, port, gai_strerror(error));
    }

    s = -1;
    for (res = res0; res; res = res->ai_next) {
	if ((s=socket(res->ai_family, res->ai_socktype,
		      res->ai_protocol)) < 0) {
	    cause = "create socket";
	    continue;
	}

	if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
	    cause = "connect";
	    close(s);
	    s = -1;
	    continue;
	}

	/* okay we got one */
	break;
    }
    if (s < 0)
	err(1, "cannot %s", cause);

    freeaddrinfo(res0);

    return s;
}

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
send_file(int id, FILE *fp)
{
    char buf[8192];
    int len;
    long rem;
    int out;
    struct stat sb;
    long offset;
    int exceed_warning_shown = 0;

    if (stat(filename, &sb) == 0) {
	/* file exists */
	if ((sb.st_mode & S_IFMT) != S_IFREG) {
	    /* XXX: rename */
	    tell_client(fp, 151, NULL);
	    return -1;
	}
	/* append (resume) */
	if ((sb.st_size > 0) && (sb.st_size < filesize)) {
	    offset = sb.st_size;
	    warnx("file exists, resuming after %ld bytes", offset);
	}
	else {
	    /* XXX: rename */
	    tell_client(fp, 151, NULL);
	    return -1;
	}
	if ((out=open(filename, O_WRONLY|O_APPEND, 0644)) == -1) {
	    warn("can't open file `%s' for appending",  filename);
	    tell_client(fp, 151, NULL);
	    return -1;
	}
    }
    else {
	offset = 0;
	if ((out=open(filename, O_WRONLY|O_CREAT|O_EXCL, 0644)) == -1) {
	    warn("can't open file `%s' for writing",  filename);
	    tell_client(fp, 151, NULL);
	    return -1;
	}
    }

    tell_client(fp, 121, "%ld", offset);

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

    if (close(out) == -1)
	warn("close error for `%s'", filename);

    if (rem <= 0) {
	warnx("`%s' from %d: %s complete (%ld/%ld bytes got, %ld new)", filename,
	      id, partner, filesize-rem, filesize, filesize-rem-offset);
	return 0;
    }
    else {
	warnx("client %d: %s closed connection for `%s' (%ld/%ld bytes got, "
	      "%ld new)", filename, id, partner, filesize-rem, filesize,
	      filesize-offset-rem);
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

/* parse line from server, update state machine, and reply */
state_t
converse_with_server(FILE *fp, state_t state, char *line, int id)
{
    state_t ret;
    unsigned char *p;

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
communicate_with_client(int sock, char *filename)
{
    FILE *fp;
    char buf[8192];
    state_t state;

    state = ST_NONE;

    if ((fp=fdopen(sock, "r+")) == NULL) {
	(void)close(sock);
	err(1, "[child] can't fdopen");
    }

    
    say("
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
	state = converse_with_client(fp, state, buf);
    }

    if (state != ST_END)
	warnx("closing connection with %s", partner);

    (void)fclose(fp);
    exit(0);
}

void
usage(void)
{

    fprintf(stderr, "%s: send a file to a MIRC /dccserver\n\n"
	    "Usage:\n"
	    "%s [-hv] [-n nickname] [-p port] [-r remotenick] host filename\n"
	    "Where nickname is the own nick that should be used (default: 'dccserver'),\n"
	    "port is the port on which the remote dccserver listens (default: 59),\n"
	    "and remotenick is the expected remote nickname on the server (default: accept any);\n",
	    "host is the internet hostname to connect to, and filename is the file to send.",
	    prg, prg);
    exit(1);
}

/*
 * set up server listening on connections and input from user, and
 * call appropriate functions to handle them
 */
int
main(int argc, char *argv[])
{
    char *endptr;
    int sock, sock_opt;
    int c;
    int pollret;
    long port;
    char *portstr;
    struct sockaddr_in laddr;
    struct pollfd pollset[2];

    strlcpy(nickname, "dccserver", sizeof(nickname));
    port = 59;
    portstr = "59";
    prg = argv[0];

    while ((c=getopt(argc, argv, "hn:p:r:v")) != -1) {
	switch(c) {
	case 'h':
	    usage();

	case 'n':
	    strlcpy(nickname, optarg, sizeof(nickname));
	    break;

	case 'p':
	    portstr = optarg;
	    port = strtol(optarg, &endptr, 10);
	    if (*optarg == '\0' || *endptr != '\0')
		usage();
	    if (port < 0 || port > 65535)
		err(1, "invalid port argument (0 <= port < 65536)");
	    break;

	case 'r':
	    strlcpy(remotenick, optarg, sizeof(remotenick));
	    break;

	case 'v':
	    puts(PACKAGE_STRING "\n");
	    exit(0);

	default:
	    /* unreachable */
	    exit(1);
	}
    }

    if (optind < argc - 2)
	err(1, "not enough arguments (need host and one filename)");

    if (optind > argc - 2)
	err(1, "too many arguments (need host and one filename)");

    /* XXX: verify filename */
    s = connect_to_server(argv[optind++], portstr);

    communicate_with_server(s, argv[optind++]);

    return 0;
}
