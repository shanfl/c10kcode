#@synonips RSSH_PROG_CXXAR
#
# check for program, which we use for creating static C++ libraries.
#   set it name to AR, flags to ARFLAGS
#
#@author   (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1998, 2000
#@id $Id: RSSH_PROG_CXXAR.m4,v 1.4 2000/07/12 08:06:53 rssh Exp $
#
AC_DEFUN(RSSH_PROG_CXXAR,[
AC_REQUIRE([AC_PROG_CXX])
AC_REQUIRE([RSSH_CHECK_SUNPRO_CC])
if test x$rssh_cv_check_sunpro_cc = xyes
then
  AR=CC
  ARFLAGS="\$(CXXFLAGS) -xar -o"
else
  AR=ar
  ARFLAGS=cru
fi
AC_SUBST(AR,$AR)
AC_SUBST(ARFLAGS, $ARFLAGS)
])# RSSH_PROG_CXXAR
