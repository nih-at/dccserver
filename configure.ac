dnl Process this file with autoconf to produce a configure script.

dnl init

AC_PREREQ(2.57)
AC_REVISION([$NiH: configure.ac,v 1.11 2003/01/22 16:54:48 wiz Exp $])
AC_INIT([dccserver],[0.2],[tk@giga.or.at])
AC_CONFIG_SRCDIR([dccserver.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

AC_CANONICAL_HOST

AC_PROG_CC

dnl header files

AC_CHECK_HEADERS(err.h)

dnl libraries

AC_CHECK_LIB(socket, main)
AC_CHECK_LIB(nsl, main)

dnl library functions

dnl AC_REPLACE_FUNCS(getopt_long)
AC_REPLACE_FUNCS([err strlcpy warn warnx])

AC_CHECK_FUNCS([seteuid])

dnl types

AC_CHECK_TYPES(socklen_t, , , [#include <sys/socket.h>])

case $host_os
in
	*bsd*) MANFMT=mdoc;;
	*) MANFMT=man;;
esac

AC_CONFIG_LINKS([dccserver.1:dccserver.${MANFMT}])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
