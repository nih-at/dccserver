/* $NiH: dccserver.h,v 1.3 2003/05/12 18:44:03 wiz Exp $ */
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
#include <fcntl.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#elif HAVE_SYS_POLL_H
#include <sys/poll.h>
#else
#include "poll.h"
#endif /* HAVE_POLL_H || HAVE_SYS_POLL_H */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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


/* typedefs */

typedef enum state_e { ST_NONE, ST_CHAT, ST_FSERVE, ST_SEND, ST_GET,
		       ST_GETFILE, ST_END } state_t;

/* global variables */
/* for handling signals */
extern volatile sig_atomic_t sigchld;
extern volatile sig_atomic_t siginfo;
extern volatile sig_atomic_t sigint;

/* option arguments/toggles */
extern int filter_control_chars;
extern char nickname[];


/* in child.c */
extern void child_loop(int sock, int id);
