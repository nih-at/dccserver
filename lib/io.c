/* $NiH: io.c,v 1.3 2003/05/14 09:45:11 wiz Exp $ */
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
#include "io.h"

#ifdef HAVE_ERR_H
#include <err.h>
#endif
#include <errno.h>
#ifdef HAVE_POLL
#include <poll.h>
#else
#include "pollemu.h"
#endif /* HAVE_POLL */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef HAVE_WARNX
void warnx(const char *, ...);
#endif

#define BUFSIZE 8192

/* XXX: ugly */
extern char nickname[];

static ssize_t find_nl(char *);
static ssize_t read_some(int, char *, size_t);

int
data_available(int fd, int direction, int timeout)
{
    struct pollfd pollset[1];

    pollset[0].fd = fd;
    pollset[0].events = 0;
    if (direction & DIRECTION_READ)
	pollset[0].events |= POLLIN|POLLPRI;
    if (direction & DIRECTION_WRITE)
	pollset[0].events |= POLLOUT;
    pollset[0].revents = 0;

    return poll(pollset, 1, timeout);
}

/* get a new-line-terminated line from fd */
int
fdgets(int fd, char *buf, int bufsize)
{
    static char intbuf[BUFSIZE+1];
    static int intbuffill = 0;
    int ret;
    int len;

    if (bufsize <= 0)
	return 0;

    buf[0] = '\0';

    /* get remaining bytes, even if not a complete line */
    if (fd == -1) {
	if ((ret=intbuffill) >= bufsize)
	    ret = bufsize - 1;

	memcpy(buf, intbuf, ret);
	buf[ret] = '\0';
	intbuffill -= ret;
	memmove(intbuf, intbuf+ret, intbuffill+1);

	return ret;
    }

    while (((ret=find_nl(intbuf)) <= 0) && (intbuffill < bufsize)) {
	switch (data_available(fd, DIRECTION_READ, CHAT_TIMEOUT)) {
	case -1:
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
    intbuffill -= ret;
    memmove(intbuf, intbuf+ret, intbuffill+1);

    return ret;
}

/* return length of string up to and including first new-line character */
static ssize_t
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

/* read characters, NUL-terminate buffer */
static ssize_t
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


/* write buf, don't accept partial writes */
ssize_t
write_complete(int fd, int timeout, char *buf)
{
    int len, written;

    len = strlen(buf);
    while (len > 0) {
	switch (data_available(fd, DIRECTION_WRITE, timeout)) {
	case -1:
	    /* ignore interrupts here */
	    if (errno == EINTR)
		continue;
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
