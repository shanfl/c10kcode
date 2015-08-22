/*
 * Original by Ramon Garcia Fernandez <ramon@juguete.quim.ucm.es>
 * Hacked by linus
 * then mingo broke it all
 * and dank made it take the profile data filename as an argument
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#define prof_func "sys_open"

#define BUFSIZE 1024

struct entry {
	struct entry * next;
	long long time;	/*
			 * With APIC irqs we can get more than
			 * 4G profiling hits.
			 */
	unsigned long address;
	char type; 	/*
			 * We profile only functions currently,
			 * so we can do some sanity checking by analyzing
			 * the symbol type field in System.map
			 */
	char name[1];
};

struct entry * list = NULL;

static void do_symbol(long long time, unsigned long address, char * name, char type)
{
	struct entry * entry = malloc(sizeof(struct entry) + strlen(name));
	struct entry ** tmp;

	entry->time = time;
	entry->address = address;
	strcpy(entry->name, name + (*name == '_'));
	entry->type = type;
	tmp = &list;
	while (*tmp) {
		if ((*tmp)->time > entry->time)
			break;
		tmp = &(*tmp)->next;
	}
	entry->next = *tmp;
	*tmp = entry;
}

static void show_symbols(long long total)
{
	int had_type_conflict=0;
	struct entry * entry = list;
	long long sanity_total=0;

	printf("\n//\n// Function granularity sorted histogram:\n");
	printf("//---------------------------------------\n");
	while (entry) {
		printf("%12Ld %5Ld.%02Ld%% %08lx %s\n" ,
			entry->time,
			(entry->time*10000/total) / 100,
			(entry->time*10000/total) % 100,
			entry->address, entry->name);
		if ((entry->type != 't') && (entry->type != 'T')) {
			had_type_conflict=1;
			printf("// TYPE CONFLICT (type:'%c', symbol:'%s').\n",
					entry->type,entry->name);
		}
		sanity_total += entry->time;

		entry = entry->next;
	}
	printf("//-----------------------------\n");
	printf("%12Ld   100.00%% 00000000 TOTAL\n" ,total);

	if (total != sanity_total) 
		printf("WARNING: sum of times(%Ld) != total.\n", sanity_total);

	if (had_type_conflict) {
		fprintf(stderr, "\n// WARNING: type conflict detected.");
		fprintf(stderr, " (wrong System.map?)\n");
#if 0
		// this warning appears to be spurious - wonder what's wrong?
		exit(1);
#endif
	}
}

static inline void show_finegrained_function (int has_read, int * buffer, int step,
					 char * func, int addr)
{
	long long sum=0;
	int i;

	for ( i = 0 ; i < has_read/(int)sizeof(int) ; i++)
		sum += buffer[i];

	if (!sum)
		printf("// (no profiling hits in %s()).\n", func);
	else {
		printf("// Fine grained profile of %s().\n", func);
		printf("//\n");
		printf("// TOTAL:   100.000000%%  %12Ld\n", sum);
		printf("//-----------------------------------\n");

		for ( i = 0 ; i < has_read/(int)sizeof(int) ; i++) {

			/*
			 * Could someone please enhance/fix printf's %f
			 * conversion type? This workaround to get proper
			 * padding and position is _soooo_ silly.
			 */
			printf("   %08x %3Ld.%06Ld%%  %12d\n",
				i*step + addr,
				 ((long long)buffer[i]*100)/sum,
				(((long long)buffer[i]*100)%sum)*1000000/sum,
				buffer[i]
			);
		}
	}
}

/* If you do not speak Spanish:
 * valor_simbolo_actual: current_symbol_value
 * valor_simbolo_siguiente: next_symbol_value
 * simbolo_actual: current_symbol
 * next_symbol: next_symbol
 * leidos: read (past participle)
 * total: total
*/

int main(int argc, char ** argv)
{
	int fp;
	const char * func = prof_func;
	FILE *kmap;
	int current_symbol_value , next_symbol_value, code_offset;
	char current_symbol[80] , next_symbol[80];
	int has_read , j;
	long long total=0;
	off_t profile_size;
	unsigned long end_address=0;
	const char *fname = "/proc/profile";

	int step;	/*
			 * We can read the profiling step from
			 * /proc/profile directly, so we are not
			 * compilation dependent
			 */

	char type;

	int found_function=0, 
	    just_found_function=0;     /*
					* This is needed for ultra-low
					* resolution profiling.
					*/
	if (argc >= 2)
		func = argv[1];
	if (argc >= 3)
		fname = argv[2];

	fp = open(fname, O_RDONLY);
	if (fp < 0) {
		perror(fname);
		exit(1);
	}
	kmap = fopen("/System.map","r");
	if (!kmap) {
		kmap = fopen("/usr/src/linux/System.map","r");
		if (!kmap) {
			perror("System.map");
			exit(1);
		}
	}

/*
 * The size of /proc/profile is a very good sanity check, it should end
 * with _etext. If not the System.map is bolixed.
 */
	{
		struct stat statbuf;
		fstat (fp, &statbuf);
		profile_size=statbuf.st_size;
	}

	fscanf(kmap, "%x %*s %s\n", &current_symbol_value, current_symbol);
	fscanf(kmap, "%x %c %s\n", &next_symbol_value, &type, next_symbol);

/*
 * We expect the _stext symbol here. This is both necessary and a good
 * sanity check.
 */
	if (	strcmp(current_symbol,"_stext") &&
	 	strcmp(current_symbol,"_text")		) {

		fprintf(stderr,
		   "expecting _text or _stext as first System.map symbol.\n%s",
		   "(maybe stray undefined symbols in System.map?).\n");
		exit(1);
	}
	code_offset=current_symbol_value;

/*
 * Here we read the profiling step from /proc/profile. This is a good
 * sanity check as well. Nothing should go wrong after this.
 */
	has_read = read (fp , &step , sizeof(step) );
	if (has_read != (int)sizeof(int)) {
		perror("huh, couldnt read step from /proc/profile.");
		exit(1);
	}
	printf("// Step: %d, Profiling %s().\n\n",step, func);

	/*
	 * Main read-analyze loop.
	 */
	for (;;) {
		unsigned long long tiempo = 0;
		unsigned int buffer [(next_symbol_value -
					 current_symbol_value)/step];

		if (!strcmp(func, current_symbol)) {
			found_function=1;
			just_found_function=1;
		}

		if (!strcmp("_etext", current_symbol))
			end_address=next_symbol_value;

		if ((next_symbol_value/step) == (current_symbol_value/step)) {
			strcpy(current_symbol, next_symbol);
			fscanf(kmap, "%x %c %s\n", &next_symbol_value,
							&type, next_symbol);
			continue;
		}
		lseek (fp , sizeof(int)+
				(current_symbol_value-code_offset)/step*
					sizeof(int) ,
			 SEEK_SET);
		has_read = read (fp , buffer , sizeof(buffer) );

		if (just_found_function) {
			just_found_function=0;
			show_finegrained_function(has_read, buffer, step,
				current_symbol, current_symbol_value);
		}

		for ( j = 0 ; j < has_read/(int)sizeof(int) ; j++)
			tiempo += buffer[j];

		if (tiempo != 0) {
			do_symbol(tiempo, current_symbol_value,
						current_symbol, type);
			total += tiempo;
		}
		if ((has_read < (next_symbol_value-current_symbol_value)/
				step*(int)sizeof(int)) || 
			next_symbol_value == current_symbol_value )
			break;

		strcpy ( current_symbol , next_symbol );
		current_symbol_value = next_symbol_value;
		fscanf(kmap , "%x %c %s\n" ,
			 &next_symbol_value , &type, next_symbol );
	}

	show_symbols(total);

	if ( abs((int)(profile_size-sizeof(int))/sizeof(int)*step
			-(end_address-code_offset)) > (int)step) {
		fprintf(stderr,"\n// WARNING: wrong _etext symbol.\n");
		fprintf(stderr,"//          (wrong System.map?)\n");
		exit(1);
	}
	if (!found_function) {
		fprintf(stderr,"\n// WARNING: Symbol '%s' not in System.map.\n",
				func);
		exit(1);
	}
	return(0);
}
