# Process this file with autoconf to produce a configure script.

# init

AC_PREREQ(2.57)
AC_REVISION([$NiH: configure.ac,v 1.27 2003/09/08 09:51:31 wiz Exp $])
AC_INIT([dccserver],[0.4],[wiz@danbala.tuwien.ac.at])
AC_CONFIG_SRCDIR([dccserver.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_RANLIB

# header files

AC_CHECK_HEADERS(err.h)

# libraries

AC_CONFIG_LIBOBJ_DIR(lib)

AC_CHECK_LIB(socket, main)
AC_CHECK_LIB(nsl, main)

# library functions

AC_MSG_CHECKING([checking for getaddrinfo])
AC_TRY_LINK([#include <netdb.h>], [getaddrinfo(0,0,0,0);],
[AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_GETADDRINFO], [1], [Define to 1 if you have the `getaddrinfo' function.])],
[AC_MSG_RESULT([no]); AC_LIBOBJ(getaddrinfo)])
AC_REPLACE_FUNCS([err errx poll strlcpy warn warnx])

# types

AC_CHECK_TYPES(socklen_t, , , [#include <sys/types.h>
#include <sys/socket.h>])

case $host_os
in
	*bsd*) MANFMT=mdoc;;
	*) MANFMT=man;;
esac

AC_CONFIG_LINKS([dccserver.1:dccserver.${MANFMT} dccsend.1:dccsend.${MANFMT}])

AC_CONFIG_FILES([Makefile lib/Makefile])
AC_OUTPUT
