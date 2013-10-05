MANFMT=@MANFMT@

bin_PROGRAMS=dccserver dccsend
man1_MANS=dccserver.${MANFMT} dccsend.${MANFMT}

LDADD=		lib/libdcc.a
AM_CPPFLAGS=	-I${top_srcdir}/lib

dccserver_SOURCES=dccserver.c child.c
dccsend_SOURCES=dccsend.c

noinst_HEADERS=	dccserver.h

EXTRA_DIST=	dccserver.man dccserver.mdoc \
		dccsend.man dccsend.mdoc \
		contrib/dccsend.pl \
		CMakeLists.txt lib/CMakeLists.txt \
		cmake-config.h.in \
		debian/changelog debian/control debian/copyright debian/dirs \
		debian/docs debian/rules \
		socklen_t.c

AUTOMAKE_OPTIONS=	dist-bzip2

SUBDIRS=	lib

install-exec-hook:
	@(chown root $(DESTDIR)$(bindir)/dccserver && \
		chmod u+s $(DESTDIR)$(bindir)/dccserver && \
		echo NOTE: dccserver has been installed setuid root.) || \
	echo dccserver NOT installed setuid root.

SUFFIXES=.man .mdoc .html
MDOCPAGES=dccsend.mdoc dccserver.mdoc
MANPAGES=${MDOCPAGES:.mdoc=.man}

.mdoc.man:
	mandoc -T man $< | sed '/TH/s,"NetBSD [^"]*","NiH",' > $@.$$$$ && diff -I NiH $@.$$$$ $@ || mv $@.$$$$ $@; \
	rm -f $@.$$$$

.mdoc.html:
	mandoc -Thtml -Oman="%N.html",style=../nih-man.css $< > $@.$$$$ && mv $@.$$$$ $@

update-man: ${MANPAGES}
