dnl@synopsys YAD_CHECK_INCLUDE_LIB(INCLUDE, LIBRARY, CODE
dnl              [, ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND
dnl              [, OTHER-LIBRARIES]]])
dnl 
dnl same as the AC_CHECK_LIB except of the following:
dnl      - You sholud specify include part of test.
dnl      - You can test any code for linking, not just function calls.
dnl
dnl@author Alexandr Yanovets <yad@gradsoft.kiev.ua>
dnl@id $Id: YAD_CHECK_INCLUDE_LIB.m4,v 1.4 2002/01/11 12:09:01 rssh Exp $
dnl
AC_DEFUN(YAD_CHECK_INCLUDE_LIB,
[AC_MSG_CHECKING([for $3 in -l$2])
dnl Use a cache variable name containing both the library and function name,
dnl because the test really is for library $2 defining function $3, not
dnl just for library $2.  Separate tests with the same $2 and different $3s
dnl may have different results.
ac_lib_var=`echo $2['_']include | sed 'y%./+-%__p_%'`
AC_CACHE_VAL(ac_cv_lib_$ac_lib_var,
[yad_check_lib_save_LIBS="$LIBS"
LIBS="-l$2 $6 $LIBS"
AC_TRY_LINK(dnl
            [$1],
	    [$3],
	    eval "ac_cv_lib_$ac_lib_var=yes",
	    eval "ac_cv_lib_$ac_lib_var=no")
LIBS="$yad_check_lib_save_LIBS"
])dnl
if eval "test \"`echo '$ac_cv_lib_'$ac_lib_var`\" = yes"; then
  AC_MSG_RESULT(yes)
  ifelse([$4], ,
[changequote(, )dnl
  ac_tr_lib=HAVE_LIB`echo $2 | sed -e 's/[^a-zA-Z0-9_]/_/g' \
    -e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/'`
changequote([, ])dnl
  AC_DEFINE_UNQUOTED($ac_tr_lib)
  LIBS="-l$2 $LIBS"
], [$4])
else
  AC_MSG_RESULT(no)
ifelse([$5], , , [$5
])dnl
fi
])

