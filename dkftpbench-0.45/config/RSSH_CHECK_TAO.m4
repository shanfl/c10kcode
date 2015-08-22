#@synopsis RSSH_CHECK_TAO
#
# support macroses for TAO CORBA ORB
#        (see http://www.cs.wustl.edu/~schmidt/TAO.html)
#
#@author (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1998, 2000
#
#@id Id: RSSH_CHECK_TAO.m4,v 1.10 2000/08/04 20:52:32 rssh Exp $
#
AC_DEFUN([RSSH_CHECK_TAO],[
AC_REQUIRE([AC_PROG_CC])dnl
AC_REQUIRE([AC_PROG_CXX])dnl
AC_REQUIRE([AC_PROG_CPP])dnl
AC_REQUIRE([AC_PROG_CXXCPP])dnl


AC_ARG_WITH(tao, [tao: prefix to TAO installation (default: \$TAO_ROOT)] ,\
            TAO_PREFIX=${with_tao} , TAO_PREFIX=/usr/local )

AC_CHECKING(for TAO)

if test "x$TAO_PREFIX" = "xno"
then
 tao=no
else

svCXXCPPFLAGS=$CXXCPPFLAGS
svCXXFLAGS=$CXXFLAGS
svCPPFLAGS=$CPPFLAGS
svLIBS=$LIBS
svLDFLAGS=$LDFLAGS
svRSSH_ROLLBACK=$rssh_rollback
rssh_rollback="true"

if  test "x$ACE_ROOT" = "x"
then
  AC_MSG_RESULT(ACE_ROOT not set)
  tao=no
else

RSSH_ENABLE_PTHREADS
AC_LANG_SAVE
AC_LANG_CPLUSPLUS

ORB_INCLUDES="-I$ACE_ROOT -I$ACE_ROOT/TAO -I$ACE_ROOT/TAO/orbsvcs"
CXXCPPFLAGS="$CXXCPPFLAGS $ORB_INCLUDES"
CPPFLAGS="$CPPFLAGS $ORB_INCLUDES"

AC_CHECK_HEADER( tao/corba.h, tao=yes , tao=no, )

if test "x$tao" = "xyes" 
then
  LDFLAGS="$LDFLAGS -L$ACE_ROOT/ace"
#  AC_CHECK_LIB(ACE,main, LIBS="-lACE $LIBS",ace_lib=no,)
#  AC_HAVE_LIBRARY(ACE, LIBS="-lACE $LIBS",ace_lib=no,)
  YAD_CHECK_INCLUDE_LIB([#include <tao/corba.h>],TAO,CORBA::ORB_var orb, LIBS="-lTAO -lACE $LIBS",tao_libs=no,-lACE)
  AC_CHECK_LIB(socket,socket, LIBS="-lsocket $LIBS",,)
  AC_CHECK_LIB(nsl,gethostbyname, LIBS="-lnsl $LIBS",,)
  AC_CHECK_HEADER( tao/PortableServer/PortableServer.h, tao_poahead=yes, tao_poahead=no, )
  if test "x$tao_poahead" = "xyes"
  then
    AC_DEFINE(TAO_HAVE_PORTABLE_SERVER_H)
    YAD_CHECK_INCLUDE_LIB([#include <tao/corba.h>
#include <tao/PortableServer/PortableServer.h>],TAO_PortableServer,PortableServer::ObjectId_var oid = PortableServer::string_to_ObjectId("myObj"), LIBS="-lTAO_PortableServer $LIBS")
  fi
  YAD_CHECK_INCLUDE_LIB([#include <orbsvcs/Time_Utilities.h>],TAO_Svc_Utils,ORBSVCS_Time::zero(), LIBS="-lTAO_Svc_Utils $LIBS")
  AC_CHECK_HEADER( tao/IORTable/IORTable.h, tao_iortable=yes, tao_iortable=no, )
  if test "x$tao_iortable" = "xyes"
  then
    AC_DEFINE(TAO_HAVE_IORTABLE_ADAPTER)
    YAD_CHECK_INCLUDE_LIB([#include <tao/IORTable/IORTable.h>],TAO_IORTable,TAO_IORTable_Initializer::init(), LIBS="-lTAO_IORTable $LIBS")
  fi
  LIBS="$LDFLAGS $LIBS"
fi

#if test "x$ace_lib" = "xno"
#then
# tao=no
#fi

if test "x$tao_libs" = "xno"
then
 tao=no
fi

fi

if test "x$tao" = "xno"
then
  CXXCPPFLAGS=$svCXXCPPFLAGS
  CPPFLAGS=$svCPPFLAGS
  LIBS=$svLIBS
  LDFLAGS=$svLDFLAGS
  eval "$rssh_rollback"
  rssh_rollback=$svRSSH_ROLLBACK
else
  ORB_PREFIX=$ACE_ROOT
  AC_SUBST(ORB_PREFIX)

  ORB=TAO
  AC_SUBST(ORB)

  IDL=$ACE_ROOT/TAO/TAO_IDL/tao_idl
  AC_SUBST(IDL)

  IDLFLAGS="$IDLFLAGS -I$ACE_ROOT/TAO/orbsvcs/orbsvcs"
  AC_SUBST(IDLFLAGS)

  ORB_INCLUDE_PREFIX=tao
  AC_SUBST(ORB_INCLUDE_PREFIX)


  IDL_CLN_H=C.h
  IDL_CLN_H_SUFFIX=C.h
  IDL_CLN_H1_SUFFIX=C.i
  AC_SUBST(IDL_CLN_H,$IDL_CLN_H)
  AC_SUBST(IDL_CLN_H_SUFFIX,$IDL_CLN_H_SUFFIX)
  AC_SUBST(IDL_CLN_H1_SUFFIX,$IDL_CLN_H1_SUFFIX)
  AC_DEFINE_UNQUOTED(IDL_CLN_H_SUFFIX,$IDL_CLN_H_SUFFIX)

  IDL_CLN_CPP=C.cpp
  IDL_CLN_CPP_SUFFIX=C.cpp
  AC_SUBST(IDL_CLN_CPP,$IDL_CLN_CPP)
  AC_SUBST(IDL_CLN_CPP_SUFFIX,$IDL_CLN_CPP_SUFFIX)
  AC_DEFINE_UNQUOTED(IDL_CLN_CPP_SUFFIX,$IDL_CLN_CPP_SUFFIX)

  IDL_CLN_O=C.o 
  IDL_CLN_OBJ_SUFFIX=C.o 
  AC_SUBST(IDL_CLN_O,$IDL_CLN_O)
  AC_SUBST(IDL_CLN_OBJ_SUFFIX,$IDL_CLN_OBJ_SUFFIX)

  IDL_SRV_H=S.h 
  IDL_SRV_H_SUFFIX=S.h 
  IDL_SRV_H1_SUFFIX=S.i 
  AC_SUBST(IDL_SRV_H,$IDL_SRV_H)
  AC_SUBST(IDL_SRV_H_SUFFIX,$IDL_SRV_H_SUFFIX)
  AC_SUBST(IDL_SRV_H1_SUFFIX,$IDL_SRV_H1_SUFFIX)
  AC_DEFINE_UNQUOTED(IDL_SRV_H_SUFFIX,$IDL_SRV_H_SUFFIX)

  IDL_SRV_CPP=S.cpp
  IDL_SRV_CPP_SUFFIX=S.cpp
  AC_SUBST(IDL_SRV_CPP)
  AC_SUBST(IDL_SRV_CPP_SUFFIX)
  AC_DEFINE_UNQUOTED(IDL_SRV_CPP_SUFFIX,$IDL_SRV_CPP_SUFFIX)

  IDL_SRV_O=S.o
  IDL_SRV_OBJ_SUFFIX=S.o
  AC_SUBST(IDL_SRV_O,$IDL_SRV_O)
  AC_SUBST(IDL_SRV_OBJ_SUFFIX,$IDL_SRV_OBJ_SUFFIX)

  IDL_TIE_H_SUFFIX=S_T.h
  IDL_TIE_H1_SUFFIX=S_T.i
  IDL_TIE_CPP_SUFFIX=S_T.cpp
  AC_SUBST(IDL_TIE_H_SUFFIX,$IDL_TIE_H_SUFFIX)
  AC_SUBST(IDL_TIE_H1_SUFFIX,$IDL_TIE_H1_SUFFIX)
  AC_SUBST(IDL_TIE_CPP_SUFFIX,$IDL_TIE_CPP_SUFFIX)

  CORBA_H='tao/corba.h'
  AC_DEFINE_UNQUOTED(CORBA_H,<$CORBA_H>)

  COSNAMING_H='orbsvcs/CosNamingC.h'
  AC_DEFINE_UNQUOTED(COSNAMING_H,<$COSNAMING_H>)

  YAD_CHECK_INCLUDE_LIB([#include <$CORBA_H>
#include <$COSNAMING_H>],orbsvcs,CORBA::Object_var obj; CosNaming::NamingContext_ptr nc = CosNaming::NamingContext::_narrow(obj),have_orbsvcs=yes,have_orbsvcs=no)
  YAD_CHECK_INCLUDE_LIB([#include <$CORBA_H>
#include <$COSNAMING_H>],TAO_CosNaming,CORBA::Object_var obj; CosNaming::NamingContext_ptr nc = CosNaming::NamingContext::_narrow(obj),have_taoCosNaming=yes,have_taoCosNaming=no)
  if test "$have_orbsvcs" = yes 
  then
    ORB_COSNAMING_LIB="-lorbsvcs"
    TAO_VERSION=11
  else
   if test "$have_taoCosNaming" = yes 
   then
    ORB_COSNAMING_LIB="-lTAO_CosNaming"
    TAO_VERSION=12
   else
    AC_MSG_ERROR("found TAO but can not find TAO CosNaming libraries")
   fi
  fi
   
  AC_SUBST(ORB_COSNAMING_LIB)

  HAVE_ORB_IDL=1
  AC_SUBST(HAVE_ORB_IDL)

  CORBA_HAVE_POA=1
  AC_DEFINE_UNQUOTED(CORBA_HAVE_POA,$CORBA_HAVE_POA)

  AC_CACHE_CHECK("whether TAO support namespaces",
  rssh_cv_tao_corba_namespaces,
  AC_TRY_COMPILE(#include <$CORBA_H>
,
[
#ifndef ACE_HAS_USING_KEYWORD
#error "we have no namespaces"
we have no namespaces -- $$$$
#else
return 0;
#endif
], rssh_cv_tao_corba_namespaces=yes, rssh_cv_tao_corba_namespaces=0)
  )

  if test "$rssh_cv_tao_corba_namespaces" = "yes" 
  then
    AC_DEFINE(CORBA_MODULE_NAMESPACE_MAPPING)
  else
    AC_DEFINE(CORBA_MODULE_CLASS_MAPPING)
  fi
  
  AC_DEFINE(RSSH_TAO)
  AC_DEFINE(CORBA_SYSTEM_EXCEPTION_IS_STREAMLE)
  AC_DEFINE(CORBA_ORB_HAVE_DESTROY)

fi
fi

AC_LANG_RESTORE

AC_MSG_RESULT(for TAO: $tao)

])dnl
dnl
