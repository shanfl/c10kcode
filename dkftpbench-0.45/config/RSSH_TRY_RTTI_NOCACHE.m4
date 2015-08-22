dnl   (C) Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1998
dnl   $Id: RSSH_TRY_RTTI_NOCACHE.m4,v 1.2 2000/07/19 10:19:15 rssh Exp $
dnl --------------------------------------------------------------------
dnl RSSH_TRY_RTTI_NOCACHE
dnl if C++ compiler have rtti support enabled, than 
dnl and set $rssh_try_rtti_result to "yes" or "no", depend from result.
dnl
AC_DEFUN(RSSH_TRY_RTTI_NOCACHE,[
AC_REQUIRE([AC_PROG_CXX])
AC_LANG_SAVE
AC_LANG_CPLUSPLUS
AC_TRY_RUN([
struct X { virtual int z() { return 1; } };
struct Y:public X { virtual int z() { return 2; } };
int check_dynamic_cast(X& x) {  return dynamic_cast<Y*>(&x)!=0; }
int main() { X x; return check_dynamic_cast(x); }
], 
rssh_try_rtti_result=yes, 
rssh_try_rtti_result=no,
AC_MSG_ERROR("this macros have no support for crosscompiling ")
)
AC_LANG_RESTORE
])dnl
dnl
dnl
