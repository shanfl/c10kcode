#include "dprint.h"
#include <stdarg.h>

#ifdef USE_DPRINT 
int g_dprint_enabled = true;
FILE *g_dprint_fp = stdout;

void dprint_print(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(g_dprint_fp, format,  args);
	va_end(args);
}

#endif
