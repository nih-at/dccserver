/* $NiH: dccsend.c,v 1.14 2003/05/11 02:39:38 wiz Exp $ */
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
#ifdef HAVE_ERR_H
#include <err.h>
#endif /* HAVE_ERR_H */
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HAVE_GETADDRINFO
#include "getaddrinfo.h"
#endif

#include "dccserver.h"
#include "dcc.h"
#include "io.h"
#include "util.h"

#ifndef HAVE_ERR
void err(int, const char *, ...);
#endif

#ifndef HAVE_ERRX
void errx(int, const char *, ...);
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

#define BACKLOG 10

static long offset;
static char remotenick[100];

char nickname[1024];

int
connect_to_server(char *host, int port)
{
    struct addrinfo hints, *res0, *res;
    int error, s;
    char *cause = NULL;
    char portstr[10];

    if (port < 0 || port > 65535)
	return -1;
    sprintf(portstr, "%d", port);
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    if ((error=getaddrinfo(host, portstr, &hints, &res0)) != 0) {
	warnx("cannot get host ``%s'' port %d: %s",
	      host, port, gai_strerror(error));
	return -1;
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
	warn("cannot %s", cause);

    freeaddrinfo(res0);

    return s;
}

/* send file to network */
int
send_file(int out, char *filename, long filesize)
{
    char buf[8192];
    int len;
    long rem;
    int in;

    if (filesize < offset) {
	warnx("remote requested part after EOF (%ld < %ld)", filesize, offset);
	return -1;
    }

    if ((in=open(filename, O_RDONLY, 0)) < 0) {
	warn("can't open file %s", filename);
	return -1;
    }

    if (offset > 0) {
	if (lseek(in, offset, SEEK_SET) != 0) {
	    warn("can't seek to %ld", offset);
	    close(in);
	    return -1;
	}
    }

    /* write file data to network */
    rem = filesize - offset;
    while((len=read(in, buf, sizeof(buf))) > 0) {
	if (write(out, buf, len) < len) {
	    warn("write error on network");
	    close(in);
	    return -1;
	}
	rem -= len;
    }

    (void)close(in);

    if (rem <= 0) {
	warnx("sending `%s' to %s complete: %ld/%ld bytes sent, %ld new",
	      filename, remotenick, filesize-rem, filesize, filesize-rem-offset);
	return 0;
    }
    else {
	warnx("%s closed connection for `%s' (%ld/%ld bytes sent, "
	      "%ld new)", remotenick, filename, filesize-rem, filesize,
	      filesize-offset-rem);
    }

    return -1;
}

/* parse line given by remote client */
int
parse_send_line(char *line)
{
    char *p, *endptr;

    if ((p=strchr(line+4, ' ')) == NULL)
	return -1;
    *p = '\0';
    strlcpy(remotenick, line+4, sizeof(remotenick));
    *p++ = ' ';

    offset = strtol(p, &endptr, 10);
    if (*p == '\0' || (*endptr != '\0' && *endptr != '\n'
		       && *endptr != '\r') || (offset <0))
	return -1;

    return 0;
}    

/* main child routine: read line from client and call parser */
void
communicate_with_server(int fd, char *filename, long filesize)
{
    char buf[8192];
    state_t state;

    state = ST_NONE;

    tell_client(fd, 120, "%ld %s", filesize, strip_path(filename));
    if (get_line_from_client(fd, buf, sizeof(buf)) < 0) {
	warnx("closing connection");
	(void)close(fd);
	return;
    }

    if (strncmp("121 ", buf, 4) == 0) {
	/* accepted */
	if (parse_send_line(buf) < 0) {
	    warnx("invalid reply: %s", buf);
	}
	else /* XXX: verify remote user */
	    send_file(fd, filename, filesize);
    }
    else if (strncmp("150 ", buf, 4) == 0)
	warnx("remote unavailable");
    else if (strncmp("151 ", buf, 4) == 0)
	warnx("remote denied");
    else
	warnx("invalid reply: %s", buf);

    (void)close(fd);
    return;
}

void
usage(const char *prg)
{

    fprintf(stderr, "%s: send a file to a MIRC /dccserver\n\n"
	    "Usage:\n"
	    "%s [-hv] [-n nickname] [-p port] [-r remotenick] host filename\n"
	    "Where nickname is the own nick that should be used (default: 'dccserver'),\n"
	    "port is the port on which the remote dccserver listens (default: 59),\n"
	    "and remotenick is the expected remote nickname on the server (default: accept any);\n"
	    "host is the internet hostname to connect to, and filename is the file to send.\n",
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
    int c;
    long port;
    struct stat sb;
    int s;

    strlcpy(nickname, "dccsend", sizeof(nickname));
    port = 59;

    while ((c=getopt(argc, argv, "hn:p:r:v")) != -1) {
	switch(c) {
	case 'h':
	    usage(argv[0]);

	case 'n':
	    strlcpy(nickname, optarg, sizeof(nickname));
	    break;

	case 'p':
	    port = strtol(optarg, &endptr, 10);
	    if (*optarg == '\0' || *endptr != '\0')
		usage(argv[0]);
	    if (port < 0 || port > 65535)
		err(1, "invalid port argument (0 <= port < 65536)");
	    break;

	case 'r':
	    strlcpy(remotenick, optarg, sizeof(remotenick));
	    break;

	case 'v':
	    puts("dccsend " PACKAGE_VERSION);
	    exit(0);

	default:
	    /* unreachable */
	    exit(1);
	}
    }

    if (optind > argc - 2)
	errx(1, "not enough arguments (need host and one filename)");

    if (optind < argc - 2)
	errx(1, "too many arguments (need host and one filename)");

    /* verify filename */
    if (stat(argv[optind+1], &sb) == 0) {
	/* file exists */
	if ((sb.st_mode & S_IFMT) != S_IFREG)
	    errx(1, "not a regular file: %s", argv[optind+1]);
    }
    else
	errx(1, "file does not exist: %s", argv[optind+1]);

    s = connect_to_server(argv[optind++], port);

    if (s < 0)
	return 1;

    communicate_with_server(s, argv[optind++], sb.st_size);

    return 0;
}
