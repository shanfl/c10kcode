dnl@synposis RSSH_CHECK_CORBA_ORB 
dnl
dnl set CORBA support for omniORB v3-pr2 or highter
dnl    ( http://www.uk.research.att.com/omniORB/omniORB.html)
dnl
dnl@author (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1999, 2000
dnl@id $Id: RSSH_CHECK_OMNIORB.m4,v 1.20 2002/01/16 16:33:28 yad Exp $
dnl
AC_DEFUN([RSSH_CHECK_OMNIORB],[
AC_REQUIRE([AC_PROG_CC])dnl
AC_REQUIRE([AC_PROG_CXX])dnl
AC_REQUIRE([AC_PROG_CPP])dnl
AC_REQUIRE([AC_PROG_CXXCPP])dnl


AC_ARG_WITH(omni, [omni: prefix to omniORB installation (default: \$OMNI_ROOT)] ,\
            OMNI_PREFIX=${with_omni} , OMNI_PREFIX=/usr/local )

AC_CHECKING(for omniORB)

if test "x$OMNI_ROOT" = "x"
then
 if test "x$OMNI_PREFIX" = "x"
 then
   OMNI_ROOT="/usr/local"
 else
   OMNI_ROOT="$OMNI_PREFIX"
 fi
fi

if  test "x$OMNI_PREFIX" = "xno"
then
dnl OMNI NOT SET 
  AC_MSG_RESULT(omniORB is disabled)
  omni=no
else

AC_LANG_SAVE
AC_LANG_CPLUSPLUS

svCXXCPPFLAGS=$CXXCPPFLAGS
svCXXFLAGS=$CXXFLAGS
svCPPFLAGS=$CPPFLAGS
svLIBS=$LIBS
svLDFLAGS=$LDFLAGS
svRSSH_ROLLBACK=$rssh_rollback
rssh_rollback="true"

ORB_INCLUDES="-I$OMNI_ROOT/include"
CXXCPPFLAGS="$CXXCPPFLAGS -I$OMNI_ROOT/include "
CPPFLAGS="$CPPFLAGS -I$OMNI_ROOT/include "

RSSH_ENABLE_PTHREADS

CXXCPPFLAGS="$CXXCPPFLAGS"

case $build_cpu in
 sparc*)
    AC_DEFINE(__sparc__)
    IDLCXXFLAGS="$IDLCXXFLAGS -D__sparc__"
    ;;
 "i686"|"i586"|"i486"|"i386")
    AC_DEFINE(__x86__)
    IDLCXXFLAGS="$IDLCXXFLAGS -D__x86__"
    ;;
esac
case $build_os in
 solaris*)
    AC_DEFINE(__sunos__)
    IDLCXXFLAGS="$IDLCXXFLAGS -D__sunos__"
    __OSVERSION__=5
    AC_DEFINE(__OSVERSION__)
    IDLCXXFLAGS="$IDLCXXFLAGS -D__OSVERSION__=5"
    ;;
 freebsd*)
    AC_DEFINE(__freebsd__)
    IDLCXXFLAGS="$IDLCXXFLAGS -D__freebsd__"
    ;;
esac

AC_SUBST(IDLCXXFLAGS)

CXXCPPFLAGS="$CXXCPPFLAGS $IDLCXXFLAGS"

AC_CHECK_HEADER( omniORB3/CORBA.h, omni=yes , omni=no, )

if test "x$omni" = "xyes" 
then
  ORB_LIBDIR="$OMNI_ROOT/lib"
  if test ! -r "$ORB_LIBDIR/libomniORB3.so"
  then
    for i in $OMNI_ROOT/lib/*/lib*.so
    do
      ORB_LIBDIR=`dirname $i` 
      break;
    done
  fi

  LIBS="$LIBS -lomnithread"
  svLIBS=$LIBS
  LIBS="-L$ORB_LIBDIR $LIBS"

  AC_CACHE_CHECK("are we have omnithreads",
    rssh_cv_check_omnithreads,
    rssh_enable_pthreads_done=""
    RSSH_ENABLE_PTHREADS
    AC_LANG_SAVE
    AC_LANG_CPLUSPLUS
    AC_TRY_LINK(
#include <omnithread.h>
,omni_mutex my_mutex,
                 rssh_cv_check_omnithreads=yes,rssh_cv_check_omnithreads=no)
    AC_LANG_RESTORE
  )
  if  test ! $rssh_cv_check_omnithreads = yes
  then
    AC_MSG_RESULT("omnithreads not found")
    omni_lib=no
  fi
  AC_CHECK_LIB(socket,socket, LIBS="-lsocket $LIBS",,)
  AC_CHECK_LIB(nsl,gethostbyname, LIBS="-lnsl $LIBS",,)

  ORB_LDFLAGS="-L$ORB_LIBDIR"
  LIBS="$ORB_LDFLAGS -lomniORB3 -ltcpwrapGK -lomniDynamic3 $svLIBS"
  AC_CACHE_CHECK("whether we can link with omniORB3",
    rssh_cv_check_omniORBlib,
    AC_TRY_LINK(
#include <omniORB3/CORBA.h>
,CORBA::ORB_var orb,
    rssh_cv_check_omniORBlib=yes,rssh_cv_check_omniORBlib=no
    )
  )

  if  test ! $rssh_cv_check_omniORBlib = yes
  then
    AC_MSG_RESULT("omniORB libs not found")
    omni_lib=no
  fi


  ORB_LIBS="$ORB_LDFLAGS -lomniORB3 -lomnithread"
fi

if test "x$omni_lib" = "xno"
then
 AC_MSG_RESULT(omniORB library linking failed)
 omni="no"
fi

fi

if test "x$omni" = "x" -o "x$omni" = "xno"
then
  CXXCPPFLAGS=$svCXXCPPFLAGS
  CPPFLAGS=$svCPPFLAGS
  LIBS=$svLIBS
  LDFLAGS=$svLDFLAGS
  ORB=unknown
  omni=no
  eval "$rssh_rollback"
  rssh_rollback=$svRSSH_ROLLBACK 
else
  AC_SUBST(CORBA_INCLUDES)

  ORB_PREFIX=$OMNI_ROOT
  AC_SUBST(ORB_PREFIX)

  ORB=omniORB
  AC_SUBST(ORB)

  IDL=omniidl
  if test -x $OMNI_ROOT/bin/omniidl
  then
    IDL=$OMNI_ROOT/bin/omniidl
  else
    for i in $OMNI_ROOT/bin/*/omniidl
    do
      if test "$i" != $OMNI_ROOT'/bin/*/omniidl'
      then
        IDL=$i
        break
      fi
    done 
  fi
  AC_SUBST(IDL)
  IDLCXX=$IDL
  AC_SUBST(IDLCXX)

  IDLFLAGS="$IDLFLAGS -bcxx -I$OMNI_ROOT/idl"
  AC_SUBST(IDLFLAGS)

  ORB_INCLUDE_PREFIX=
  AC_SUBST(ORB_INCLUDE_PREFIX)

  IDL_H_SUFFIX=.hh
  AC_SUBST(IDL_H_SUFFIX)
  IDL_H1_SUFFIX=no
  AC_SUBST(IDL_H1_SUFFIX)

  IDL_CLN_H=.hh
  IDL_CLN_H_SUFFIX=.hh
  IDL_CLN_H1_SUFFIX=no

  AC_SUBST(IDL_CLN_H,$IDL_CLN_H)
  AC_SUBST(IDL_CLN_H_SUFFIX,$IDL_CLN_H_SUFFIX)
  AC_SUBST(IDL_CLN_H1_SUFFIX,$IDL_CLN_H1_SUFFIX)
  AC_DEFINE_UNQUOTED(IDL_CLN_H_SUFFIX,$IDL_CLN_H_SUFFIX)

  IDL_CLN_CPP=SK.cc
  IDL_CLN_CPP_SUFFIX=SK.cc
  AC_SUBST(IDL_CLN_CPP,$IDL_CLN_CPP)
  AC_SUBST(IDL_CLN_CPP_SUFFIX,$IDL_CLN_CPP_SUFFIX)
  AC_DEFINE_UNQUOTED(IDL_CLN_CPP_SUFFIX,$IDL_CLN_CPP)

  IDL_CLN_O=SK.o 
  IDL_CLN_OBJ_SUFFIX=SK.o 
  AC_SUBST(IDL_CLN_O,$IDL_CLN_O)
  AC_SUBST(IDL_CLN_OBJ_SUFFIX,$IDL_CLN_OBJ_SUFFIX)

  IDL_SRV_H=.hh
  IDL_SRV_H_SUFFIX=.hh
  IDL_SRV_H1_SUFFIX=no
  AC_SUBST(IDL_SRV_H,$IDL_SRV_H)
  AC_SUBST(IDL_SRV_H_SUFFIX,$IDL_SRV_H_SUFFIX)
  AC_SUBST(IDL_SRV_H1_SUFFIX,$IDL_SRV_H1_SUFFIX)
  AC_DEFINE_UNQUOTED(IDL_SRV_H_SUFFIX,$IDL_SRV_H_SUFFIX)

  IDL_SRV_CPP=SK.cc
  IDL_SRV_CPP_SUFFIX=SK.cc
  AC_SUBST(IDL_SRV_CPP,$IDL_SRV_CPP)
  AC_SUBST(IDL_SRV_CPP_SUFFIX,$IDL_SRV_CPP_SUFFIX)
  AC_DEFINE_UNQUOTED(IDL_SRV_H_SUFFIX,$IDL_SRV_H_SUFFIX)

  IDL_SRV_O=SK.o
  IDL_SRV_OBJ_SUFFIX=SK.o
  AC_SUBST(IDL_SRV_O,$IDL_SRV_O)
  AC_SUBST(IDL_SRV_OBJ_SUFFIX,$IDL_SRV_OBJ_SUFFIX)

  IDL_TIE_H_SUFFIX=no
  IDL_TIE_H1_SUFFIX=no
  IDL_TIE_CPP_SUFFIX=no
  AC_SUBST(IDL_TIE_H_SUFFIX,$IDL_TIE_H_SUFFIX)
  AC_SUBST(IDL_TIE_H1_SUFFIX,$IDL_TIE_H1_SUFFIX)
  AC_SUBST(IDL_TIE_CPP_SUFFIX,$IDL_TIE_CPP_SUFFIX)

  CORBA_H='omniORB3/CORBA.h'
  AC_DEFINE_UNQUOTED(CORBA_H,<$CORBA_H>)

  COSNAMING_H='omniORB3/Naming.hh'
  AC_DEFINE_UNQUOTED(COSNAMING_H,<$COSNAMING_H>)

  ORB_COSNAMING_LIB= 
  AC_SUBST(ORB_COSNAMING_LIB)

dnl i. e. it's build into ORB lib

  HAVE_ORB_IDL=1
  AC_SUBST(HAVE_ORB_IDL)

  AC_CACHE_CHECK("whether CORBA modules mapped to namespaces",
    rssh_cv_corba_namespaces,
  AC_TRY_COMPILE(#include <$CORBA_H>
,
[
#ifndef HAS_Cplusplus_Namespace
#error "we have no namespaces"
we have no namespaces -- $$$$
#else
return 0;
#endif
], rssh_cv_corba_namespaces=yes, rssh_cv_corba_namespaces=no)
  )

  if test "$rssh_cv_corba_namespaces" = "yes" 
  then
    AC_DEFINE(CORBA_MODULE_NAMESPACE_MAPPING)
  else
    AC_DEFINE(CORBA_MODULE_CLASS_MAPPING)
  fi
  
  AC_DEFINE(OMNIORB)

  CORBA_HAVE_POA=1
  AC_DEFINE(CORBA_HAVE_POA)

  CORBA_ORB_INIT_HAVE_3_ARGS=1
  AC_DEFINE(CORBA_ORB_INIT_HAVE_3_ARGS)
  CORBA_ORB_INIT_THIRD_ARG='"omniORB3"'
  AC_DEFINE(CORBA_ORB_INIT_THIRD_ARG, "omniORB3")
  AC_DEFINE(CORBA_ORB_HAVE_DESTROY)


fi

AC_LANG_RESTORE

AC_MSG_RESULT(for omniORB: $omni)

])dnl
dnl
