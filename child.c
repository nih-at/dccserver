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

#ifdef HAVE_ERR_H
#include <err.h>
#endif /* HAVE_ERR_H */
#include <errno.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#elif HAVE_SYS_POLL_H
#include <sys/poll.h>
#else
#warning Neither poll.h nor sys/poll.h found -- compilation will probably fail.
#warning In that case, read the included README.Darwin.
#endif /* HAVE_POLL_H || HAVE_SYS_POLL_H */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* XXX: duplicate */
typedef enum state_e { ST_NONE, ST_CHAT, ST_FSERVE, ST_SEND, ST_GET,
		       ST_GETFILE, ST_END } state_t;
extern char *nickname, *partner;
extern int parse_get_line(char *);
extern char *filename;
extern long filesize;
extern int id;

/* maximum line length accepted from remote */
#define BUFSIZE 8192

/* maximum format string length accepted from program itself */
#define FMTSIZE 1024

/* maximum number of errors before connection gets closed */
#define MAX_ERRORS   3

/* test for read/write possibility */
#define DIRECTION_READ  1
#define DIRECTION_WRITE 2

/* timeout values, ms */
#define CHAT_TIMEOUT		 15000
#define TRANSFER_TIMEOUT	120000

/* read some characters */
ssize_t
read_some(int fd, char *buf, size_t bufsize)
{
    ssize_t bytes_read;

    if (bufsize <= 0)
	return 0;
	
    if ((bytes_read=read(fd, buf, bufsize-1)) >= 0)
	buf[bytes_read] = '\0';

    return bytes_read;
}

int
tell_client(int fd, int retcode, char *fmt, ...)
{
    char buf[BUFSIZE];
    int offset;
    va_list ap;

    /* nickname can't be that long */
    offset = snprintf(buf, sizeof(buf), "%03d %s", retcode, nickname);

    if (fmt != NULL) {
	buf[offset++] = ' ';
	va_start(ap, fmt);
	offset += vsnprintf(buf+offset, sizeof(buf)-offset, fmt, ap);
	va_end(ap);
    }
    if (offset > sizeof(buf) - 2) {
	warnx("line too long to send -- aborted");
	return -1;
    }
    buf[offset++] = '\n';
    buf[offset++] = '\0';

    return write_complete(fd, CHAT_TIMEOUT, buf);
}

/* return length of string up to and including first new-line character */
ssize_t
find_nl(char *buf)
{
    char *p;
    ssize_t ret;

    ret = -1;
    if ((p=strchr(buf, '\n')) != NULL) {
	/* include the new-line character */
	ret = p - buf + 1;
    }

    return ret;
}

int
data_available(int fd, int direction, int timeout)
{
    int pollret;
    struct pollfd pollset[1];

    pollset[0].fd = fd;
    pollset[0].events = 0;
    if (direction & DIRECTION_READ)
	pollset[0].events |= POLLIN|POLLPRI;
    if (direction & DIRECTION_WRITE)
	pollset[0].events |= POLLOUT;
    pollset[0].revents = 0;

    if ((pollret=poll(pollset, 1, timeout)) == -1) {
	if (errno != EINTR)
	    warn("poll error");
    }

    return pollret;
}

int
fdgets(int fd, char *buf, int bufsize)
{
    static char intbuf[BUFSIZE+1];
    static int intbuffill = 0;
    int ret;
    int len;

    if (bufsize <= 0)
	return 0;

    while (((ret=find_nl(intbuf)) <= 0) && (intbuffill < bufsize)) {
	switch (data_available(fd, DIRECTION_READ, CHAT_TIMEOUT)) {
	case -1:
	    /* error */
	    return -1;
	case 0:
	    /* timeout */
	    return -2;
	default:
	    break;
	}
	len = read_some(fd, intbuf+intbuffill, sizeof(intbuf)-intbuffill);
	/* connection closed by remote */
	if (len == 0)
	    return 0;

	intbuffill += len;
	intbuf[intbuffill] = '\0';
    }

    /* no new-line found, but already more data available than
     * fits in the output buffer; or line too long */
    if ((ret <= 0) || (ret >= bufsize))
	ret = bufsize - 1;

    memcpy(buf, intbuf, ret);
    buf[ret] = '\0';
    memmove(intbuf, intbuf+ret, intbuffill-ret+1);
    
    return ret;
}

/* parse line from client, update state machine, and reply */
state_t
parse_client_reply(int fd, state_t state, char *line)
{
    state_t ret;

    ret = state;
    switch(state) {
    case ST_NONE:
	if (strncmp("100 ", line, 4) == 0) {
	    strlcpy(partner, line+4, sizeof(partner));
	    tell_client(fd, 101, NULL);
	    warnx("starting chat with %d: %s", id, partner);
	    ret = ST_CHAT;
	}
	else if (strncmp("110 ", line, 4) == 0) {
	    strlcpy(partner, line+4, sizeof(partner));
#ifdef NOT_YET
	    tell_client(fd, 111, NULL);
	    warnx("starting fserve session with %d: %s", id, partner);
	    ret = ST_FSERVE;
#endif
	    tell_client(fd, 151, NULL);
	    ret = ST_END;
	}
	else if (strncmp("120 ", line, 4) == 0) {
	    /* Client sending file */
	    if (parse_get_line(line) < 0) {
		tell_client(fd, 151, NULL);
		ret = ST_END;
		break;
	    }

	    warnx("getting file `%s' (%ld bytes) from %d: %s", filename,
		  filesize, id, partner);
	    fflush(stderr);

	    get_file(id, fd);
	    ret = ST_END;
	}
	else if (strncmp("130 ", line, 4) == 0) {
#ifdef NOT_YET
	    tell_client(fd, 131, NULL);
	    ret = ST_GET;
#endif
	    tell_client(fd, 151, NULL);
	    ret = ST_END;
	}
	else {
	    tell_client(fd, 151, NULL);
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
	    write_complete(fd, CHAT_TIMEOUT, "Goodbye!");
	    ret = ST_END;
	}
	break;
    case ST_GET:
	break;   

    case ST_SEND:
    case ST_END:
    default:
	    tell_client(fd, 151, NULL);
	    ret = ST_END;
    }

    return ret;
}

state_t
get_line_from_client(int sock, state_t state)
{
    char line[BUFSIZE];
    int errcount;
    int len;
    state_t ret;

    /* default timeout: 15s */
    errcount = 0;

    /* get new-line terminated line from client */
    len = fdgets(sock, line, sizeof(line));
    switch (len) {
    case -2:
	warnx("timeout after %d seconds -- closing connection", CHAT_TIMEOUT/1000);
	ret = ST_END;
	break;
    case -1:
	if (++errcount > MAX_ERRORS) {
	    warnx("%d errors in a row -- closing connection", MAX_ERRORS);
	    ret = ST_END;
	}
	break;
    default:
	ret = parse_client_reply(sock, state, line);
	break;
    }

    return ret;
}

void
child_loop(int sock)
{
    state_t state;

    state = ST_NONE;

    while (state != ST_END) {
	switch (state) {
	case ST_NONE:
	    state = get_line_from_client(sock, state);
	    break;

	case ST_GETFILE:
	    state = read_file(sock);
	    break;

	default:
	    tell_client(sock, 151, NULL);
	    break;
	}
    }

    close(sock);
    exit(0);
}


#if 0
    /* XXX: timeout checks */
    /* XXX: status display/signal handling */

    if (reading) {
	/* check for existing reply */
	if (semibufferedread(fd, buf, buf_fill, do_fgets) <= 0) {
	    /* if no reply, try to read more bytes from the network and check again */
	    switch (connectionchange(fd, DIRECTION_READ, timeout)) {
	    case -1:
		/* error */
		/* XXX */
		break;
	    case 0:
		/* timeout */
		/* XXX */
		return 0;
	    default:
		/* bytes available */
		switch(read_some(fd, buf+buf_fill, MAX_LINELEN-buf_fill)) {
		case 0:
		    /* XXX: connection closed */
		    break;
		default:
		    break;
		}
		break;
	    }
	    if (semibufferedread(fd, buf, buf_fill, do_fgets) <= 0) {
		/* XXX */
	    }
	}
	/* else, handle reply, change state */
	converse_with_client(fd, state, buf, id);
    }
    else {
	/* writing */
	/* try to write more bytes out */
	switch (connectionchange(fd, DIRECTION_WRITE, timeout)) {
	case -1:
	    /* XXX: error */
	    break;
	case 0:
	    /* XXX: timeout */
	    break;
	default:
	    write();
	    break;
	}
    }
    /* if connection closed or all done, finish */
}
#endif
