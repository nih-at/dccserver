dnl Process this file with autoconf to produce a configure script.

dnl init

AC_PREREQ(2.50)
AC_REVISION([$NiH: configure.ac,v 1.1 2002/10/15 17:39:41 wiz Exp $])
AC_INIT([dccserver], [0.1], [tk@giga.or.at])
AC_CONFIG_SRCDIR([dccserver.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

AC_CANONICAL_HOST

AC_PROG_CC

dnl library functions

dnl AC_REPLACE_FUNCS(getopt_long)
AC_REPLACE_FUNCS([strlcpy])

dnl case $host_os
dnl in
dnl         *bsd*) MANFMT=mdoc;;
dnl         *) MANFMT=man;;
dnl esac

dnl AC_LINK_FILES(dccserver.${MANFMT}, dccserver.1)

AC_OUTPUT(Makefile)
