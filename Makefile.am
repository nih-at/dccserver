bin_PROGRAMS=dccserver dccsend
man_MANS=dccserver.1 dccsend.1

LDADD=		lib/libdcc.a
AM_CPPFLAGS=	-Ilib

dccserver_SOURCES=dccserver.c child.c
dccsend_SOURCES=dccsend.c

noinst_HEADERS=	dccserver.h

DISTCLEANFILES=	dccserver.1 dccsend.1

EXTRA_DIST=	dccserver.man dccserver.mdoc \
		dccsend.man dccsend.mdoc \
		debian/changelog debian/control debian/copyright debian/dirs \
		debian/docs debian/rules

AUTOMAKE_OPTIONS=	dist-bzip2

SUBDIRS=	lib

install-exec-hook:
	@(chown root $(bindir)/dccserver && \
		chmod u+s $(bindir)/dccserver && \
		echo NOTE: dccserver has been installed setuid root.) || \
	echo dccserver NOT installed setuid root.
