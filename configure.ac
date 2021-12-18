AC_INIT([memcached_functions_mysql],[1.1])
AC_CONFIG_SRCDIR([src/get.c])
AC_CONFIG_HEADERS(src/libmemcached_config.h)

AM_INIT_AUTOMAKE

AC_PROG_CC
AC_PROG_LIBTOOL
LIBTOOL="$LIBTOOL --preserve-dup-deps"
AC_SUBST(LIBTOOL)dnl

sinclude(config/ac_mysql.m4)
MYSQL_CONFIG_TEST

AC_SUBST(MYSQL_CONFIG)
AC_SUBST(MYSQL_INC)
AC_SUBST(MYSQL_LIB)

sinclude(config/ac_libmemcached.m4)
LIBMEMCACHED_CONFIG_TEST

AC_SUBST(DEPS_CFLAGS)
AC_SUBST(DEPS_LIBS)

AC_C_CONST
AC_TYPE_SIZE_T
AC_CHECK_HEADERS(limits.h syslimits.h)
AC_CHECK_FUNCS([bzero])
AC_CHECK_FUNCS([strcasecmp])
AC_CHECK_FUNCS([strtol])
AC_CHECK_HEADERS([strings.h])
AC_CHECK_HEADERS([unistd.h])
AC_CHECK_HEADER_STDBOOL
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_PROG_RANLIB
AC_TYPE_UINT16_T
AC_TYPE_UINT64_T
AC_PROG_CPP
AC_PROG_CXX
AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile docs/Makefile])
AC_OUTPUT
