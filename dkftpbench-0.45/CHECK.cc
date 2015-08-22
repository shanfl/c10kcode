/**--------------------------------------------------------------------------
 Helper functions for unit self-tests.
--------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CHECK.h"
#include "dprint.h"

/* Check two integers to make sure they are equal. */
void check(int d, int e, const char* file, int line)
{
	if (d != e) {
		printf("check: %d != %d at line %d file %s\n", d, e, line, file);
		DPRINT(("check: %d != %d at line %d file %s\n", d, e, line, file));
		exit(1);
	}
}

/* Check two integers to make sure they are not equal. */
void checkne(int d, int e, const char* file, int line)
{
	if (d == e) {
		printf("check: %d == %d at line %d file %s\n", d, e, line, file);
		DPRINT(("check: %d == %d at line %d file %s\n", d, e, line, file));
		exit(1);
	}
}
