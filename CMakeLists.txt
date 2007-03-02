# $NiH: CMakeLists.txt,v 1.6 2007/02/26 18:44:52 wiz Exp $

PROJECT(dccserver C)

INCLUDE(CheckFunctionExists)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
#INCLUDE(CheckSymbolExists)

SET(PACKAGE_NAME "dccserver")
SET(PACKAGE_VERSION_MAJOR "0")
SET(PACKAGE_VERSION_MINOR "4")
SET(PACKAGE_VERSION_PATCH "b")
SET(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}${PACKAGE_VERSION_PATCH}")
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

IF(CMAKE_SYSTEM_NAME MATCHES BSD)
  SET(MANFMT mdoc)
ELSE(CMAKE_SYSTEM_NAME MATCHES BSD)
  SET(MANFMT man)
ENDIF(CMAKE_SYSTEM_NAME MATCHES BSD)

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
  CHECK_LIBRARY_EXISTS(socket getaddrinfo "" HAVE_GETADDRINFO)
  IF(HAVE_GETADDRINFO)
    TARGET_LINK_LIBRARIES(dccsend socket)
    TARGET_LINK_LIBRARIES(dccserver socket)
  ELSE(HAVE_GETADDRINFO)
    SET(EXTRA_LIB_FILES ${EXTRA_LIB_FILES} getaddrinfo.c)
  ENDIF(HAVE_GETADDRINFO)
ENDIF(NOT HAVE_GETADDRINFO)

CHECK_FUNCTION_EXISTS(inet_ntoa HAVE_INET_NTOA)
IF(NOT HAVE_INET_NTOA)
  CHECK_LIBRARY_EXISTS(nsl inet_ntoa "" HAVE_INET_NTOA)
  IF(HAVE_INET_NTOA)
    TARGET_LINK_LIBRARIES(dccserver nsl)
  ELSE(HAVE_INET_NTOA)
    MESSAGE(FATAL_ERROR "inet_ntoa() not found")
  ENDIF(HAVE_INET_NTOA)
ENDIF(NOT HAVE_INET_NTOA)

# Manually check for socklen_t. Using CHECK_SYMBOL_EXISTS doesn't work
# on Irix or Linux, because the compilers don't like
# cmakeRequireSymbol(0,&socklen_t);
#CHECK_SYMBOL_EXISTS(socklen_t "sys/types.h;sys/socket.h" HAVE_SOCKLEN_T)
MESSAGE("-- Looking for socklen_t")
TRY_COMPILE(HAVE_SOCKLEN_T
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/socklen_t.c
  OUTPUT_VARIABLE SLT)
IF(HAVE_SOCKLEN_T)
  MESSAGE("-- Looking for socklen_t - found")
ELSE(HAVE_SOCKLEN_T)
  MESSAGE("-- Looking for socklen_t - not found:\n${SLT}")
ENDIF(HAVE_SOCKLEN_T)

# custom targets

SET(MAN_PAGES dccserver dccsend) 
FOREACH(MAN ${MAN_PAGES})
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
ENDFOREACH(MAN)

ADD_CUSTOM_TARGET(update-man
  DEPENDS dccserver.man dccsend.man)

ADD_CUSTOM_TARGET(update-html
  DEPENDS dccserver.html dccsend.html)

# packaging

SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "simulate mIRC's /dccserver command")
SET(CPACK_PACKAGE_VENDOR "Thomas Klausner <tk@giga.or.at>")
SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README")
SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
SET(CPACK_PACKAGE_VERSION_MAJOR ${PACKAGE_VERSION_MAJOR})
SET(CPACK_PACKAGE_VERSION_MINOR ${PACKAGE_VERSION_MINOR})
SET(CPACK_PACKAGE_VERSION_PATCH ${PACKAGE_VERSION_PATCH})
SET(CPACK_PACKAGE_EXECUTABLES "dccserver" "dccsend")
SET(CPACK_SOURCE_PACKAGE_FILE_NAME ${PACKAGE_NAME}-${PACKAGE_VERSION})
SET(CPACK_GENERATOR "TGZ")
# XXX: adding "TBZ" gives:
# "CPack Error: Cannot initialize CPack generator:"
SET(CPACK_SOURCE_GENERATOR "TGZ")
SET(CPACK_SOURCE_IGNORE_FILES
  ".a$"
  ".cmake$"
  ".o$"
  ".so$"
  "/.deps/"
  "/CMakeFiles/"
  "/CVS/"
  "/autom4te.cache/"
  "/docs/"
  "AdditionalInfo.txt$"
  "CMakeCache.txt$"
  "Makefile$"
  "\\\\.sh$"
  "_CPack_"
  "config.h$"
  "config.log$"
  "config.status$"
  "dccsend$"
  "dccserver$"
  "html$"
  "install_manifest.txt$"
  "stamp-h1$"
  "tar.bz2"
  "tar.gz"
  "~$"
)
SET(CPACK_SOURCE_STRIP_FILES "")
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
