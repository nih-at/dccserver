/* $NiH: child.c,v 1.19 2003/05/24 23:53:49 wiz Exp $ */
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
#include "dccserver.h"

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
#include "poll.h"
#endif /* HAVE_POLL_H || HAVE_SYS_POLL_H */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_WARN
void warn(const char *, ...);
#endif

#ifndef HAVE_WARNX
void warnx(const char *, ...);
#endif

#include "dcc.h"
#include "io.h"
#include "util.h"

struct transfer_state {
    char *filename;
    int infd;
    int outfd;
    int exceed_warning_shown;
    long filesize;
    long offset;
    long rem;
    struct timeval starttime;
    struct timeval lastdata;
};

static char partner[100];

/* maximum line length accepted from remote */
#define BUFSIZE 4096

/* maximum format string length accepted from program itself */
#define FMTSIZE 1024

/* display line given from remote; filter out some characters */
/* assumes ASCII text */
void
display_remote_line(int id, const char *p)
{
    printf("<%d: %s> ", id, partner);
    while (*p) {
	if (*(unsigned char *)p > 0x7f) {
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
    fflush(stdout);
}

/* return difference between two timevals in ms */
long
timevaldiff(struct timeval *before, struct timeval *after)
{
    int sec, usec;

    usec = after->tv_usec - before->tv_usec;
    sec = after->tv_sec - before->tv_sec;
    if (before->tv_usec > after->tv_usec) {
	usec += 1000000;
	sec--;
    }

    return (long)sec * 1000 + ((long)usec + 500)/1000;
}

/* display current transfer statistics */
void
display_transfer_statistics(struct transfer_state *ts, int id)
{
    double transfer_rate;
    double size_percent;
    long time_divisor;
    long stalled;
    struct timeval after;

    gettimeofday(&after, NULL);

    time_divisor = timevaldiff(&(ts->starttime), &after);
    /* avoid division by zero in _very_ fast (or small) transfers */
    if (time_divisor == 0)
	time_divisor = 1;
    transfer_rate = (((double)(ts->filesize-ts->rem-ts->offset))/1024*1000)/time_divisor;

    if (ts->filesize > 0)
	size_percent = ((double)(ts->filesize-ts->rem))/ts->filesize*100;
    else
	size_percent = 0.0;

    warnx("child %d: %s sending `%s' (%ld of %ld bytes (%.1f%%), %ld new)",
	  id, partner, ts->filename, ts->filesize - ts->rem, ts->filesize,
	  size_percent, ts->filesize - ts->rem - ts->offset);
    stalled = timevaldiff(&(ts->lastdata), &after);
    if (stalled > STALL_TIMEOUT) {
	warnx("child %d: time %ld.%lds (stalled for %ld.%lds), transfer rate %.1fKb/s",
	      id, (time_divisor + 50)/1000, ((time_divisor + 50) / 100) % 10,
	      (stalled + 50)/1000, ((stalled + 50) / 100) % 10, transfer_rate);
    }
    else {
	warnx("child %d: time %ld.%ld seconds, transfer rate %.1fKb/s", id,
	      (time_divisor + 50)/1000, ((time_divisor + 50) / 100) % 10, transfer_rate);
    }

    return;
}

/* determine how much of filename should be transferred, and
 * intialise structure; tell remote side offset */
struct transfer_state *
setup_read_file(int fd, char *filename, long filesize)
{
    char buf[BUFSIZE];
    struct stat sb;
    struct transfer_state *ts;
    long offset;
    int out;
    int len;

    if (stat(filename, &sb) == 0) {
	/* file exists */
	if ((sb.st_mode & S_IFMT) != S_IFREG) {
	    warnx("existing directory entry for `%s' not a file", filename);
	    tell_client(fd, 151, NULL);
	    return NULL;
	}
	/* append (resume) */
	if ((sb.st_size >= 0) && (sb.st_size < filesize)) {
	    offset = sb.st_size;
	    warnx("file exists, resuming after %ld bytes", offset);
	}
	else if (sb.st_size == filesize) {
	    warnx("already have complete file, denying");
	    tell_client(fd, 151, NULL);
	    return NULL;
	}
	else {
	    warnx("already have %ld bytes, client offers only %ld, denying",
		  (long)sb.st_size, filesize);
	    tell_client(fd, 151, NULL);
	    return NULL;
	}
	if ((out=open(filename, O_WRONLY, 0644)) == -1) {
	    warn("can't open file `%s' for appending",  filename);
	    tell_client(fd, 151, NULL);
	    return NULL;
	}
	if (lseek(out, offset, SEEK_SET) != offset) {
	    warn("can't seek to offset %ld -- starting from zero", offset);
	    offset = 0;
	    if (lseek(out, offset, SEEK_SET) != offset) {
		warn("error seeking to beginning -- giving up");
		tell_client(fd, 151, NULL);
		return NULL;
	    }
	}
    }
    else {
	offset = 0;
	if ((out=open(filename, O_WRONLY|O_CREAT, 0644)) == -1) {
	    warn("can't open file `%s' for writing",  filename);
	    tell_client(fd, 151, NULL);
	    return NULL;
	}
    }

    if ((ts=malloc(sizeof(*ts))) == NULL) {
	warn("malloc failure");
	tell_client(fd, 151, NULL);
	return NULL;
    }
    if ((ts->filename=strdup(filename)) == NULL) {
	warn("strdup failure");
	tell_client(fd, 151, NULL);
	free(ts);
	return NULL;
    }
    ts->infd = fd;
    ts->outfd = out;
    ts->exceed_warning_shown = 0;
    ts->filesize = filesize;
    ts->offset = offset;
    ts->rem = filesize - offset;

    tell_client(fd, 121, "%ld", offset);

    gettimeofday(&(ts->starttime), NULL);
    ts->lastdata = ts->starttime;

    /* get any data that's in fdgets() buffer */
    while ((len=fdgets(-1, buf, sizeof(buf))) > 0) {
	if (write(ts->outfd, buf, len) < len) {
	    warn("`%s': write error", ts->filename);
	    free(ts->filename);
	    free(ts);
	    return NULL;
	}
	ts->rem -= len;
    }

    return ts;
}

/* transfer data from remote, with timeout */
int
read_file(struct transfer_state *ts)
{
    char buf[8192];
    int len;
    int timeout;
    struct timeval now;

    gettimeofday(&now, NULL);

    timeout = TRANSFER_TIMEOUT - timevaldiff(&(ts->lastdata), &now);
    if (timeout <= 0)
	timeout = MIN_TIMEOUT;
    switch (data_available(ts->infd, DIRECTION_READ, timeout)) {
    case -1:
	return -1;
    case 0:
	return -2;
    default:
	break;
    }

    if ((len=read(ts->infd, buf, sizeof(buf))) > 0) {
	if (write(ts->outfd, buf, len) < len) {
	    warn("`%s': write error", ts->filename);
	    return -1;
	}
	ts->rem -= len;

	if (ts->rem == 0)
	    len = 0;
	else if (ts->rem < 0 && ts->exceed_warning_shown == 0) {
	    ts->exceed_warning_shown = 1;
	    warnx("`%s': getting more than the expected %ld bytes",
		  ts->filename, ts->filesize);
	}
	gettimeofday(&(ts->lastdata), NULL);
    }

    return len;
}

/* display transfer statistic, close connection, and free data structure */
int
cleanup_read_file(struct transfer_state *ts, int id)
{
    int ret;

    ret = -1;

    display_transfer_statistics(ts, id);

    if (close(ts->outfd) == -1)
	warn("close error for `%s'", ts->filename);

    if (ts->rem <= 0) {
	warnx("child %d: transfer complete", id);
	ret = 0;
    }

    free(ts->filename);
    free(ts);

    return ret;

}

/* parse line from client, update state machine, and reply */
state_t
parse_client_reply(int fd, int id, state_t state, char *line, struct transfer_state **arg)
{
    state_t ret;
    char *arg2, *arg3, *endptr, *filename;
    long filesize;

    arg2 = arg3 = filename = NULL;
    ret = state;
    switch(state) {
    case ST_NONE:
	switch(parse_reply(line, partner, sizeof(partner), &arg2, &arg3)) {
	case 100:
	    tell_client(fd, 101, NULL);
	    warnx("child %d: %s wants to chat", id, partner);
	    ret = ST_CHAT;
	    break;
	case 110:
#ifdef NOT_YET
	    tell_client(fd, 111, NULL);
	    warnx("child %d: %s using fserve", id, partner);
	    ret = ST_FSERVE;
#endif
	    tell_client(fd, 151, NULL);
	    ret = ST_END;
	    break;
	case 120:
	    filesize = strtol(arg2, &endptr, 10);
	    if (*arg2 == '\0' || *endptr != '\0' || (filesize <= 0)) {
		tell_client(fd, 151, NULL);
		ret = ST_END;
		break;
	    }
	    filename = strip_path(arg3);

	    warnx("child %d: %s offers `%s' (%ld bytes)", id, partner,
		  filename, filesize);
	    fflush(stderr);
	    ret = ST_GETFILE;

	    if ((*arg=setup_read_file(fd, filename, filesize)) == NULL)
		ret = ST_END;

	    free(arg2);
	    free(arg3);
	    break;
	case 130:
#ifdef NOT_YET
	    tell_client(fd, 131, NULL);
	    ret = ST_GET;
#endif
	    tell_client(fd, 151, NULL);
	    ret = ST_END;
	    break;
	default:
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

void
child_loop(int sock, int id)
{
    char line[BUFSIZE];
    state_t state;
    struct transfer_state *ts;
    int ret;

    state = ST_NONE;

    while (state != ST_END) {
	if (sigint) {
	    if (state == ST_GETFILE)
		display_transfer_statistics(ts, id);
	    warnx("child %d: SIGINT caught -- exiting", id);
	    break;
	}

	switch (state) {
	case ST_NONE:
	    if (get_line_from_client(sock, line, sizeof(line)) < 0) {
		warnx("child %d: closing connection", id);
		state = ST_END;
	    }
	    else
		state = parse_client_reply(sock, id, state, line, &ts);
	    break;

	case ST_GETFILE:
	    if (siginfo) {
		siginfo = 0;
		display_transfer_statistics(ts, id);
	    }
	    if ((ret=read_file(ts)) <= 0) {
		if (ret == -1 && errno == EINTR)
		    continue;
		else if (ret == -2)
		    warnx("child %d: %s transfer timed out after %ds", id, partner,
			  TRANSFER_TIMEOUT/1000);
		cleanup_read_file(ts, id);
		state = ST_END;
	    }
	    break;

	case ST_CHAT:
	    if (siginfo) {
		siginfo = 0;
		warnx("child %d: chatting with %s", id, partner);
	    }
	    switch (fdgets(sock, line, sizeof(line))) {
	    case -1:
		if (errno == EINTR)
		    break;
		/* FALLTHROUGH */
	    case 0:
		/* connection closed */
		state = ST_END;
		break;
	    case -2:
		/* timeout, ignored */
		break;

	    default:
		display_remote_line(id, line);
		break;
	    }

	    break;

	default:
	    break;
	}

    }

    close(sock);
    exit(0);
}
