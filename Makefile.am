bin_PROGRAMS=dccserver
man_MANS=dccserver.1

LDADD=@LIBOBJS@

dccserver_SOURCES=dccserver.c

DISTCLEANFILES=	dccserver.1

EXTRA_DIST=	dccserver.man dccserver.mdoc README \
		debian/changelog debian/control debian/copyright debian/dirs \
		debian/docs debian/rules \
		err.c strlcpy.c warn.c warnx.c

AUTOMAKE_OPTIONS=	dist-bzip2

install-exec-hook:
	chown root $(bindir)/dccserver
	chmod u+s $(bindir)/dccserver
	@echo dccserver has been installed setuid root.
