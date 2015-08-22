dnl@synposis  CORBA_CHECK_SYMBOL(x)
dnl
dnl if ORB define symbol CORBA::x ?
dnl
dnl@author (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 2001
dnl@id $Id: CORBA_CHECK_SYMBOL.m4,v 1.1 2001/07/06 13:25:28 rssh Exp $
dnl
AC_DEFUN([CORBA_CHECK_SYMBOL],[
AC_REQUIRE([RSSH_CHECK_ORB])dnl

AC_LANG_SAVE
AC_LANG_CPLUSPLUS

AC_CACHE_CHECK("whether CORBA::$1 is defined",
    rssh_cv_check_corbasymbol$1,
    AC_TRY_COMPILE(
#include CORBA_H
,
#ifdef CORBA_NAMESPACE_C_MAPPING
CORBA_$1* x
#else
CORBA::$1* x
#endif
,
    rssh_cv_check_corbasymbol$1=yes,
    rssh_cv_check_corbasymbol$1=no)
)

if  test $rssh_cv_check_corbasymbol$1 = yes
then 
  AC_DEFINE(CORBA_HAVE_$1)
fi

AC_LANG_RESTORE

])dnl
dnl
