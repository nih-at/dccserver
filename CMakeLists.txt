PROJECT(dccserver)

INCLUDE(CheckFunctionExists)
INCLUDE(CheckIncludeFiles)

SET(PACKAGE_NAME "dccserver")
SET(PACKAGE_VERSION "0.4a")
SET(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")

CHECK_INCLUDE_FILES(err.h HAVE_ERR_H)

CHECK_FUNCTION_EXISTS(getaddrinfo HAVE_GETADDRINFO)
IF(NOT HAVE_GETADDRINFO)
  CHECK_LIBRARY_EXISTS(socket getaddrinfo "" CMAKE_LIB_SOCKET_HAS_GETADDRINFO)
  IF(CMAKE_LIB_SOCKET_HAS_GETADDRINFO)
    SET(EXTRA_LIBS -lsocket)
  ELSE(CMAKE_LIB_SOCKET_HAS_GETADDRINFO)
    SET(EXTRA_LIB_FILES ${EXTRA_LIB_FILES} getaddrinfo.c)
  ENDIF(CMAKE_LIB_SOCKET_HAS_GETADDRINFO)
ENDIF(NOT HAVE_GETADDRINFO)

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/cmake-config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/config.h)

ADD_SUBDIRECTORY(lib) 

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/lib
  ${CMAKE_CURRENT_BINARY_DIR})

IF(CMAKE_SYSTEM_NAME MATCHES BSD)
  SET(MANFMT mdoc)
ELSE(CMAKE_SYSTEM_NAME MATCHES BSD)
  SET(MANFMT man)
ENDIF(CMAKE_SYSTEM_NAME MATCHES BSD)

SET(MAN_PATH man)
SET(PROGRAMS dccserver dccsend) 

FOREACH(MAN_PAGE ${PROGRAMS})
  INSTALL(FILES ${MAN_PAGE}.${MANFMT} DESTINATION ${MAN_PATH}/man1
    RENAME ${MAN_PAGE}.1)
ENDFOREACH(MAN_PAGE ${PROGRAMS})

SET(MAN_PAGES dccserver dccsend) 
FOREACH(MAN ${MAN_PAGES})
  # XXX: when I add "OUTPUT" as first argument, the target is called "OUTPUT"
  # and ${MAN}.man is the first COMMAND line in it. Seems to contradict
  # documentation.
  ADD_CUSTOM_TARGET(${MAN}.man
    DEPENDS ${MAN}.mdoc
    COMMAND mdoc2man ${MAN}.mdoc > ${MAN}.new
    COMMAND diff -I NiH ${MAN}.new ${MAN}.man || mv ${MAN}.new ${MAN}.man
    COMMAND rm -f ${MAN}.new
    COMMENT Updating man page ${MAN}.man
    )
  ADD_CUSTOM_TARGET(${MAN}.html
    DEPENDS ${MAN}.mdoc
    COMMAND nroff -mdoc2html ${MAN}.mdoc | sed -e "s,../html[0-9]/,," > ${MAN}.html
    COMMENT Updating HTML page ${MAN}.html
    )
ENDFOREACH(MAN ${MAN_PAGES})

ADD_CUSTOM_TARGET(update-man
  DEPENDS dccserver.man dccsend.man)

ADD_CUSTOM_TARGET(update-html
  DEPENDS dccserver.html dccsend.html)

########### next target ###############

SET(dccserver_SRCS
  dccserver.c
  child.c
)

ADD_EXECUTABLE(dccserver ${dccserver_SRCS})

TARGET_LINK_LIBRARIES(dccserver dcc)

# XXX: actually, should only be setuid if for root, otherwise there is
# no point
INSTALL(PROGRAMS dccserver DESTINATION bin
  PERMISSIONS OWNER_READ OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE SETUID)

########### next target ###############

SET(dccsend_SRCS
  dccsend.c
)

ADD_EXECUTABLE(dccsend ${dccsend_SRCS})

TARGET_LINK_LIBRARIES(dccsend dcc)

INSTALL(PROGRAMS dccsend DESTINATION bin)

########### install files ###############

#noinst_HEADERS=	dccserver.h
#
#EXTRA_DIST=	dccserver.man dccserver.mdoc \
#		dccsend.man dccsend.mdoc \
#		contrib/dccsend.pl \
#		debian/changelog debian/control debian/copyright debian/dirs \
#		debian/docs debian/rules
