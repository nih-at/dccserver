/* $NiH: io.c,v 1.1 2003/05/14 09:06:01 wiz Exp $ */
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
#ifdef HAVE_POLL_H
#include <poll.h>
#elif HAVE_SYS_POLL_H
#include <sys/poll.h>
#else
#include "poll.h"
#endif /* HAVE_POLL_H || HAVE_SYS_POLL_H */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef HAVE_WARNX
void warnx(const char *, ...);
#endif

#define BUFSIZE 8192

/* XXX: ugly */
extern char nickname[];

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
