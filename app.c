#include "app.h"

#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <termios.h>

const static int START_LEN = 10, LINESIZE = 1024;
const static char  COMMENT_START[] = "#;", SEPARATOR[] = "=";
const static char * YES[] = { "YES", "ON", "TRUE", NULL },
				* NO[] = { "NO", "OFF", "FALSE", NULL };

struct app_t {
	char * program_name;
	char * description;
	opt * options;
	int len, pos;
	app_callback on_error;
};

void app_die(const char * msg)
{
	fprintf(stderr,"%s\n", msg);
	exit(-1);
}

void app_assert(bool clause, const char * msg)
{
	if(!clause) {
		fprintf(stderr,"Assertion failed: ");
		app_die(msg);
	}
}

#define ASSERT(clause) app_assert( clause, #clause )

char* app_term_readline_from(FILE* stream) {
	char * buf = (char*) malloc(LINESIZE), * res;
	ASSERT(buf != NULL);
	res = fgets(buf, LINESIZE, stream);
	if(!res) free(buf);
	return res;
}

char* app_term_readline() {
	return app_term_readline_from(stdin);
}

app * app_new()
{
	app * this = (app*) malloc(sizeof(app));
	ASSERT(this != NULL);
	memset(this, 0, sizeof(app));
	this->len = START_LEN;
	this->pos=0;
	this->options = (opt*) malloc(START_LEN*sizeof(opt));
	ASSERT(this->options != NULL);
	memset(this->options, 0, START_LEN*sizeof(opt));
	return this;	
}

void app_free(app *this)
{
	if(!this) return;
	if(this->options) free(this->options);
	free(this);
}

char* as_long_opt(const char * ln)
{
	char * on;
	if(!ln) return strdup("");
	on = malloc(strlen(ln)+3);
	strcpy(on,"--");
	return strcat(on,ln);
}

void opt_display(opt * anopt)
{
	char * lo = as_long_opt(anopt->long_name);
	fprintf(stderr,"-%c %s\t%s\n",
		anopt->short_name,
		as_long_opt(anopt->long_name),
		(anopt->description ? anopt->description : "")
	);
	free(lo);
}

void app_auto_help(app* this, const char* theopt)
{
	int i;
	if(this->description) fprintf(stderr,"%s: %s\n", this->program_name, this->description);
	fprintf(stderr, "Usage: %s <options>\nOptions:\n", this->program_name);
	
	//list available options
	for(i=0; i<this->pos; ++i) opt_display(this->options+i);
}

void app_make_room_for_opt(app* theapp)
{
	if (theapp->pos >= theapp->len-1) {
		theapp->len*=2;
		theapp->options = realloc(theapp->options, theapp->len * sizeof(opt));
		ASSERT(theapp->options != NULL);
	}
}

void app_opt_add(app* theapp, opt* theopt)
{
	app_make_room_for_opt(theapp);
	memcpy(theapp->options + (theapp->pos++), theopt, sizeof(opt));
}

static opt auto_help_opt = {
	short_name: 'h',
	long_name: "help",
	type: OPT_CALLBACK,
	val: &app_auto_help,
	description: "(show this help message)"
};

void app_opt_add_help(app* this)
{
	app_opt_add(this, &auto_help_opt);
}

void app_opt_add_short(app* theapp, char optc, opt_type typ, void * v)
{
	opt * shopt = (opt*) malloc(sizeof(opt));
	shopt->short_name = optc;
	shopt->type = typ;
	shopt->val = v;
	shopt->description = NULL;
	app_opt_add (theapp,  shopt);
	free(shopt);
}

void app_opt_on_error(app* theapp, app_callback error_handler)
{
	theapp->on_error = error_handler;
}

void app_opt_default_error_handler(app* this, const char* theopt)
{
	fprintf(stderr, "ERROR: Wrong or invalid option '%s'\n\n", theopt);
	app_auto_help(this, theopt);
}

app_callback app_opt_error_handler = &app_opt_default_error_handler;
app_callback app_help = &app_auto_help;

void app_arg_required(app* this, const char * theopt)
{
	fprintf(stderr, "ERROR: Option '%s' requires an argument\n", theopt);
	this->on_error ? this->on_error(this, theopt) : app_auto_help(this, theopt);
}

void app_bad_value(app* this, const char * thekey, const char * theval)
{
	fprintf(stderr, "ERROR: Bad value '%s' for configuration key '%s'\n", theval, thekey);
}

void app_wipe(char * opt)
{
	memset(opt, 0, strlen(opt));
}

bool app_compare_opt(const char * arg, const opt * curopt)
{
	bool is_short = (arg[1]!='-');
	return ( is_short ?
		curopt->short_name == arg[1] && !arg[2] :
		curopt->long_name && !strcmp(curopt->long_name, arg+2)
	);                                            
}

bool app_parse_opts(app * theapp, int argc, char* argv[])
{
	int i=1, last_opt=0, pos;
	bool found;
	opt * curopt;
	app_callback cb;
	
	if(!argv) return false;
	theapp->program_name = basename(strdup(argv[0]));
	while( i < argc ) {
		//go to next opt, if not there
		while( i < argc && argv[i][0] != '-' ) ++i;
		if( i >= argc ) break;
		last_opt = i;
		
		//search for opt
		found = false; pos = 0;
		while(pos < theapp->pos && ! found) {
			curopt = theapp->options + pos;
			found = app_compare_opt(argv[i], curopt);
			if(!found) ++pos;
		}
		
		//handle opt
		if(!found) {
			if(theapp->on_error) theapp->on_error(theapp, argv[i]);
			return false;
		}
		switch(curopt->type) {
			case OPT_FLAG: 
				*(bool*)curopt->val = true;
				break;
			case OPT_INT:
				if(i==argc-1) {
					app_arg_required(theapp, argv[i]);
					return false;
				}
				*(int*)curopt->val = atoi(argv[++i]);
				break;
			case OPT_STRING:
				if(i==argc-1) {
					app_arg_required(theapp, argv[i]);
					return false;
				}
				*(char**)curopt->val = argv[++i];
				break;
			case OPT_PASSWD:
				if(i==argc-1) {
					app_arg_required(theapp, argv[i]);
					return false;
				}
				*(char**)curopt->val = strdup(argv[++i]);
				app_wipe(argv[i]);
				break;
			case OPT_CALLBACK:
				cb = (app_callback)curopt->val;
				cb(theapp, argv[i]);
				break;
			default:
				break;
		}
		++i;
	}
	return true;
}

void trim(char *s) {
        if (!s) return;
    char *p = s;
    int l = strlen(p);

    while(isspace(p[l - 1])) p[--l] = 0;
    while(* p && isspace(* p)) ++p, --l;

    memmove(s, p, l + 1);
}

void app_split_line(char * line, char **key, char **val)
{
	char * tms;
	*key = strtok_r(line, SEPARATOR, &tms); trim(*key);
	*val = strtok_r(NULL, SEPARATOR, &tms); trim(*val);
}

void strtoupper(char * str)
{
	int i, len = strlen(str);
	for(i=0; i<len; ++i) str[i]=toupper(str[i]);
}

bool is_true(const char * val)
{
	int i = 0, found = false;
	while ( YES[i] && ! found ) {
		if (!strcmp(YES[i],val)) found=true;
		else ++i;
	}
	return found;
}

bool is_false(const char * val)
{
	int i = 0, found = false;
	while ( NO[i] && ! found ) {
		if (!strcmp(NO[i],val)) found=true;
		else ++i;
	}
	return found;
}

bool app_parse_opts_from(app * theapp, FILE * file)
{
	int i=1, last_opt=0, pos;
	bool found;
	opt * curopt;
	app_callback cb;
	char * line;
	char * key = NULL, * val = NULL;
	
	while( line = app_term_readline_from(file) ) {
		if(strchr(COMMENT_START, line[0]) || !strlen(line)) {
			free(line);
			continue;
		}
		app_split_line(line, &key, &val);
				
		//search for opt
		found = false; pos = 0;
		if( key ) while(pos < theapp->pos && ! found) {
			curopt = theapp->options + pos;
			found = curopt->long_name && !strcmp(key, curopt->long_name);
			if(!found) ++pos;
		}

		//handle opt
		if(!found) {
			if(theapp->on_error) theapp->on_error(theapp, key);
			free(line);
			return false;
		}
		switch(curopt->type) {
			case OPT_FLAG: 
				// passing a value is optional, but if you do it
				// you must use one of the allowed values
				if ( !val ) { 
					*(bool*)curopt->val = true;
					break;
				}
				strtoupper(val);
				if(is_true(val)) *(bool*)curopt->val = true;
				else if(is_false(val)) *(bool*)curopt->val = false;
				else {
					app_bad_value(theapp, key, val);
					free(line);
					return false;
				}
				break;
			case OPT_INT:
				if(!val) {
					app_arg_required(theapp, key);
					free(line);
					return false;
				}
				*(int*)curopt->val = atoi(val);
				break;
			case OPT_STRING:
				if(!val) {
					app_arg_required(theapp, key);
					free(line);
					return false;
				}
				*(char**)curopt->val = val;
				break;
			case OPT_PASSWD:
				if(!val) {
					app_arg_required(theapp, key);
					free(line);
					return false;
				}
				*(char**)curopt->val = val;
				break;
			case OPT_CALLBACK:
				cb = (app_callback)curopt->val;
				cb(theapp, key);
				break;
			default:
				break;
		}
		free(line); 
	}
	return true;
}

void app_term_set_echo(bool enable)
{
  struct termios tio;
  int tty = fileno(stdin); //a better way?

  if(!tcgetattr(tty, &tio)) {
    if (enable) tio.c_lflag |= ECHO;
    else tio.c_lflag &= ~ECHO;

    tcsetattr(tty, TCSANOW, &tio);
  }
}

char * app_term_askpass(const char * what)
{
  char * val = malloc(512);
  printf("%s ",what);
  app_term_set_echo(0);
  scanf("%s",val);
  app_term_set_echo(1);
  printf("\n");
  return val;
}

const char * app_get_program_name(app * theapp)
{
	if(!theapp) return NULL;
	return theapp->program_name;
}
