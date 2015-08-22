#ifndef CHECK_h
#define CHECK_h
/**--------------------------------------------------------------------------
 @name CHECK.h - Helper functions for unit self-tests.
 Each macro makes a particular check, and if the check fails,
 an error message containing the filename and line number
 is printed to stdout, and the program is terminated with status 1.
--------------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

/** Check two integers to make sure they are equal. */
#define CHECK(d, e) check((int)(d), (int)(e), __FILE__,  __LINE__)
void check(int d, int e, const char * file, int line);

/** Check two integers to make sure they are not equal. */
#define CHECKNE(d, e) checkne((int)(d), (int)(e), __FILE__,  __LINE__)
void checkne(int d, int e, const char * file, int line);

#ifdef __cplusplus
}
#endif
#endif
