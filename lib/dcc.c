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
#include "dccserver.h"

#ifdef HAVE_ERR_H
#include <err.h>
#endif /* HAVE_ERR_H */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "dcc.h"
#include "io.h"

#ifndef HAVE_WARN
void warn(const char *, ...);
#endif

#ifndef HAVE_WARNX
void warnx(const char *, ...);
#endif

/* maximum number of errors before connection gets closed */
#define MAX_ERRORS   3

static int get_status_code(char *);

/* get line from client and warn in error cases; return < 0 on problem */
int
get_line_from_client(int sock, char *line, int linelen)
{
    int errcount;
    int ret;

    errcount = 0;

    /* get new-line terminated line from client */
    ret = fdgets(sock, line, linelen);
    switch (ret) {
    case -2:
	warnx("timeout after %ds -- closing connection", CHAT_TIMEOUT/1000);
	break;
    case -1:
	if (errno != EINTR && ++errcount > MAX_ERRORS)
	    warnx("%d errors in a row -- closing connection", MAX_ERRORS);
	break;
    default:
	if (strtok(line, "\n\r") == NULL) {
	    warn("client sent too long line");
	    ret = -1;
	}
	break;
    }

    return ret;
}

static int
get_status_code(char *line)
{
    if (strspn(line, "0123456789") != 3 || line[3] != ' ')
	return -1;

    return strtol(line, NULL, 10);
}

/* parse line into two, three, or four chunks, return value of first as integer */
int
parse_reply(char *line, char *partner, int partnersize, char **arg2, char **arg3)
{
    char *p, *q;
    int command;
    int args;
    int linelen;

    linelen = strlen(line);
    switch(command=get_status_code(line)) {
    case 100: /* CHAT */
    case 101: /* CHAT REPLY */
    case 110: /* FSERVE */
    case 111: /* FSERVE REPLY */
    case 150: /* UNAVAILABLE */
    case 151: /* REJECTED */
	args = 1;
	break;
    case 121: /* SEND REPLY */
    case 130: /* GET */
    case 131: /* GET REPLY */
    case 132: /* GET REPLY REPLY */
	args = 2;
	break;
    case 120: /* SEND */
	args = 3;
	break;
    default:
	warn("invalid command: `%s'", line);
	return -1;
    }
    if ((p=strtok(line+4, " ")) == NULL)
	return -1;
    strlcpy(partner, p, partnersize);

    if (args == 3) {
	if ((p=strtok(NULL, " ")) == NULL)
	    return -1;
	if ((*arg2=strdup(p)) == NULL)
	    return -1;
	q = p + strlen(p) + 1;
	if (q >= line + linelen)
	    return -1;
	if ((*arg3=strdup(q)) == NULL) {
	    free(*arg2);
	    *arg2 = NULL;
	    return -1;
	}
    }
    else if (args == 2)
	if ((*arg2=strdup(p)) == NULL)
	    return -1;

    return command;
}
