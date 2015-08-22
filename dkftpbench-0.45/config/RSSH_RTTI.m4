dnl   (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1998
dnl   $Id: RSSH_RTTI.m4,v 1.4 2001/03/11 02:11:43 rssh Exp $
dnl --------------------------------------------------------------------
dnl RSSH_RTTI(ACTION-IF-FOUND,ACTION-IF-NOT-FOUND)
dnl   rssh_cv_rtti : yes | flag-for-settings | no 
dnl
AC_DEFUN(RSSH_RTTI,[
AC_REQUIRE([AC_PROG_CXX])dnl
AC_REQUIRE([RSSH_CHECK_SUNPRO_CC])dnl
AC_REQUIRE([RSSH_TRY_RTTI_NOCACHE])dnl
AC_MSG_CHECKING( "try set c++ compiler flags for rtti support" )

AC_CACHE_VAL(rssh_cv_rtti,[

 svCXXFLAGS=$CXXFLAGS

 RSSH_TRY_RTTI_NOCACHE

 if test x$rssh_try_rtti_result = xyes
 then
   rssh_cv_rtti=yes
 else
  if test x$rssh_cv_check_sunpro_cc = xyes
  then
     rssh_cv_rtti="-features=rtti"
     rssh_try_set_rtti_try=yes
  else
     if test "x$GXX" = xyes 
     then
       rssh_cv_check_rtti="-frtti"
       rssh_try_set_rtti_try=yes
     fi
  fi
  if test x$rssh_try_set_rtti_try = xyes
  then
    if test x$rssh_cv_rtti != xyes
    then
     CXXFLAGS="$CXXFLAGS $rssh_cv_rtti"
    fi
    rssh_check_rtti_once=yes
    RSSH_TRY_RTTI_NOCACHE
  fi
  if test $rssh_try_rtti_result = no
  then
    rssh_cv_rtti=no
    CXXFLAGS=$svCXXFLAGS
  else
    if test x$rssh_cv_rtti = x
    then
        rssh_cv_rtti=yes
    fi
  fi
 fi

])

AC_MSG_RESULT($rssh_cv_rtti)

if test  "x$rssh_cv_rtti" != xno  -a  "x$rssh_cv_rtti" != xyes 
then
  CXXFLAGS="$CXXFLAGS $rssh_cv_rtti"
  rssh_check_rtti_once=yes
  if test x$rssh_cv_rtti = x-frtti
  then
      AC_MSG_WARN("Old version with gcc with broken rtti detected")
  fi
  $1
  :
else
  $2
  :
fi 
  
if test "x$rssh_check_rtti_once" = xyes
then
  AC_DEFINE(HAVE_RTTI,1)
fi

])dnl
dnl
dnl
