/* $NiH: child.c,v 1.12 2003/05/12 18:44:03 wiz Exp $ */
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

#include "dccserver.h"

#include <sys/stat.h>
#include <sys/time.h>

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

/* maximum number of errors before connection gets closed */
#define MAX_ERRORS   3

/* test for read/write possibility */
#define DIRECTION_READ  1
#define DIRECTION_WRITE 2

/* timeout values (ms) */
#define CHAT_TIMEOUT		 15000
#define TRANSFER_TIMEOUT	120000
#define STALL_TIMEOUT		  5000
#define MIN_TIMEOUT		   300

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

/* read characters, NUL-terminate buffer */
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

/* some fserves incorrectly include the complete path -- */
/* strip it off */
char *
strip_path(char *p)
{
    char *q;

    if ((q=strrchr(p, '/')) != NULL)
	p = ++q;
    if ((q=strrchr(p, '\\')) != NULL)
	p = ++q;

    return p;
}


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

/* parse GET line given by remote client */
int
parse_get_line(char *line, char **filename, long *filesize)
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

    *filesize = strtol(q, &endptr, 10);
    if (*q == '\0' || *endptr != '\0' || (*filesize <=0))
	return -1;

    /* remove path components in file name */
    q = strip_path(p+1);
    if ((*filename=strdup(q)) == NULL)
	return -1;

    return 0;
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

	if (ts->rem < 0 && ts->exceed_warning_shown == 0) {
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
parse_client_reply(int fd, int id, state_t state, char *line, void **arg)
{
    state_t ret;

    ret = state;
    switch(state) {
    case ST_NONE:
	if (strncmp("100 ", line, 4) == 0) {
	    strlcpy(partner, line+4, sizeof(partner));
	    tell_client(fd, 101, NULL);
	    warnx("child %d: %s wants to chat", id, partner);
	    ret = ST_CHAT;
	}
	else if (strncmp("110 ", line, 4) == 0) {
	    strlcpy(partner, line+4, sizeof(partner));
#ifdef NOT_YET
	    tell_client(fd, 111, NULL);
	    warnx("child %d: %s using fserve", id, partner);
	    ret = ST_FSERVE;
#endif
	    tell_client(fd, 151, NULL);
	    ret = ST_END;
	}
	else if (strncmp("120 ", line, 4) == 0) {
	    char *filename = NULL;
	    long filesize;

	    /* Client sending file */
	    if (parse_get_line(line, &filename, &filesize) < 0) {
		tell_client(fd, 151, NULL);
		ret = ST_END;
		break;
	    }

	    warnx("child %d: %s offers `%s' (%ld bytes)", id, partner,
		  filename, filesize);
	    fflush(stderr);
	    ret = ST_GETFILE;

	    if ((*arg=setup_read_file(fd, filename, filesize)) == NULL)
		ret = ST_END;

	    free(filename);
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
get_line_from_client(int sock, int id, state_t state, void **arg)
{
    char line[BUFSIZE];
    int errcount;
    int len;
    state_t ret;

    errcount = 0;
    ret = state;

    /* get new-line terminated line from client */
    len = fdgets(sock, line, sizeof(line));
    switch (len) {
    case -2:
	warnx("timeout after %ds -- closing connection", CHAT_TIMEOUT/1000);
	ret = ST_END;
	break;
    case -1:
	if (errno != EINTR && ++errcount > MAX_ERRORS) {
	    warnx("child %d: %s: %d errors in a row -- closing connection", id,
		  partner, MAX_ERRORS);
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
	ret = parse_client_reply(sock, id, state, line, arg);
	break;
    }

    return ret;
}

void
child_loop(int sock, int id)
{
    char line[BUFSIZE];
    state_t state;
    void *arg;
    int ret;

    state = ST_NONE;

    while (state != ST_END) {
	if (sigint) {
	    if (state == ST_GETFILE)
		display_transfer_statistics((struct transfer_state *)arg, id);
	    warnx("child %d: SIGINT caught -- exiting", id);
	    break;
	}

	switch (state) {
	case ST_NONE:
	    state = get_line_from_client(sock, id, state, &arg);
	    break;

	case ST_GETFILE:
	    if (siginfo) {
		siginfo = 0;
		display_transfer_statistics((struct transfer_state *)arg, id);
	    }
	    if ((ret=read_file((struct transfer_state *)arg)) <= 0) {
		if (ret == -1 && errno == EINTR)
		    continue;
		else if (ret == -2)
		    warnx("child %d: %s transfer timed out after %ds", id, partner,
			  TRANSFER_TIMEOUT/1000);
		cleanup_read_file((struct transfer_state *)arg, id);
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
