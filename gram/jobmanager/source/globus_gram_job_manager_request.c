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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_gram_job_manager_request.c Globus Job Management Request
 *
 * CVS Information:
 * $Source$
 * $Date$
 * $Revision$
 * $Author$
 */

/*
 * Include header files
 */
#include "globus_common.h"
#include "globus_gram_protocol.h"
#include "globus_gram_job_manager.h"
#include "globus_rsl_assist.h"

#include <string.h>
#include <syslog.h>

static
int
globus_l_gram_symbol_table_populate(
    globus_gram_jobmanager_request_t *  request);

static
int
globus_l_gram_symboltable_add(
    globus_symboltable_t *              symbol_table,
    const char *                        symbol,
    const char *                        value);

static
void
globus_l_gram_log_rsl(
    globus_gram_jobmanager_request_t *  request,
    const char *                        label);

static
int
globus_l_gram_generate_id(
    globus_gram_jobmanager_request_t *  request,
    unsigned long *                     ulong1p,
    unsigned long *                     ulong2p);

static
int
globus_l_gram_init_cache(
    globus_gram_jobmanager_request_t *  request,
    char **                             cache_locationp,
    globus_gass_cache_t  *              cache_handlep);

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

/**
 * Allocate and initialize a request.
 *
 * This function allocates a new request structure and clears all of the
 * values in the structure. It also creates a script argument file which
 * will be used when the job request is submitted.
 *
 * @param request
 *        A pointer to a globus_gram_jobmanager_request_t pointer. This
 *        will be modified to point to a freshly allocated request structure.
 *
 * @return GLOBUS_SUCCESS on successfully initialization, or GLOBUS_FAILURE.
 */
int 
globus_gram_job_manager_request_init(
    globus_gram_jobmanager_request_t ** request,
    globus_gram_job_manager_t *         manager,
    char *                              rsl,
    gss_ctx_id_t                        response_ctx)
{
    globus_gram_jobmanager_request_t *  r;
    unsigned long                       ulong1, ulong2;
    int                                 rc;

    /*** creating request structure ***/
    r = malloc(sizeof(globus_gram_jobmanager_request_t));

    /* Order matches that of struct declaration in globus_gram_job_manager.h */
    r->config = manager->config;
    r->manager = manager;

    r->status = GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED;
    r->status_update_time = 0;
    r->failure_code = 0;
    /* Won't be set until job has been submitted to the LRM */
    r->job_id = NULL;
    r->poll_frequency = 30;
    r->local_stdout = NULL;
    r->local_stderr = NULL;
    r->dry_run = GLOBUS_FALSE;
    r->two_phase_commit = 0;
    r->commit_extend = 0;
    r->save_state = GLOBUS_TRUE;
    r->scratchdir = GLOBUS_NULL;
    globus_gram_job_manager_output_init(r);
    r->creation_time = time(NULL);
    r->queued_time = time(NULL);
    r->cache_tag = GLOBUS_NULL;
    r->stdout_position_hack = GLOBUS_NULL;
    r->stderr_position_hack = GLOBUS_NULL;
    rc = globus_symboltable_init(
            &r->symbol_table,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto symboltable_init_failed;
    }
    rc = globus_symboltable_create_scope(&r->symbol_table);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto symboltable_create_scope_failed;
    }
    rc = globus_l_gram_symbol_table_populate(r);
    if (rc != GLOBUS_SUCCESS)
    {
        goto symboltable_populate_failed;
    }
    r->rsl_spec = globus_libc_strdup(rsl);
    if (r->rsl_spec == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto rsl_spec_dup_failed;
    }

    globus_gram_job_manager_request_log(
            r,
            "Pre-parsed RSL string: %s\n",
            r->rsl_spec);
    
    r->rsl = globus_rsl_parse(r->rsl_spec);
    if (r->rsl == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto rsl_parse_failed;
    }
    globus_l_gram_log_rsl(r, "Job Request RSL");

    rc = globus_rsl_assist_attributes_canonicalize(r->rsl);
    if(rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto rsl_canonicalize_failed;
    }

    globus_l_gram_log_rsl(r, "Job Request RSL (canonical)");
    
    rc = globus_gram_job_manager_rsl_add_substitutions_to_symbol_table(r);
    if(rc != GLOBUS_SUCCESS)
    {
        goto add_substitutions_to_symbol_table_failed;
    }

    /* If this is a restart job, the id will come from the restart RSL
     * value; otherwise, it will be generated from current pid and time
     */
    rc = globus_l_gram_generate_id(
            r,
            &ulong1,
            &ulong2);

    /* Unique ID is used to have a handle to a job that has its state saved
     * and then the job is later restarted
     */
    r->uniq_id = globus_common_create_string(
            "%lu.%lu",
            ulong1,
            ulong2);
    if (r->uniq_id == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto failed_set_uniq_id;
    }
    /* The job contact is how the client is able to send signals or cancel this 
     * job.
     */
    r->job_contact = globus_common_create_string(
            "%s%lu/%lu/",
            r->manager->url_base,
            ulong1,
            ulong2);

    if (r->job_contact == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto failed_set_job_contact;
    }
    rc = globus_l_gram_symboltable_add(
            &r->symbol_table,
            "GLOBUS_GRAM_JOB_CONTACT",
            r->job_contact);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_add_contact_to_symboltable;
    }

    rc = setenv("GLOBUS_GRAM_JOB_CONTACT", r->job_contact, 1);
    if (rc != 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto failed_setenv_job_contact;
    }

    r->job_contact_path = globus_common_create_string(
            "/%lu/%lu/",
            ulong1,
            ulong2);
    if (r->job_contact_path == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto failed_set_job_contact_path;
    }
    
    r->remote_io_url = NULL;
    r->remote_io_url_file = NULL;
    r->x509_user_proxy = NULL;
    r->job_state_file = NULL;
    r->job_state_lock_file = NULL;
    r->job_state_lock_fd = -1;
    rc = globus_mutex_init(&r->mutex, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
        goto mutex_init_failed;
    }
    rc = globus_cond_init(&r->cond, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
        goto cond_init_failed;
    }
    r->client_contacts = NULL;
    r->stage_in_todo = NULL;
    r->stage_in_shared_todo = NULL;
    r->stage_out_todo = NULL;
    r->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_START;
    r->restart_state = GLOBUS_GRAM_JOB_MANAGER_STATE_START;
    r->unsent_status_change = GLOBUS_FALSE;
    r->poll_timer = GLOBUS_NULL_HANDLE;
    r->proxy_expiration_timer = GLOBUS_NULL_HANDLE;
    rc = globus_fifo_init(&r->pending_queries);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;

        goto pending_queries_init_failed;
    }
    r->relocated_proxy = GLOBUS_FALSE;
    r->proxy_timeout = 60;
    rc = globus_gram_job_manager_output_make_job_dir(r);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_make_job_dir;

    }
    r->streaming_requested = GLOBUS_FALSE;

    rc = globus_gram_job_manager_rsl_eval_string(
            r,
            r->config->scratch_dir_base,
            &r->scratch_dir_base);
    if(rc != GLOBUS_SUCCESS)
    {
        goto failed_eval_scratch_dir_base;
    }

    rc = globus_l_gram_init_cache(
            r,
            &r->cache_location,
            &r->cache_handle);
    if (rc != GLOBUS_SUCCESS)
    {
        goto init_cache_failed;
    }

    rc = globus_gram_job_manager_history_file_set(r);
    if (rc != GLOBUS_SUCCESS)
    {
        goto history_file_set_failed;
    }
    r->job_history_status = GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED;

    if (globus_gram_job_manager_rsl_attribute_exists(
            r->rsl,
            GLOBUS_GRAM_PROTOCOL_STDOUT_PARAM))
    {
        rc = globus_gram_job_manager_output_get_cache_name(
                r,
                GLOBUS_GRAM_PROTOCOL_STDOUT_PARAM,
                &r->local_stdout);
        if (rc != GLOBUS_SUCCESS)
        {
            goto failed_get_stdout_cache_name;
        }
        rc = globus_symboltable_insert(
                &r->symbol_table,
                "GLOBUS_CACHED_STDOUT",
                r->local_stdout);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto failed_insert_cached_stdout_into_symboltable;
        }
    }
    else
    {
        r->local_stdout = NULL;
    }

    if (globus_gram_job_manager_rsl_attribute_exists(
            r->rsl,
            GLOBUS_GRAM_PROTOCOL_STDERR_PARAM))
    {

        rc = globus_gram_job_manager_output_get_cache_name(
                r,
                "stderr",
                &r->local_stderr);
        if (rc != GLOBUS_SUCCESS)
        {
            goto failed_get_stderr_cache_name;
        }
        rc = globus_symboltable_insert(
                &r->symbol_table,
                "GLOBUS_CACHED_STDERR",
                r->local_stderr);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto failed_insert_cached_stderr_into_symboltable;
        }
    }
    else
    {
        r->local_stderr = NULL;
    }

    r->response_context = response_ctx;


    if (rc != GLOBUS_SUCCESS)
    {
failed_insert_cached_stderr_into_symboltable:
        if (r->local_stderr)
        {
            free(r->local_stderr);
        }
failed_get_stderr_cache_name:
failed_insert_cached_stdout_into_symboltable:
        if (r->local_stdout)
        {
            free(r->local_stdout);
        }
failed_get_stdout_cache_name:
        if (r->job_history_file)
        {
            free(r->job_history_file);
            r->job_history_file = NULL;
        }
history_file_set_failed:
        globus_gass_cache_close(&r->cache_handle);
        if (r->cache_location)
        {
            free(r->cache_location);
        }
init_cache_failed:
        if (r->scratch_dir_base)
        {
            free(r->scratch_dir_base);
        }
failed_eval_scratch_dir_base:
    /* TODO: Remove job dir */
failed_make_job_dir:
pending_queries_init_failed:
        globus_cond_destroy(&r->cond);
cond_init_failed:
        globus_mutex_destroy(&r->mutex);
mutex_init_failed:
        free(r->job_contact_path);
failed_set_job_contact_path:
failed_setenv_job_contact:
failed_add_contact_to_symboltable:
        free(r->job_contact);
failed_set_job_contact:
        free(r->uniq_id);
failed_set_uniq_id:
add_substitutions_to_symbol_table_failed:
rsl_canonicalize_failed:
        globus_rsl_free_recursive(r->rsl);
rsl_parse_failed:
        free(r->rsl_spec);
rsl_spec_dup_failed:
symboltable_populate_failed:
symboltable_create_scope_failed:
        globus_symboltable_destroy(&r->symbol_table);
symboltable_init_failed:
        free(r);
        r = NULL;
    }
    *request = r;
    return rc;
}
/* globus_gram_job_manager_request_init() */

/**
 * Deallocate memory related to a request.
 *
 * This function frees the data within the request, and then frees the request.
 * The caller must not access the request after this function has returned.
 *
 * @param request
 *        Job request to destroy.
 *
 * @return GLOBUS_SUCCESS
 */
void
globus_gram_job_manager_request_destroy(
    globus_gram_jobmanager_request_t *  request)
{
    if (!request)
    {
        return;
    }

    if (request->job_id)
    {
        free(request->job_id);
    }
    if (request->uniq_id)
    {
        free(request->uniq_id);
    }
    if (request->local_stdout)
    {
        free(request->local_stdout);
    }
    if (request->local_stderr)
    {
        free(request->local_stderr);
    }
    if (request->jm_restart)
    {
        free(request->jm_restart);
    }
    if (request->scratchdir)
    {
        free(request->scratchdir);
    }
    /* TODO: clean up: request->output? */
    /* TODO: clean up: request->cache_handle? */
    if (request->cache_tag)
    {
        free(request->cache_tag);
    }
    globus_symboltable_destroy(&request->symbol_table);
    if (request->rsl_spec)
    {
        free(request->rsl_spec);
    }
    if (request->rsl)
    {
        globus_rsl_free_recursive(request->rsl);
    }
    if (request->remote_io_url)
    {
        free(request->remote_io_url);
    }
    if (request->remote_io_url_file)
    {
        free(request->remote_io_url_file);
    }
    if (request->x509_user_proxy)
    {
        free(request->x509_user_proxy);
    }
    if (request->job_state_file)
    {
        free(request->job_state_file);
    }
    if (request->job_state_lock_file)
    {
        free(request->job_state_lock_file);
    }
    if (request->job_state_lock_fd >= 0)
    {
        close(request->job_state_lock_fd);
    }
    globus_mutex_destroy(&request->mutex);
    globus_cond_destroy(&request->cond);
    globus_gram_job_manager_contact_list_free(request);
    /* TODO: clean up request->stage_in_todo */
    /* TODO: clean up request->stage_in_shared_todo */
    /* TODO: clean up request->stage_in_out_todo */
    globus_assert(request->poll_timer == GLOBUS_NULL_HANDLE);
    globus_assert(request->proxy_expiration_timer == GLOBUS_NULL_HANDLE);
    if (request->job_contact)
    {
        free(request->job_contact);
    }
    if (request->job_contact_path)
    {
        free(request->job_contact_path);
    }
    /* TODO: clean up request->pending_queries */
    if (request->job_history_file)
    {
        free(request->job_history_file);
    }
    if (request->job_dir)
    {
        free(request->job_dir);
    }
}
/* globus_gram_job_manager_request_destroy() */

/**
 * Change the status associated with a job request
 *
 * Changes the status associated with a job request.
 * There is now additional tracking data associated with the
 * status that must be updated when the status is.  This function
 * handles managing it.  It is NOT recommended that you directly
 * change the status.
 *
 * @param request
 *        Job request to change status of.
 * @param status
 *        Status to set the job request to.
 *
 * @return GLOBUS_SUCCESS assuming valid input.
 *         If the request is null, returns GLOBUS_FAILURE.
 */
int
globus_gram_job_manager_request_set_status(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_job_state_t    status)
{
    return globus_gram_job_manager_request_set_status_time(
            request,
            status,
            time(0));
}
/* globus_gram_job_manager_request_set_status() */


/**
 * Change the status associated with a job request
 *
 * Changes the status associated with a job request.
 * There is now additional tracking data associated with the
 * status that must be updated when the status is.  This function
 * handles managing it.  It is NOT recommended that you directly
 * change the status.
 *
 * @param request
 *        Job request to change status of.
 * @param status
 *        Status to set the job request to.
 * @param valid_time
 *        The status is known good as of this time (seconds since epoch)
 *
 * @return GLOBUS_SUCCESS assuming valid input.
 *         If the request is null, returns GLOBUS_FAILURE.
 */
int
globus_gram_job_manager_request_set_status_time(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_job_state_t    status,
    time_t valid_time)
{
    if( ! request )
        return GLOBUS_FAILURE;
    request->status = status;
    request->status_update_time = valid_time;
    return GLOBUS_SUCCESS;
}
/* globus_gram_job_manager_request_set_status() */

/**
 * Write data to the job manager log file
 *
 * This function writes data to the passed file, using a printf format
 * string. Data is prefixed with a timestamp when written.
 *
 * @param log_fp
 *        Log file to write to.
 * @param format
 *        Printf-style format string to be written.
 * @param ...
 *        Parameters substituted into the format string, if needed.
 *
 * @return This function returns the value returned by vfprintf.
 */
int
globus_gram_job_manager_request_log(
    globus_gram_jobmanager_request_t *  request,
    const char *                        format,
    ... )
{
    struct tm *curr_tm;
    time_t curr_time;
    va_list ap;
    int rc;

    if (!request)
    {
        return -1;
    }

    if ( request->manager->jobmanager_log_fp == GLOBUS_NULL )
    {
        return -1;
    }

    time( &curr_time );
    curr_tm = localtime( &curr_time );

    fprintf(request->manager->jobmanager_log_fp,
         "%d/%d %02d:%02d:%02d ",
             curr_tm->tm_mon + 1, curr_tm->tm_mday,
             curr_tm->tm_hour, curr_tm->tm_min,
             curr_tm->tm_sec );

    va_start(ap, format);

    rc = vfprintf(request->manager->jobmanager_log_fp, format, ap);

    va_end(ap);

    return rc;
}
/* globus_gram_job_manager_request_log() */

/**
 * Write data to the job manager accounting file.
 * Also use syslog() to allow for easy central collection.
 *
 * This function writes data to the passed file descriptor, if any,
 * using a printf format string.
 * Data is prefixed with a timestamp when written.
 *
 * @param format
 *        Printf-style format string to be written.
 * @param ...
 *        Parameters substituted into the format string, if needed.
 *
 * @return This function returns the value returned by write().
 */
int
globus_gram_job_manager_request_acct(
    globus_gram_jobmanager_request_t *  request,
    const char *                        format,
    ... )
{
    static const char *jm_syslog_id  = "gridinfo";
    static int         jm_syslog_fac = LOG_DAEMON;
    static int         jm_syslog_lvl = LOG_NOTICE;
    static int         jm_syslog_init;
    struct tm *curr_tm;
    time_t curr_time;
    va_list ap;
    int rc = -1;
    int fd;
    const char * gk_acct_fd_var = "GATEKEEPER_ACCT_FD";
    const char * gk_acct_fd;
    int n;
    int t;
    char buf[1024 * 128];

    time( &curr_time );
    curr_tm = localtime( &curr_time );

    n = t = sprintf( buf, "JMA %04d/%02d/%02d %02d:%02d:%02d ",
                curr_tm->tm_year + 1900,
                curr_tm->tm_mon + 1, curr_tm->tm_mday,
                curr_tm->tm_hour, curr_tm->tm_min,
                curr_tm->tm_sec );

    va_start( ap, format );

    /*
     * FIXME: we should use vsnprintf() here...
     */

    n += vsprintf( buf + t, format, ap );

    if (!jm_syslog_init)
    {
        const char *s;

        if ((s = globus_libc_getenv( "JOBMANAGER_SYSLOG_ID"  )) != 0)
        {
            jm_syslog_id = *s ? s : 0;
        }

        if ((s = globus_libc_getenv( "JOBMANAGER_SYSLOG_FAC" )) != 0)
        {
            if (sscanf( s, "%u", &jm_syslog_fac ) != 1)
            {
                jm_syslog_id = 0;
            }
        }

        if ((s = globus_libc_getenv( "JOBMANAGER_SYSLOG_LVL" )) != 0) {
            if (sscanf( s, "%u", &jm_syslog_lvl ) != 1) {
                jm_syslog_id = 0;
            }
        }

        if (jm_syslog_id)
        {
            openlog( jm_syslog_id, LOG_PID, jm_syslog_fac );
        }

        jm_syslog_init = 1;
    }

    if (jm_syslog_id)
    {
        char *p, *q = buf;

        while ((p = q) < buf + n) {
            char c;

            while ((c = *q) != 0 && c != '\n') {
                q++;
            }

            *q = 0;

            syslog( jm_syslog_lvl, "%s", p );

            *q++ = c;
        }
    }

    if (!(gk_acct_fd = globus_libc_getenv( gk_acct_fd_var )))
    {
        return -1;
    }

    if (sscanf( gk_acct_fd, "%d", &fd ) != 1)
    {
        globus_gram_job_manager_request_log( request,
            "ERROR: %s has bad value: '%s'\n", gk_acct_fd_var, gk_acct_fd );
        return -1;
    }

    if (fcntl( fd, F_SETFD, FD_CLOEXEC ) < 0)
    {
        globus_gram_job_manager_request_log( request,
            "ERROR: cannot set FD_CLOEXEC on %s '%s': %s\n",
            gk_acct_fd_var, gk_acct_fd, strerror( errno ) );
    }

    if ((rc = write( fd, buf, n )) != n)
    {
        globus_gram_job_manager_request_log( request,
            "ERROR: only wrote %d bytes to %s '%s': %s\n%s\n",
            rc, gk_acct_fd_var, gk_acct_fd, strerror( errno ), buf + t );

        rc = -1;
    }

    return rc;
}
/* globus_gram_job_manager_request_acct() */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * Populate the request symbol table with values from the job manager config
 *
 * @param request
 *     Request to update the symbol table of
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 */
static
int
globus_l_gram_symbol_table_populate(
    globus_gram_jobmanager_request_t *  request)
{
    int                                 rc = GLOBUS_SUCCESS;

    rc = globus_l_gram_symboltable_add(
            &request->symbol_table,
            "HOME",
            request->config->home);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_insert_home;
    }

    rc = globus_l_gram_symboltable_add(
            &request->symbol_table,
            "LOGNAME",
            request->config->logname);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_insert_logname;
    }

    rc = globus_l_gram_symboltable_add(
            &request->symbol_table,
            "GLOBUS_ID",
            request->config->subject);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_insert_globusid;
    }

    rc = globus_l_gram_symboltable_add(
            &request->symbol_table,
            "GLOBUS_CONDOR_OS",
            request->config->condor_os);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_insert_condor_os;
    }
    rc = globus_l_gram_symboltable_add(
            &request->symbol_table,
            "GLOBUS_CONDOR_ARCH",
            request->config->condor_arch);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_insert_condor_arch;
    }

    rc = globus_l_gram_symboltable_add(
            &request->symbol_table,
            "GLOBUS_LOCATION",
            request->config->target_globus_location);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_symboltable_remove(&request->symbol_table, "GLOBUS_CONDOR_ARCH");
failed_insert_condor_arch:
        globus_symboltable_remove(&request->symbol_table, "GLOBUS_CONDOR_OS");
failed_insert_condor_os:
        globus_symboltable_remove(&request->symbol_table, "GLOBUS_ID");
failed_insert_globusid:
        globus_symboltable_remove(&request->symbol_table, "LOGNAME");
failed_insert_logname:
        globus_symboltable_remove(&request->symbol_table, "HOME");
    }
failed_insert_home:
    return rc;
}
/* globus_gram_symbol_table_populate() */

/**
 * Insert a symbol into the RSL evaluation symbol table
 * 
 * Also checks that the value is non-NULL and transforms the return
 * value to a GRAM error code.
 *
 * @param symboltable
 *     Symbol table to insert the value to.
 * @param symbol
 *     Symbol name to add to the table.
 * @pram value
 *     Symbol value to add to the table. If NULL nothing is inserted.
 * 
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 */
static
int
globus_l_gram_symboltable_add(
    globus_symboltable_t *              symbol_table,
    const char *                        symbol,
    const char *                        value)
{
    int                                 rc = GLOBUS_SUCCESS;

    if (value != NULL)
    {
        rc = globus_symboltable_insert(
            symbol_table,
            (void *) symbol,
            (void *) value);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        }
    }
    return rc;
}
/* globus_l_gram_symboltable_add() */

/**
 * Dump an RSL specification to the request's log file
 */
static
void
globus_l_gram_log_rsl(
    globus_gram_jobmanager_request_t *  request,
    const char *                        label)
{
    char *                              tmp_str;

    tmp_str = globus_rsl_unparse(request->rsl);

    if(tmp_str)
    {
        globus_gram_job_manager_request_log(
                request,
                "\n<<<<<%s\n%s\n"
                ">>>>>%s\n",
                label,
                tmp_str,
                label);

        globus_libc_free(tmp_str);
    }
}
/* globus_l_gram_log_rsl() */

static
int
globus_l_gram_generate_id(
    globus_gram_jobmanager_request_t *  request,
    unsigned long *                     ulong1p,
    unsigned long *                     ulong2p)
{
    int                                 rc = GLOBUS_SUCCESS;

    if(globus_gram_job_manager_rsl_need_restart(request))
    {
        globus_gram_job_manager_request_log(
                request,
                "Job Request is a Job Restart\n");

        /* Need to do this before unique id is set */
        rc = globus_gram_job_manager_rsl_eval_one_attribute(
                request,
                GLOBUS_GRAM_PROTOCOL_RESTART_PARAM,
                &request->jm_restart);

        if (rc != GLOBUS_SUCCESS)
        {
            goto failed_jm_restart_eval;
        }
        else if (request->jm_restart == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_RESTART;
            goto failed_jm_restart_eval;
        }
        globus_gram_job_manager_request_log(
                request,
                "Will try to restart job %s\n",
                request->jm_restart);

        rc = sscanf(
                request->jm_restart,
                "https://%*[^:]:%*d/%lu/%lu",
                ulong1p,
                ulong2p);

        if (rc < 2)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_RESTART;
            goto failed_jm_restart_scan;
        }
    }
    else
    {
        request->jm_restart = NULL;
        *ulong1p = (unsigned long) getpid();
        *ulong2p = (unsigned long) time(NULL);
    }

    if (rc != GLOBUS_SUCCESS)
    {
failed_jm_restart_scan:
        if (request->jm_restart != NULL)
        {
            free(request->jm_restart);
        }
    }
failed_jm_restart_eval:
    return rc;
}
/* globus_l_gram_generate_id() */

/**
 * Determine the cache location to use for this job
 *
 * If the gass_cache RSL attribute is present, it is evaluated and used.
 * Otherwise, if -cache-location was in the configuration, it used. Otherwise,
 * the GASS cache library default is used.
 *
 * As a side-effect, the GLOBUS_GASS_CACHE_DEFAULT environment variable is set
 * when the non-default value is to be used.
 *
 * @param request
 *     Request to use to find which value to use.
 * @param cache_locationp
 *     Pointer to set to the job-specific cache location.
 * @param cache_handlep
 *     Pointer to the GASS cache handle to initialize for this job
 *
 * @retval GLOBUS_SUCCESS
 *     Success.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_RSL_CACHE
 *     Invalid gass_cache RSL parameter.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_CACHE
 *     Invalid cache path.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_CACHE
 *     Error opening cache.
 */
static
int
globus_l_gram_init_cache(
    globus_gram_jobmanager_request_t *  request,
    char **                             cache_locationp,
    globus_gass_cache_t  *              cache_handlep)
{
    int                                 rc = GLOBUS_SUCCESS;

    if (globus_gram_job_manager_rsl_attribute_exists(
                request->rsl,
                GLOBUS_GRAM_PROTOCOL_GASS_CACHE_PARAM))
    {
        /* If gass_cache is in RSL, we'll evaluate that and use it. */
        rc = globus_gram_job_manager_rsl_eval_one_attribute(
                request,
                GLOBUS_GRAM_PROTOCOL_GASS_CACHE_PARAM,
                cache_locationp);

        if (rc != GLOBUS_SUCCESS)
        {
            goto failed_cache_eval;
        }

        /* cache location in rsl, but not a literal after eval */
        if ((*cache_locationp) == GLOBUS_NULL)
        {
            globus_gram_job_manager_request_log(
                    request,
                    "Poorly-formed RSL gass_cache attribute\n");

            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_CACHE;

            goto failed_cache_eval;
        }

        globus_gram_job_manager_request_log(
                request,
                "Overriding system gass_cache location %s "
                " with RSL-supplied %s\n",
                request->config->cache_location
                    ? request->config->cache_location : "NULL",
                *(cache_locationp));
    }
    else if (request->config->cache_location != NULL)
    {
        /* If -cache-location was on command-line or config file, then 
         * eval and use it
         */
        globus_gram_job_manager_request_log(
                request,
                "gass_cache location: %s\n",
                request->config->cache_location);

        rc = globus_gram_job_manager_rsl_eval_string(
                request,
                request->config->cache_location,
                cache_locationp);

        if(rc != GLOBUS_SUCCESS)
        {
            goto failed_cache_eval;
        }
        globus_gram_job_manager_request_log(
                request,
                "gass_cache location (post-eval): %s\n",
                *cache_locationp);
    }
    else
    {
        /* Use GASS-default location for the cache */
        *cache_locationp = NULL;
    }

    if (*cache_locationp)
    {
        rc = setenv("GLOBUS_GASS_CACHE_DEFAULT", *cache_locationp, 1);
        if (rc != 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto failed_cache_setenv;
        }
    }

    rc = globus_gass_cache_open(*cache_locationp, cache_handlep);
    if(rc != GLOBUS_SUCCESS)
    {
        if (*cache_locationp)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_CACHE;
        }
        else
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_CACHE;
        }

        goto failed_cache_open;
    }
failed_cache_open:
failed_cache_setenv:
        if (*cache_locationp)
        {
            free(*cache_locationp);
            *cache_locationp = NULL;
        }
failed_cache_eval:
    return rc;
}
/* globus_l_gram_init_cache() */

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
