dnl@synopsis RSSH_CHECK_OTS
dnl
dnl   check for CORBA Transaction Service installed and
dnl   set appropriative ORB flags.
dnl  
dnl   currently only ORBacus OTS is supported.
dnl
dnl@author   (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1998
dnl@id   Id: RSSH_CHECK_ORB.m4,v 1.7 2000/08/03 18:34:37 rssh Exp $
dnl --------------------------------------------------------------------
AC_DEFUN([RSSH_CHECK_OTS],[
AC_REQUIRE([RSSH_CHECK_ORB])dnl

AC_ARG_WITH(ots, [ots: prefix of OTS installation (default: \$OB_PREFIX) ] ,\
            OTS_PREFIX=${with_ots} , OTS_PREFIX=$OB_PREFIX )

AC_CHECKING(CORBA Transaction Service)

svLIBS=$LIBS
svCPPFLAGS=$CPPFLAGS

if test ! x$OTS_PREFIX = xno
then
  if test x$ORB != xORBacus
  then
    OTS=no
  else
    if test x$OTS_PREFIX != x$OB_PREFIX 
    then
     CPPFLAGS="$CPPFLAGS -I$OTS_PREFIX/include"
    fi
    OTS_LIBDIR="-I$OTS_PREFIX/lib"
    OTS_LIB="-IOBTransactions"
    LIBS="$OTS_LIBDIR -lOTS -lOBTransactions $LIBS"
    AC_CACHE_CHECK("whether we can link with OTS",
                 [rssh_cv_check_ots],
      AC_LANG_CPLUSPLUS
      AC_TRY_LINK(
#include <OB/CORBA.h>
#include <OB/OTS.h>
#include <OB/CosTransactions.h>
,
int x=0;
OB::OTSInit(x,NULL),
                 rssh_cv_check_ots=yes,rssh_cv_check_ots=no)
      AC_LANG_RESTORE
    )
    if test x$rssh_cv_check_ots = xyes
    then
     AC_DEFINE(OTSINIT,OB::OTSInit)
     AC_DEFINE(OTSXAINIT,OB::XA::OTSInit)
     AC_DEFINE(COSTRANSACTIONS_H,<OB/CosTransactions.h>)
     AC_DEFINE(COSTRANSACTION_SERVER_H,<OB/OTS.h>)
     AC_DEFINE(COSTRANSACTION_XA_SERVER_H,<OB/OTSXA.h>)
    fi
  fi
fi

if test x$rssh_cv_check_ots = xyes
then
  AC_DEFINE(CORBA_HAVE_OTS)
  AC_DEFINE(CORBA_HAVE_XA_OTS)
  IDL_TRANSACTION_FLAGS=-DCORBA_HAVE_OTS
else
  OTS=no
  IDL_TRANSACTION_FLAGS=
  LIBS=$svLIBS
  CPPFLAGS=$svCPPFLAGS
fi
AC_SUBST(IDL_TRANSACTION_FLAGS)

AC_MSG_RESULT("Result for OTS:")

])dnl
dnl
