dnl Process this file with autoconf to produce a configure script.

dnl init

AC_PREREQ(2.57)
AC_REVISION([$NiH: configure.ac,v 1.18 2003/04/07 12:20:56 wiz Exp $])
AC_INIT([dccserver],[0.3],[tk@giga.or.at])
AC_CONFIG_SRCDIR([dccserver.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

AC_CANONICAL_HOST

AC_PROG_CC

dnl header files

AC_CHECK_HEADERS([err.h poll.h sys/poll.h])

dnl libraries

POLL=none
AC_CHECK_FUNC(poll, [POLL=""])
if test "$POLL" = none
then
	AC_CHECK_LIB(poll, poll, [POLL=-lpoll])
	AC_DEFINE(USE_LIBPOLL, 1, [define if we're linking against -lpoll])
fi
if test "$POLL" = none
then
	AC_MSG_ERROR([can't find poll function; install e.g. http://www.clapper.org/software/poll/])
fi
LIBS="$LIBS $POLL"

AC_CHECK_LIB(socket, main)
AC_CHECK_LIB(nsl, main)

dnl library functions

AC_REPLACE_FUNCS([getaddrinfo err errx strlcpy warn warnx])

dnl types

AC_CHECK_TYPES(socklen_t, , , [#include <sys/socket.h>])

case $host_os
in
	*bsd*) MANFMT=mdoc;;
	*) MANFMT=man;;
esac

AC_CONFIG_LINKS([dccserver.1:dccserver.${MANFMT} dccsend.1:dccsend.${MANFMT}])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
