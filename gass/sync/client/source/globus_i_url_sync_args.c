/*
 * Copyright 1999-2006 University of Chicago
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * CVS Information
 *
 * $Source$
 * $Date$
 * $Revision$
 */

#include "globus_common.h"
#include "globus_i_url_sync_args.h"
#include "globus_url.h"
#include "globus_libc.h"
#include "version.h"  /* provides local_version */
#include <getopt.h>
#include <ctype.h>
#include <stdio.h>

globus_url_t *	globus_i_url_sync_args_source		= GLOBUS_NULL;
globus_url_t *	globus_i_url_sync_args_destination	= GLOBUS_NULL;
globus_bool_t	globus_i_url_sync_args_verbose		= GLOBUS_FALSE;
globus_bool_t	globus_i_url_sync_args_debug		= GLOBUS_FALSE;
globus_bool_t	globus_i_url_sync_args_newer		= GLOBUS_FALSE;
globus_bool_t	globus_i_url_sync_args_older		= GLOBUS_FALSE;
globus_bool_t	globus_i_url_sync_args_size		= GLOBUS_FALSE;
globus_bool_t	globus_i_url_sync_args_cache		= GLOBUS_TRUE;
globus_bool_t	globus_i_url_sync_args_recurse		= GLOBUS_TRUE;
char * globus_i_url_sync_args_src_endpoint		= GLOBUS_NULL;
char * globus_i_url_sync_args_dst_endpoint		= GLOBUS_NULL;

static globus_url_t     globus_l_url_sync_args_source;
static globus_url_t     globus_l_url_sync_args_destination;

enum {
    arg_verbose = 1,
    arg_debug,
    arg_cache_off,
    arg_recurse,
    arg_size,
	arg_new,
	arg_old,
	arg_globus_endpoints,
    arg_num = arg_globus_endpoints
};

static globus_args_option_descriptor_t args_options[arg_num];
static char *verbose_args[] = {"-v", "-verbose", GLOBUS_NULL};
static char *debug_args[] = {"-d", "-debug", GLOBUS_NULL};
static char *cache_off_args[] = {"-c", "-connection-caching-off", GLOBUS_NULL};
static char *recurse_args[] = {"-r", "-recursive-dir-copy", GLOBUS_NULL};
static char *size_args[] = {"-s", "-size", GLOBUS_NULL};
static char *new_args[] = {"-n", "-newer", GLOBUS_NULL};
static char *old_args[] = {"-o", "-older", GLOBUS_NULL};
static char *globus_args[] = {"-g", "-globus-endpoints", GLOBUS_NULL};

static globus_args_option_descriptor_t verbose_def =
  {arg_verbose, verbose_args, 0, GLOBUS_NULL, GLOBUS_NULL};
static globus_args_option_descriptor_t debug_def =
  {arg_debug, debug_args, 0, GLOBUS_NULL, GLOBUS_NULL};
static globus_args_option_descriptor_t new_def =
{arg_new, new_args, 0, GLOBUS_NULL, GLOBUS_NULL};
static globus_args_option_descriptor_t old_def =
{arg_old, old_args, 0, GLOBUS_NULL, GLOBUS_NULL};
static globus_args_option_descriptor_t size_def =
  {arg_size, size_args, 0, GLOBUS_NULL, GLOBUS_NULL};
static globus_args_option_descriptor_t cache_off_def =
  {arg_cache_off, cache_off_args, 0, GLOBUS_NULL, GLOBUS_NULL};
static globus_args_option_descriptor_t recurse_def =
  {arg_recurse, recurse_args, 0, GLOBUS_NULL, GLOBUS_NULL};
static globus_args_option_descriptor_t globus_def =
{arg_globus_endpoints, globus_args, 2, GLOBUS_NULL, GLOBUS_NULL};

static char * usage_str= 
"globus_url_sync [-help | -usage] [-version] [-d | -v] [-c] [-r ] [-n] [-o] [-s] [-g <src> <dst>] <sourceURL> <destURL>";

static char * help_str= 
"\nglobus_url_sync [options] <sourceURL> <destURL>\n\n"
"OPTIONS\n"
"  -help | -usage\n"
"\tPrint help\n"
"  -version\n"
"\tPrint the version of this program\n"
"  -d | -debug | -v | -verbose\n"
"\tPrint additional detail.\n"
"  -c | -connection-caching-off\n"
"\tDisable GridFTP client connection caching.\n"
"  -r | -recursive-dir-copy\n"
"\tDisable GridFTP client connection caching.\n"
"  -g | -globus-endpoints <source endpoint> <destination endpoint>\n"
"\tFormat output using endpoints in place of the host portion of source and destination URLs.\n"
"  -n | -newer\n"
"\tFile is to be transferred, if the source timestamp is newer than the destination timestamp.\n"
"  -o | -older\n"
"\tFile is to be transferred, if the destination timestamp is older than source timestamp.\n"
"  -s | -size\n"
"\tFile is to be transferred, if the sizes of the source and the destination are not the same.\n"
"URL scheme(s) supported:\n"
"  gsiftp\n"
"    For example:\n"
"\tFile name, absolute path:\t\"gsiftp://myhost.edu//tmp/file1\"\n"
"\t\t\t\t\t\"gsiftp://myhost.edu/~/file1\"\n"
"\tFile name, relative path:\t\"gsiftp://myhost.edu/file1\"\n"
"\tDirectory, absolute path:\t\"gsiftp://myhost.edu//tmp/dir1/\"\n"
"  sshftp\n"
"    For example:\n"
"\tFile name, absolute path:\t\"sshftp://myhost.edu//tmp/file1\"\n"
"\t\t\t\t\t\"sshftp://myhost.edu/~/file1\"\n"
"\tFile name, relative path:\t\"sshftp://myhost.edu/file1\"\n"
"\tDirectory, absolute path:\t\"sshftp://myhost.edu//tmp/dir1/\"\n"
"\n"
;

/*
 * globus_l_url_sync_usage -- prints usage for this program
 */
static void
globus_l_url_sync_usage(char *prog)
{
    globus_libc_fprintf(stderr, usage_str, prog);
}
/* globus_l_url_sync_usage */

/*
 * globus_l_url_sync_badarg -- prints error for bad argument
 */
static void
globus_l_url_sync_badarg(char *badarg)
{
    globus_libc_fprintf(stderr, "Error, bad argument: %s\n", badarg);
}
/* globus_l_url_sync_badarg */

/*
 * globus_i_url_sync_parse_args -- parses command-line arguments
 */
globus_result_t
globus_i_url_sync_parse_args(
     int	argc,
     char	 *argv[])
{
    int		result;
    char	*program;
    globus_args_option_instance_t *instance = NULL;
    globus_list_t	*options_found = NULL,
					*list = NULL;
	
    /* Defaults */
    globus_i_url_sync_args_verbose  = GLOBUS_FALSE;
    globus_i_url_sync_args_debug    = GLOBUS_FALSE;
    globus_i_url_sync_args_newer    = GLOBUS_FALSE;
    globus_i_url_sync_args_older    = GLOBUS_FALSE;
    globus_i_url_sync_args_size     = GLOBUS_FALSE;
    globus_i_url_sync_args_cache    = GLOBUS_TRUE;
    globus_i_url_sync_args_recurse  = GLOBUS_TRUE;
	globus_i_url_sync_args_src_endpoint = GLOBUS_NULL;
	globus_i_url_sync_args_dst_endpoint = GLOBUS_NULL;
	
    /* determine the program name */
	program = strrchr(argv[0],'/');
    if (!program) {
        program = argv[0];
    } else {
		program++;
    }
	
    args_options[0] = verbose_def;
    args_options[1] = debug_def;
	args_options[2] = new_def;
	args_options[3] = old_def;
    args_options[4] = size_def;
    args_options[5] = cache_off_def;
    args_options[6] = recurse_def;
	args_options[7] = globus_def;
    if (globus_args_scan(
		&argc,
		&argv,
		arg_num,
		args_options,
		program,
		&local_version,
		usage_str,
		help_str,
		&options_found,
		NULL) < 0) 
    {
        /* error on argument line */
        return GLOBUS_FAILURE;
    }
    for (list = options_found;
		 !globus_list_empty(list);
		 list = globus_list_rest(list)) 
      {
	instance = globus_list_first(list);
		
	switch(instance->id_number) {
	  case arg_verbose:
	    globus_i_url_sync_args_verbose = GLOBUS_TRUE;
	    break;
	  case arg_debug:
	    globus_i_url_sync_args_debug = GLOBUS_TRUE;
	    break;
	  case arg_cache_off:
	    globus_i_url_sync_args_cache = GLOBUS_FALSE;
	    break;
	  case arg_recurse:
	    globus_i_url_sync_args_recurse = GLOBUS_FALSE;
	    break;
	  case arg_new:
		globus_i_url_sync_args_newer = GLOBUS_TRUE;
		break;
	  case arg_old:
		globus_i_url_sync_args_older = GLOBUS_TRUE;
		break;
	  case arg_size:
	    globus_i_url_sync_args_size = GLOBUS_TRUE;
	    break;
      case arg_globus_endpoints:
        globus_i_url_sync_args_src_endpoint = globus_libc_strdup(instance->values[0]);
		globus_i_url_sync_args_dst_endpoint = globus_libc_strdup(instance->values[1]);
        break;
	  default:
	    /* should not get here */
	    break;
	}
      }
	
    globus_args_option_instance_list_free(&options_found);
	
    /* argc should be equal to 3; argv[1] is source and argv[2] is destination */
	
    /* Get source, destination */
    if (argc != 3) {
        goto usage;
    }
	
    result = globus_url_parse(argv[1], &globus_l_url_sync_args_source);
    if (result != GLOBUS_URL_SUCCESS) {
        globus_l_url_sync_badarg(argv[1]);
        globus_url_destroy(&globus_l_url_sync_args_source);
        goto usage;
    }
    globus_i_url_sync_args_source = &globus_l_url_sync_args_source;
	
    if (globus_i_url_sync_args_debug)
        globus_libc_fprintf(stderr, "Source: %s\n", globus_l_url_sync_args_source);
	
    result = globus_url_parse(argv[2], &globus_l_url_sync_args_destination);
    if (result != GLOBUS_URL_SUCCESS) {
        globus_l_url_sync_badarg(argv[2]);
        globus_url_destroy(&globus_l_url_sync_args_source);
        globus_url_destroy(&globus_l_url_sync_args_destination);
        goto usage;
    }
    globus_i_url_sync_args_destination = &globus_l_url_sync_args_destination;
	
    if (globus_i_url_sync_args_debug)
      globus_libc_fprintf(stderr, "Destination: %s\n", globus_l_url_sync_args_destination);
	
    return GLOBUS_SUCCESS;
	
usage:
    globus_l_url_sync_usage(program);
    return GLOBUS_FAILURE;
}
/* globus_i_url_sync_parse_args */
