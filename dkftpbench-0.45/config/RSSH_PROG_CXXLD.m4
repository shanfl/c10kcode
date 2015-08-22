dnl autoconf macros: RSSH_PROG_CXXLD
dnl   (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1998
dnl   $Id: RSSH_PROG_CXXLD.m4,v 1.5 2001/05/24 17:04:53 rssh Exp $
dnl --------------------------------------------------------------------
dnl RSSH_PROG_CXXLD
dnl check for program, which used for creating shared C++ libraries.
dnl set variables LD and LD_CREATE_FLAGS.
dnl
dnl
AC_DEFUN(RSSH_PROG_CXXLD,[
AC_REQUIRE([RSSH_CHECK_SUNPRO_CC])
if test x$rssh_cv_check_sunpro_cc = xyes
then 
  LD=CC
  LD_CREATE_FLAGS="\$(CXXFLAGS) -pta -G -o"
else
  if test x$GXX = xyes
  then
    LD=g++
    LD_CREATE_FLAGS="-shared -o"
  else
    LD=ld
    LD_CREATE_FLAGS="-G -o"
  fi
fi
AC_SUBST(LD,$LD)
AC_SUBST(LD_CREATE_FLAGS, $LD_CREATE_FLAGS)
])dnl
dnl
