bin_PROGRAMS=dccserver dccsend
man_MANS=dccserver.1 dccsend.1

LDADD=@LIBOBJS@

dccserver_SOURCES=dccserver.c io.c
dccsend_SOURCES=dccsend.c

DISTCLEANFILES=	dccserver.1 dccsend.1

EXTRA_DIST=	dccserver.man dccserver.mdoc \
		dccsend.man dccsend.mdoc \
		README.Darwin \
		debian/changelog debian/control debian/copyright debian/dirs \
		debian/docs debian/rules \
		err.c errx.c getaddrinfo.c getaddrinfo.h strlcpy.c warn.c warnx.c

AUTOMAKE_OPTIONS=	dist-bzip2

install-exec-hook:
	@(chown root $(bindir)/dccserver && \
		chmod u+s $(bindir)/dccserver && \
		echo NOTE: dccserver has been installed setuid root.) || \
	echo dccserver NOT installed setuid root.
