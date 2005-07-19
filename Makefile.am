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
		debian/changelog debian/control debian/copyright debian/dirs \
		debian/docs debian/rules

AUTOMAKE_OPTIONS=	dist-bzip2

SUBDIRS=	lib

install-exec-hook:
	@(chown root $(bindir)/dccserver && \
		chmod u+s $(bindir)/dccserver && \
		echo NOTE: dccserver has been installed setuid root.) || \
	echo dccserver NOT installed setuid root.

SUFFIXES=.man .mdoc .html
MDOCPAGES=dccsend.mdoc dccserver.mdoc
MANPAGES=${MDOCPAGES:.mdoc=.man}

.mdoc.man:
	mdoc2man $< > $@.$$$$ && diff -I NiH $@.$$$$ $@ || mv $@.$$$$ $@; \
	rm -f $@.$$$$

.mdoc.html:
	nroff -mdoc2html $< | sed -e "s,../html1/,," > $@.$$$$ && mv $@.$$$$ $@

update-man: ${MANPAGES}
