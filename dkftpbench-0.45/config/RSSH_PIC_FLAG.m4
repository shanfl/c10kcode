dnl   (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1998
dnl   $Id: RSSH_PIC_FLAG.m4,v 1.2 2002/01/11 12:10:19 yad Exp $
dnl --------------------------------------------------------------------
dnl RSSH_PIC_FLAG()
dnl   set PIC_FLAG to compiler flag, for producing PIC files.
dnl siutable for building sharing libs.
dnl
AC_DEFUN(RSSH_PIC_FLAG,[
AC_REQUIRE([AC_PROG_CXX])dnl
AC_REQUIRE([RSSH_CHECK_SUNPRO_CC])dnl
AC_MSG_CHECKING( "how we generate PIC code  " )


 if test "x$rssh_cv_check_sunpro_cc" = xyes
 then
   PIC_FLAG="-KPIC"
 else
   PIC_FLAG="-fpic"  #most compilers understood that.
 fi


AC_MSG_RESULT($PIC_FLAG)

AC_SUBST(PIC_FLAG)

])dnl
dnl
dnl
