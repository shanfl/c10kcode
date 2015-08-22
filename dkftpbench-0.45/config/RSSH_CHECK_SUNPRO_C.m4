# RSSH_CHECK_SUNPROC_C([ACTION-IF-YES], [ACTION-IF-NOT])
# ------------------------------------------------------
# check : are we using SUN workshop C compiler.
#  Corresponding cache value: rssh_cv_check_sunpro_c is set to yes or no
#
#@author  Ruslan Shevchenko <Ruslan@Shevchenko.Kiev.UA>, 1998, 2000
#@version $Id: RSSH_CHECK_SUNPRO_C.m4,v 1.8 2000/09/11 03:47:40 rssh Exp $
#
#  RSSH_CHECK_SUNPRO_C([ACTION-IF-YES],[ACTION-IF-NOT])
#
AC_DEFUN([RSSH_CHECK_SUNPRO_C],
[AC_CACHE_CHECK([whether using Sun Worckshop C compiler],
                [rssh_cv_check_sunpro_c],

[AC_LANG_SAVE
 AC_LANG_C
 AC_TRY_COMPILE([],
[#ifndef __SUNPRO_C
# include "error: this is not Sun Workshop."
#endif
],
               rssh_cv_check_sunpro_c=yes,
                rssh_cv_check_sunpro_c=no)
AC_LANG_RESTORE])
if test ${rssh_cv_check_sunpro_c} = yes
then
  $2
  :
else 
  $3
  :
fi
])# RSSH_CHECK_SUNPROC_C
