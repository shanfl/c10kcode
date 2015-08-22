dnl@synopsis RSSH_CHECK_ORB
dnl
dnl   check for CORBA ORB, set complilation flags 
dnl   and define appropriative variables and preprocessor symbols
dnl  (look at CORBA-autoconf.pdf for details)
dnl
dnl@author   (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1998
dnl@id   Id: RSSH_CHECK_ORB.m4,v 1.7 2000/08/03 18:34:37 rssh Exp $
dnl --------------------------------------------------------------------
AC_DEFUN([RSSH_CHECK_ORB],[
AC_REQUIRE([AC_PROG_CC])dnl
AC_REQUIRE([AC_PROG_CPP])dnl
AC_REQUIRE([AC_PROG_CXX])dnl
AC_REQUIRE([AC_PROG_CXXCPP])dnl

AC_ARG_WITH(orb, [orb: orb used (one of TAO  omniORB  ORBacus, VisiBroker, OrbixE, ORBexpress)] ,\
            ORB=${with_orb} , ORB=unknown )

AC_CHECKING(ORB)

if test x$ORB = x
then
  ORB=unknown
fi

rssh_know_orb_name=no

if test  "$ORB" = "unknown"  -o  "$ORB" = "TAO" 
then
  RSSH_CHECK_TAO
  rssh_know_orb_name=yes
fi
dnl if test  "$ORB" = "unknown"  -o  "$ORB" = "ORBacus" 
dnl then
dnl   RSSH_CHECK_ORBACUS
dnl   rssh_know_orb_name=yes
dnl fi
if test  "x$ORB" = "xunknown"  -o  "$ORB" = "omniORB"  -o "$ORB" = "omniBroker"
then
  RSSH_CHECK_OMNIORB
  rssh_know_orb_name=yes
fi
dnl if test  "x$ORB" = "xunknown"  -o  "$ORB" = "VisiBroker"  
dnl then
dnl   RSSH_CHECK_VISIBROKER
dnl   rssh_know_orb_name=yes
dnl fi
if test  "x$ORB" = "xunknown"  -o  "$ORB" = "OrbixE"  
then
  RSSH_CHECK_ORBIXE
  rssh_know_orb_name=yes
fi
if test  "x$ORB" = "xunknown"  -o  "$ORB" = "ORBexpress"  
then
  RSSH_CHECK_ORBEXPRESS
  rssh_know_orb_name=yes
fi
if test  "x$ORB" = "xunknown"  -o  "$ORB" = "ORBit"  
then
  RSSH_CHECK_ORBIT
  rssh_know_orb_name=yes
fi

if test "$rssh_know_orb_name" = "no"
then
  AC_MSG_ERROR(unknown ORB name)
fi
     

if test "$ORB" = "xunknown" ; then
  AC_MSG_ERROR(no orb found)
fi

IDL_DEPEND_ORB_FLAGS=""
svX=$X
X=""
X="$X --cln_h_suffix  $IDL_CLN_H_SUFFIX"
X="$X --cln_h1_suffix  $IDL_CLN_H1_SUFFIX"
X="$X --cln_cpp_suffix    $IDL_CLN_CPP_SUFFIX"
X="$X --cln_obj_suffix    $IDL_CLN_OBJ_SUFFIX"
X="$X --srv_h_suffix    $IDL_SRV_H_SUFFIX"
X="$X --srv_h1_suffix    $IDL_SRV_H1_SUFFIX"
X="$X --srv_cpp_suffix    $IDL_SRV_CPP_SUFFIX"
X="$X --srv_obj_suffix    $IDL_SRV_OBJ_SUFFIX"
X="$X --tie_h_suffix    $IDL_TIE_H_SUFFIX"
X="$X --tie_h1_suffix    $IDL_TIE_H1_SUFFIX"
X="$X --tie_cpp_suffix    $IDL_TIE_CPP_SUFFIX"

IDL_DEPEND_ORB_FLAGS="$X"
AC_SUBST(IDL_DEPEND_ORB_FLAGS)
X=$svX

AC_SUBST(ORB_INCLUDE)

AC_MSG_RESULT("Result for ORB: $ORB")

])dnl
dnl
