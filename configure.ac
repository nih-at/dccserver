# Process this file with autoconf to produce a configure script.

# init

AC_PREREQ(2.57)
AC_REVISION([$NiH: configure.ac,v 1.23 2003/05/12 21:34:17 wiz Exp $])
AC_INIT([dccserver],[0.3b],[tk@giga.or.at])
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

AC_REPLACE_FUNCS([getaddrinfo err errx poll strlcpy warn warnx])

# types

AC_CHECK_TYPES(socklen_t, , , [#include <sys/socket.h>])

case $host_os
in
	*bsd*) MANFMT=mdoc;;
	*) MANFMT=man;;
esac

AC_CONFIG_LINKS([dccserver.1:dccserver.${MANFMT} dccsend.1:dccsend.${MANFMT}])

AC_CONFIG_FILES([Makefile lib/Makefile])
AC_OUTPUT
