dnl  set of additional configure scripts.
dnl   (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1998
dnl   $Id: RSSH_CHECK_MODCBROKER.m4,v 1.2 2002/02/06 19:43:50 rssh Exp $
dnl --------------------------------------------------------------------
dnlRSSH_CHECK_MODCBROKER
dnl
AC_DEFUN(RSSH_CHECK_MODCBROKER,[

AC_ARG_WITH(modcbroker, modcbroker - prefix to ModCbroker client library installation (default: /usr/local) ,
         MODCBROKER_PREFIX=${with_modcbroker} , MODCBROKER_PREFIX=/usr/local )
AC_REQUIRE([RSSH_CHECK_ORB])
AC_CHECKING("for ModCbroker")

if test "x$MODCBROKER_PREFIX" != "xno"
then
if test x$MODCBROKER_PREFIX = "xyes" -o x$MODCBROKER_PREFIX = "x"
then
  MODCBROKER_PREFIX=/usr/local 
fi

svCPPFLAGS=$CPPFLAGS
svCXXCPPFLAGS=$CXXCPPFLAGS
svLIBS=$LIBS

MODCBROKER_INCLUDES="-I$MODCBROKER_PREFIX/include"
CPPFLAGS="$MODCBROKER_INCLUDES $CPPFLAGS"

MODCBROKER_LIBDIR="-L$MODCBROKER_PREFIX/lib"
MODCBROKER_LIB="-lclcbroker"
MODCBROKER_LIBS="$MODCBROKER_LIBDIR $MODCBROKER_LIB"
LIBS="$MODCBROKER_LIBDIR $LIBS"

AC_LANG_SAVE
AC_LANG_CPLUSPLUS
YAD_CHECK_INCLUDE_LIB([
#include CORBA_H
#ifndef __CAT2_FF
#define __CAT2_FF(x,y) <##x##y##>
#endif
#ifndef __CAT2_F
#define __CAT2_F(x,y) __CAT2_FF(x,y)
#endif
#define CORBA_STUB_HEADER(x) __CAT2_F(x,IDL_CLN_H_SUFFIX)
#define CORBA_SKELETON_HEADER(x) __CAT2_F(x,IDL_SRV_H_SUFFIX)
#ifdef RSSH_TAO
#include CORBA_STUB_HEADER(tao/PortableServer/PortableServer)
#endif
#include CORBA_SKELETON_HEADER(HTTPServ)
],clcbroker,[HTTP::Servlet_var obj;],
LIBS="$MODCBROKER_LIBS $svLIBS",
MODCBROKER_LIBS="no"
)
AC_LANG_RESTORE
if test "x$MODCBROKER_LIBS" = "xno"
then
 modcbroker_found=no
 LIBS=$svLIBS
 CPPFLAGS=$svCPPFLAGS
 CXXCPPFLAGS=$svCXXCPPFLAGS
else
 modcbroker_found=yes
 AC_SUBST(MODCBROKER_INCLUDES)
 AC_SUBST(MODCBROKER_LIBS)
fi
else
 modcbroker_found=no
fi

AC_MSG_RESULT("Result for ModCbroker: $modcbroker_found")

])dnl
dnl
