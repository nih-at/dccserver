bin_PROGRAMS=dccserver
man_MANS=dccserver.1

LDADD=@LIBOBJS@

dccserver_SOURCES=dccserver.c

DISTCLEANFILES=	dccserver.1

EXTRA_DIST=	dccserver.man dccserver.mdoc README \
		err.c snprintf.c strlcpy.c warn.c warnx.c

AUTOMAKE_OPTIONS=	dist-bzip2
