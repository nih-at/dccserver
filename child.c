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

#include <errno.h>
#include <string.h>
#include <unistd.h>

/* maximum line length accepted from remote */
#define MAX_LINE_LEN 1200

/*
 * Find a line in the srclen characters starting at src,
 * and copy them to dst; if the line is longer than dstlen,
 * only copy the beginning.
 * Usually dstlen should be larger than srclen.
 */
ssize_t
find_and_copy(char *dst, size_t dstsize, char *src, size_t srcsize)
{
    char *p;
    ssize_t copylen;

    copylen = -1;
    /* NUL-terminate buffer so that strchr doesn't run amok */
    src[srcsize] = '\0';
    if ((p=strchr(src, '\n')) != NULL) {
	/* include the new-line character */
	copylen = p - src + 1;

	/* leave space for terminating zero */
	if (copylen > dstsize - 1)
	    copylen = dstsize - 1;
	memcpy(dst, src, copylen);
	
	dst[copylen] = 0;
    }

    return copylen;
}

/*
 * backend for my_read and my_fread.
 * read from fd;
 * if do_fread is set, return one line (same as fgets);
 * otherwise, as long as there is data in the local buffer,
 * return as much as fits into buf; once it's empty, just call
 * read directly.
 */
ssize_t
semibufferedread(int fd, void *buf, size_t buflen, int do_fgets)
{
    static char buf_internal[MAX_LINE_LEN];
    static int buf_start=0, buf_end=0;
    /* assumes we get only called with one fd */
    static int connection_closed;
    ssize_t copylen;
    ssize_t bytes_read;

    /* XXX: if safed_fd != fd, reset static variables */

    if ((fd < 0) || (buf == NULL))
	return -1;

    if (buflen == 0)
	return 0;

    if (do_fgets) {
	buf_internal[buf_end] = '\0';
	if ((copylen=find_and_copy(buf, buflen,
				   buf_internal+buf_start,
				   buf_end-buf_start)) >= 0) {
	    buf_start += copylen;
	    return copylen;
	}

	/* read more and try again */
	if (!connection_closed &&
	    (bytes_read=read(fd, buf_internal+buf_start,
			     MAX_LINE_LEN-buf_end)) > 0) {
	    buf_internal[buf_end] = '\0';
	    if ((copylen=find_and_copy(buf, buflen,
				       buf_internal+buf_start,
				       buf_end-buf_start)) >= 0) {
		buf_start += copylen;
		return copylen;
	    }
	}
	else {
	    if (!connection_closed) {
		if (bytes_read == -1
		    && (errno == EINTR || errno == EAGAIN))
		    return -1;

		if (bytes_read == 0)
		    connection_closed = 1;
	    }

	    /* just return remaining, that's all we can do now */
	    if ((copylen=buf_end-buf_start) > buflen)
		copylen = buflen;

	    memcpy(buf, buf_internal + buf_start, copylen);
	    buf_start += copylen;

	    return copylen;
	}
    }
    else {
	/* return remaining bytes */
	if (buf_end - buf_start > 0) {
	    /* no trailing zero here, assumed binary */
	    if ((copylen=buf_end-buf_start) > buflen)
		copylen = buflen;

	    memcpy(buf, buf_internal + buf_start, copylen);
	    buf_start += copylen;

	    return copylen;
	}

	/* just read(2) */
	return read(fd, buf, buflen);
    }

    return -1;
}
