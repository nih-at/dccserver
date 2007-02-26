PROJECT(dccserver)

INCLUDE(CheckFunctionExists)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckSymbolExists)

SET(PACKAGE_NAME "dccserver")
SET(PACKAGE_VERSION "0.4b")
SET(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")

ADD_SUBDIRECTORY(lib) 

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/lib
  ${CMAKE_CURRENT_BINARY_DIR})

# Targets

## dccserver

ADD_EXECUTABLE(dccserver dccserver.c child.c)
TARGET_LINK_LIBRARIES(dccserver dcc)

## dccsend

ADD_EXECUTABLE(dccsend dccsend.c)
TARGET_LINK_LIBRARIES(dccsend dcc)

# installation

SET(MAN_PATH man)
SET(PROGRAMS dccserver dccsend) 
FOREACH(MAN_PAGE ${PROGRAMS})
  INSTALL(FILES ${MAN_PAGE}.${MANFMT} DESTINATION ${MAN_PATH}/man1
    RENAME ${MAN_PAGE}.1)
ENDFOREACH(MAN_PAGE ${PROGRAMS})

INSTALL(PROGRAMS dccsend DESTINATION bin)

# XXX: actually, should only be setuid if for root, otherwise there is
# no point
INSTALL(PROGRAMS dccserver DESTINATION bin
  PERMISSIONS OWNER_READ OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE SETUID)

# Checks

CHECK_INCLUDE_FILES(err.h HAVE_ERR_H)

CHECK_FUNCTION_EXISTS(getaddrinfo HAVE_GETADDRINFO)
IF(NOT HAVE_GETADDRINFO)
  CHECK_LIBRARY_EXISTS(socket getaddrinfo "" CMAKE_LIB_SOCKET_HAS_GETADDRINFO)
  IF(CMAKE_LIB_SOCKET_HAS_GETADDRINFO)
    SET(HAVE_GETADDRINFO 1)
    TARGET_LINK_LIBRARIES(dccsend socket)
    TARGET_LINK_LIBRARIES(dccserver socket)
  ELSE(CMAKE_LIB_SOCKET_HAS_GETADDRINFO)
    SET(EXTRA_LIB_FILES ${EXTRA_LIB_FILES} getaddrinfo.c)
  ENDIF(CMAKE_LIB_SOCKET_HAS_GETADDRINFO)
ENDIF(NOT HAVE_GETADDRINFO)

CHECK_FUNCTION_EXISTS(inet_ntoa HAVE_INET_NTOA)
IF(NOT HAVE_INET_NTOA)
  CHECK_LIBRARY_EXISTS(nsl inet_ntoa "" CMAKE_LIB_NSL_HAS_INET_NTOA)
  IF(CMAKE_LIB_NSL_HAS_INET_NTOA)
    SET(HAVE_INET_NTOA 1)
    TARGET_LINK_LIBRARIES(dccserver nsl)
  ELSE(CMAKE_LIB_NSL_HAS_INET_NTOA)
    # XXX FATALERROR
  ENDIF(CMAKE_LIB_NSL_HAS_INET_NTOA)
ENDIF(NOT HAVE_INET_NTOA)

# XXX: doesn't work on Irix or Linux, because compiler doesn't like
# cmakeRequireSymbol(0,&socklen_t);
CHECK_SYMBOL_EXISTS(socklen_t "sys/types.h;sys/socket.h" HAVE_SOCKLEN_T)

# custom targets

IF(CMAKE_SYSTEM_NAME MATCHES BSD)
  SET(MANFMT mdoc)
ELSE(CMAKE_SYSTEM_NAME MATCHES BSD)
  SET(MANFMT man)
ENDIF(CMAKE_SYSTEM_NAME MATCHES BSD)

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

# packaging

SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "simulate mIRC's /dccserver command")
SET(CPACK_PACKAGE_VENDOR "Thomas Klausner <tk@giga.or.at>")
SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README")
SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
SET(CPACK_PACKAGE_VERSION_MAJOR "0")
SET(CPACK_PACKAGE_VERSION_MINOR "4")
SET(CPACK_PACKAGE_VERSION_PATCH "a")
SET(CPACK_PACKAGE_EXECUTABLES "dccserver" "dccsend")
SET(CPACK_SOURCE_PACKAGE_FILE_NAME ${PACKAGE_NAME}-${PACKAGE_VERSION})
SET(CPACK_GENERATOR "TGZ")
# XXX: adding "TBZ" gives:
# "CPack Error: Cannot initialize CPack generator:"
SET(CPACK_SOURCE_GENERATOR "TGZ")
INCLUDE(CPack)

#noinst_HEADERS=	dccserver.h
#
#EXTRA_DIST=	dccserver.man dccserver.mdoc \
#		dccsend.man dccsend.mdoc \
#		contrib/dccsend.pl \
#		debian/changelog debian/control debian/copyright debian/dirs \
#		debian/docs debian/rules

# write out config file
CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/cmake-config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/config.h)
