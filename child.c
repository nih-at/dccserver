/* $NiH: io.c,v 1.2 2003/05/10 20:49:40 wiz Exp $ */
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

#include <sys/stat.h>
#include <sys/time.h>
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
#warning Neither poll.h nor sys/poll.h found -- compilation will probably fail.
#warning In that case, read the included README.Darwin.
#endif /* HAVE_POLL_H || HAVE_SYS_POLL_H */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* XXX: duplicate */
typedef enum state_e { ST_NONE, ST_CHAT, ST_FSERVE, ST_SEND, ST_GET,
		       ST_GETFILE, ST_END } state_t;
extern char nickname[];
char partner[100];
extern void display_remote_line(int, const unsigned char *);
extern int parse_get_line(char *);
extern char *strip_path(char *);
char filename[1024];
long filesize;

/* maximum line length accepted from remote */
#define BUFSIZE 4096

/* maximum format string length accepted from program itself */
#define FMTSIZE 1024

/* maximum number of errors before connection gets closed */
#define MAX_ERRORS   3

/* test for read/write possibility */
#define DIRECTION_READ  1
#define DIRECTION_WRITE 2

/* timeout values, ms */
#define CHAT_TIMEOUT		 150000
#define TRANSFER_TIMEOUT	1200000


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

ssize_t
write_complete(int fd, int timeout, char *buf)
{
    int len, written;

    warnx("write_complete ``%s''", buf);
    len = strlen(buf);
    while (len > 0) {
	switch (data_available(fd, DIRECTION_WRITE, timeout)) {
	case -1:
	    /* error */
	    return -1;
	case 0:
	    /* timeout */
	    return -2;
	default:
	    break;
	}
	if ((written=write(fd, buf, len)) <= 0)
	    return written;

	buf += written;
	len -= written;
    }

    return 1;
}

int
tell_client(int fd, int retcode, char *fmt, ...)
{
    char buf[BUFSIZE];
    int offset;
    va_list ap;

    warnx("tell_client");
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

/* parse GET line given by remote client */
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

int
read_file(int fd, int id)
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

    warnx("read_file");
    if (stat(filename, &sb) == 0) {
	/* file exists */
	if ((sb.st_mode & S_IFMT) != S_IFREG) {
	    warnx("existing directory entry for `%s' not a file", filename);
	    tell_client(fd, 151, NULL);
	    return -1;
	}
	/* append (resume) */
	if ((sb.st_size >= 0) && (sb.st_size < filesize)) {
	    offset = sb.st_size;
	    warnx("file exists, resuming after %ld bytes", offset);
	}
	else if (sb.st_size == filesize) {
	    warnx("already have complete file, denying");
	    tell_client(fd, 151, NULL);
	    return -1;
	}
	else {
	    warnx("already have more bytes than client willing to send, denying");
	    tell_client(fd, 151, NULL);
	    return -1;
	}
	if ((out=open(filename, O_WRONLY, 0644)) == -1) {
	    warn("can't open file `%s' for appending",  filename);
	    tell_client(fd, 151, NULL);
	    return -1;
	}
    }
    else {
	offset = 0;
	if ((out=open(filename, O_WRONLY|O_CREAT, 0644)) == -1) {
	    warn("can't open file `%s' for writing",  filename);
	    tell_client(fd, 151, NULL);
	    return -1;
	}
    }

    if (lseek(out, offset, SEEK_SET) != offset) {
	warn("can't seek to offset %ld", offset);
	tell_client(fd, 151, NULL);
    }

    tell_client(fd, 121, "%ld", offset);

    gettimeofday(&before, NULL);

    /* get file data into local file */
    rem = filesize - offset;
    while (data_available(fd, DIRECTION_READ, TRANSFER_TIMEOUT) > 0
	   && (len=read(fd, buf, sizeof(buf))) > 0) {
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
fdgets(int fd, char *buf, int bufsize)
{
    static char intbuf[BUFSIZE+1];
    static int intbuffill = 0;
    int ret;
    int len;

    warnx("fdgets");
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
parse_client_reply(int fd, int id, state_t state, char *line)
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

	    read_file(fd, id);
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
get_line_from_client(int sock, int id, state_t state)
{
    char line[BUFSIZE];
    int errcount;
    int len;
    state_t ret;

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
	if (strtok(line, "\n\r") == NULL) {
	    warn("client sent too long line");
	    tell_client(sock, 151, NULL);
	    ret = ST_END;
	    break;
	}
	ret = parse_client_reply(sock, id, state, line);
	break;
    }

    return ret;
}

void
child_loop(int sock, int id)
{
    state_t state;

    state = ST_NONE;

    while (state != ST_END) {
	switch (state) {
	case ST_NONE:
	    state = get_line_from_client(sock, id, state);
	    break;

	case ST_GETFILE:
	    state = read_file(sock, id);
	    break;

	default:
	    tell_client(sock, 151, NULL);
	    break;
	}
    }

    close(sock);
    exit(0);
}
