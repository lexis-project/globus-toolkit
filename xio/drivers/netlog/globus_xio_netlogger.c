/*
 * Portions of this file Copyright 1999-2005 University of Chicago
 * Portions of this file Copyright 1999-2005 The University of Southern California.
 *
 * This file or a portion of this file is licensed under the
 * terms of the Globus Toolkit Public License, found at
 * http://www.globus.org/toolkit/download/license.html.
 * If you redistribute this file, with or without
 * modifications, you must include this notice in the file.
 */

#include "globus_xio_netlogger.h"
#include "globus_xio_driver.h"
#include <stdarg.h>
#include "version.h"
#include "nl.h"
#include "nlsumm.h"
#include "nltransfer.h"

#define GlobusXIONetloggerError(_r)                                         \
    globus_error_put(GlobusXIONetloggerErrorObj(_r))

#define GlobusXIONetloggerErrorObj(_reason)                                 \
    globus_error_construct_error(                                           \
        GLOBUS_XIO_MODULE,                                                  \
        GLOBUS_NULL,                                                        \
        1,                                                                  \
        __FILE__,                                                           \
        _xio_name,                                                          \
        __LINE__,                                                           \
        _XIOSL(_reason))                                

GlobusDebugDefine(GLOBUS_XIO_NETLOGGER);
GlobusXIODeclareDriver(netlogger);

#define GlobusXIONetloggerDebugPrintf(level, message)                      \
    GlobusDebugPrintf(GLOBUS_XIO_NETLOGGER, level, message)

#define GlobusXIONetloggerDebugEnter()                                     \
    GlobusXIONetloggerDebugPrintf(                                         \
        GLOBUS_L_XIO_NETLOGGER_DEBUG_TRACE,                                \
        ("[%s] Entering\n", _xio_name))

#define GlobusXIONetloggerDebugExit()                                      \
    GlobusXIONetloggerDebugPrintf(                                         \
        GLOBUS_L_XIO_NETLOGGER_DEBUG_TRACE,                                \
        ("[%s] Exiting\n", _xio_name))

#define GlobusXIONetloggerDebugExitWithError()                             \
    GlobusXIONetloggerDebugPrintf(                                         \
        GLOBUS_L_XIO_NETLOGGER_DEBUG_TRACE,                                \
        ("[%s] Exiting with error\n", _xio_name))

enum globus_l_xio_netlogger_error_levels
{
    GLOBUS_L_XIO_NETLOGGER_DEBUG_TRACE                = 1,
    GLOBUS_L_XIO_NETLOGGER_DEBUG_INTERNAL_TRACE       = 2,
    GLOBUS_L_XIO_NETLOGGER_DEBUG_CNTLS                = 4
};

typedef struct xio_l_netlogger_handle_s
{
    int                                 block_id;
    int                                 log_flag;
    char *                              filename;
    char *                              id;
    char *                              type;

    NL_transfer_op_t                    read_event;
    NL_transfer_op_t                    write_event;

    NL_level_t                          nl_level;

    NL_log_T                            nl_log;
    NL_summ_T                           nl_summ;

    char *                              accept_start_event;
    char *                              accept_stop_event;
    char *                              open_start_event;
    char *                              open_stop_event;
    char *                              close_start_event;
    char *                              close_stop_event;
    char *                              read_start_event;
    char *                              read_stop_event;
    char *                              write_start_event;
    char *                              write_stop_event;

    globus_size_t                       read_buflen;
    globus_size_t                       write_buflen;
} xio_l_netlogger_handle_t;

static
globus_result_t
globus_l_xio_netlogger_attr_init(
    void **                             out_attr);

xio_l_netlogger_handle_t *     globus_l_xio_netlogger_default_handle = NULL;

static
xio_l_netlogger_handle_t *
xio_l_netlogger_create_handle(
    xio_l_netlogger_handle_t *          handle)
{
    char *                              hostname;
    GlobusXIOName(xio_l_netlogger_create_handle);

    GlobusXIONetloggerDebugEnter();

    handle->accept_start_event = globus_common_create_string(
        "xio.%s.accept.start", handle->type);
    handle->accept_stop_event = globus_common_create_string(
        "xio.%s.accept.stop", handle->type);
    handle->open_start_event = globus_common_create_string(
        "xio.%s.open.start", handle->type);
    handle->open_stop_event = globus_common_create_string(
        "xio.%s.open.stop", handle->type);
    handle->close_start_event = globus_common_create_string(
        "xio.%s.close.start", handle->type);
    handle->close_stop_event = globus_common_create_string(
        "xio.%s.close.stop", handle->type);
    handle->read_start_event = globus_common_create_string(
        "xio.%s.read.start", handle->type);
    handle->read_stop_event = globus_common_create_string(
        "xio.%s.read.stop", handle->type);
    handle->write_start_event = globus_common_create_string(
        "xio.%s.write.start", handle->type);
    handle->write_stop_event = globus_common_create_string(
        "xio.%s.write.stop", handle->type);

    handle->nl_level = NL_LVL_DEBUG;
    handle->nl_log = NL_open(handle->filename);
/*    NL_set_summ(handle->nl_log, NL_summ(NULL)); */
    NL_set_level(handle->nl_log, NL_LVL_INFO);

    NL_transfer_init(handle->nl_summ, 0, NL_LVL_DEBUG);
    NL_summ_add_log(handle->nl_summ, handle->nl_log);

    hostname = NL_get_ipaddr(); /* defined in nl_log.h */
    if(hostname == NULL)
    {
        hostname = strdup("0.0.0.0");
    }

/*    NL_open(handle->nl_log, 0, handle->filename);  */
    NL_set_const(handle->nl_log, 0, "HOST:s", hostname);
    NL_set_level(handle->nl_log, NL_LVL_INFO);

    handle->read_event = NL_TRANSFER_NET_READ;
    handle->write_event = NL_TRANSFER_NET_WRITE;

    GlobusXIONetloggerDebugExit();
    return handle;
}

static
int
globus_l_xio_netlogger_activate(void);

static
int
globus_l_xio_netlogger_deactivate(void);

GlobusXIODefineModule(netlogger) =
{
    "globus_xio_netlogger",
    globus_l_xio_netlogger_activate,
    globus_l_xio_netlogger_deactivate,
    NULL,
    NULL,
    &local_version
};

static
int
globus_l_xio_netlogger_activate(void)
{
    xio_l_netlogger_handle_t *          handle;
    int rc;
    GlobusXIOName(globus_l_xio_netlogger_activate);

    GlobusDebugInit(GLOBUS_XIO_NETLOGGER, TRACE);
    GlobusXIONetloggerDebugEnter();
    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto error_xio_system_activate;
    }
    GlobusXIORegisterDriver(netlogger);

    handle = (xio_l_netlogger_handle_t *)
        globus_calloc(1, sizeof(xio_l_netlogger_handle_t));

    globus_l_xio_netlogger_attr_init((void **)&handle);

    globus_l_xio_netlogger_default_handle = 
        xio_l_netlogger_create_handle(handle);

    GlobusXIONetloggerDebugExit();
    return GLOBUS_SUCCESS;

error_xio_system_activate:
    GlobusXIONetloggerDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_NETLOGGER);
    return rc;
}

static
int
globus_l_xio_netlogger_deactivate(void)
{   
    int rc;
    GlobusXIOName(globus_l_xio_netlogger_deactivate);
    
    GlobusXIONetloggerDebugEnter();
    GlobusXIOUnRegisterDriver(netlogger);
    rc = globus_module_deactivate(GLOBUS_XIO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {   
        goto error_deactivate;
    }
    GlobusXIONetloggerDebugExit();
    GlobusDebugDestroy(GLOBUS_XIO_NETLOGGER);
    return GLOBUS_SUCCESS;

error_deactivate:
    GlobusXIONetloggerDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_NETLOGGER);
    return rc;
}

static
globus_result_t
globus_l_xio_netlogger_attr_init(
    void **                             out_attr)
{
    int                                 rc;
    globus_uuid_t                       uuid;
    xio_l_netlogger_handle_t *          attr;

    /* intiialize everything to 0 */
    attr = (xio_l_netlogger_handle_t *)
        globus_calloc(1, sizeof(xio_l_netlogger_handle_t));
    attr->type = strdup("default");

    rc = globus_uuid_create(&uuid);
    if(rc == 0)
    {
        attr->id = strdup(uuid.text);
    }
    else
    {
        attr->id = strdup("default");
    }
    attr->filename = NULL;

    *out_attr = attr;

    return GLOBUS_SUCCESS;
}

static
globus_result_t
globus_l_xio_netlogger_attr_copy(
    void **                             dst,
    void *                              src)
{
    xio_l_netlogger_handle_t *          dst_attr;
    xio_l_netlogger_handle_t *          src_attr;

    src_attr = (xio_l_netlogger_handle_t *) src;
    /* intiialize everything to 0 */
    globus_l_xio_netlogger_attr_init((void **)&dst_attr);

    dst_attr->log_flag = src_attr->log_flag;
    if(src_attr->filename != NULL)
    {
        dst_attr->filename = strdup(src_attr->filename);
    }
    if(src_attr->type != NULL)
    {
        dst_attr->type = strdup(src_attr->type);
    }
    if(src_attr->id != NULL)
    {
        dst_attr->id = strdup(src_attr->id);
    }
    *dst = dst_attr;

    return GLOBUS_SUCCESS;
}

static globus_xio_string_cntl_table_t  netlog_l_string_opts_table[] =
{
    {"filename",
        GLOBUS_XIO_NETLOGGER_CNTL_SET_FILENAME, globus_xio_string_cntl_string},
    {"mask", GLOBUS_XIO_NETLOGGER_CNTL_SET_MASK, globus_xio_string_cntl_int},
    {"type", GLOBUS_XIO_NETLOGGER_CNTL_SET_TYPE, globus_xio_string_cntl_int},
    {"id", GLOBUS_XIO_NETLOGGER_CNTL_SET_ID, globus_xio_string_cntl_string},
    {NULL, 0, NULL}
};

static
globus_result_t
globus_l_xio_netlogger_cntl(
    void  *                             driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    char *                              str;
    char *                              tmp_str;
    globus_xio_netlogger_log_event_t    event;
    xio_l_netlogger_handle_t *          attr;
    GlobusXIOName(globus_l_xio_netlogger_cntl);

    GlobusXIONetloggerDebugEnter();

    attr = (xio_l_netlogger_handle_t *) driver_attr;

    switch(cmd)
    {
        case GLOBUS_XIO_NETLOGGER_CNTL_SET_ID:
            str = va_arg(ap, char *);
            attr->id = strdup(str);
            break;

        case GLOBUS_XIO_NETLOGGER_CNTL_SET_FILENAME:
            str = va_arg(ap, char *);
            attr->filename = strdup(str);
            break;

        case GLOBUS_XIO_NETLOGGER_CNTL_SET_MASK:
            attr->log_flag = va_arg(ap, int);
            break;

        case GLOBUS_XIO_NETLOGGER_CNTL_EVENT_ON:
            event = va_arg(ap, globus_xio_netlogger_log_event_t);
            attr->log_flag |= event;
            break;

        case GLOBUS_XIO_NETLOGGER_CNTL_EVENT_OFF:
            event = va_arg(ap, globus_xio_netlogger_log_event_t);
            attr->log_flag ^= event;
            break;

        case GLOBUS_XIO_NETLOGGER_CNTL_SET_TRANSFER_ID:
            tmp_str = va_arg(ap, char *);

            if(attr->id != NULL)
            {
                free(attr->id);
            }
            attr->id = strdup(tmp_str);
            GlobusXIONetloggerDebugPrintf(GLOBUS_L_XIO_NETLOGGER_DEBUG_CNTLS,
                ("GLOBUS_XIO_NETLOGGER_CNTL_SET_TRANSFER_ID: %s\n", attr->id));
            break;

        case GLOBUS_XIO_NETLOGGER_CNTL_SET_TYPE:
            tmp_str = va_arg(ap, char *);

            if(attr->type != NULL)
            {
                free(attr->type);
            }
            attr->type = strdup(tmp_str);
            GlobusXIONetloggerDebugPrintf(GLOBUS_L_XIO_NETLOGGER_DEBUG_CNTLS,
            ("GLOBUS_XIO_NETLOGGER_CNTL_SET_TRANSFER_TYPE: %d\n", attr->type));
            break;

        case GLOBUS_XIO_NETLOGGER_CNTL_SET_SUM_TYPES:
            attr->read_event = va_arg(ap, NL_transfer_op_t);
            attr->write_event = va_arg(ap, NL_transfer_op_t);
            break;

        case GLOBUS_XIO_NETLOGGER_CNTL_SET_STRING_SUM_TYPES:
            tmp_str = va_arg(ap, char *);

            if(strncmp("disk", tmp_str, 4) == 0)
            {
                attr->read_event = NL_TRANSFER_DISK_READ;
                attr->write_event = NL_TRANSFER_DISK_WRITE;
            }
            else
            {
                attr->read_event = NL_TRANSFER_NET_READ;
                attr->write_event = NL_TRANSFER_NET_WRITE;
            }
            break;
    }

    GlobusXIONetloggerDebugExit();
    return GLOBUS_SUCCESS;
}

static
globus_result_t
globus_l_xio_netlogger_handle_destroy(
    void *                              driver_attr)
{
    xio_l_netlogger_handle_t *          attr;

    if(driver_attr == NULL)
    {
        return GLOBUS_SUCCESS;
    }

    attr = (xio_l_netlogger_handle_t *) driver_attr;
    if(attr->type != NULL)
    {
        globus_free(attr->type);
    }
    if(attr->id != NULL)
    {
        globus_free(attr->id);
    }
    if(attr->filename != NULL)
    {
        globus_free(attr->filename);
    }
    if(attr->open_start_event != NULL)
    {
        globus_free(attr->open_start_event);
    }
    if(attr->open_stop_event != NULL)
    {
        globus_free(attr->open_stop_event);
    }
    if(attr->close_start_event != NULL)
    {
        globus_free(attr->close_start_event);
    }
    if(attr->close_stop_event != NULL)
    {
        globus_free(attr->close_stop_event);
    }
    if(attr->read_start_event != NULL)
    {
        globus_free(attr->read_start_event);
    }
    if(attr->read_stop_event != NULL)
    {
        globus_free(attr->read_stop_event);
    }
    if(attr->write_start_event != NULL)
    {
        globus_free(attr->write_start_event);
    }
    if(attr->write_stop_event != NULL)
    {
        globus_free(attr->write_stop_event);
    }

    globus_free(attr);
    return GLOBUS_SUCCESS;
}

static
globus_result_t
globus_l_xio_netlogger_server_init(
    void *                              driver_attr,
    const globus_xio_contact_t *        contact_info,
    globus_xio_operation_t              op)
{
    xio_l_netlogger_handle_t *          handle;
    xio_l_netlogger_handle_t *          cpy_handle;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_netlogger_server_init);

    GlobusXIONetloggerDebugEnter();

    /* first copy attr if we have it */
    if(driver_attr != NULL)
    {
        cpy_handle = (xio_l_netlogger_handle_t *) driver_attr;
    }
    /* else copy the default attr */
    else
    {
        cpy_handle = globus_l_xio_netlogger_default_handle;
    }

    globus_l_xio_netlogger_attr_copy((void **)&handle, (void *)cpy_handle);
    res = globus_xio_driver_pass_server_init(op, contact_info, handle);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_pass;
    }
    GlobusXIONetloggerDebugExit();

    return GLOBUS_SUCCESS;
error_pass:
    GlobusXIONetloggerDebugExitWithError();
    return res;
}

static
void
globus_l_xio_netlogger_accept_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    xio_l_netlogger_handle_t *          handle;
    GlobusXIOName(globus_l_xio_netlogger_accept_cb);

    GlobusXIONetloggerDebugEnter();
    handle = (xio_l_netlogger_handle_t *) user_arg;
    if(handle->log_flag & GLOBUS_XIO_NETLOGGER_LOG_ACCEPT)
    {
        NL_write(handle->nl_log, NL_LVL_INFO, handle->accept_stop_event,
            "sock=i uuid=s",
            (int)handle, handle->id);
    }

    globus_xio_driver_finished_accept(op, user_arg, result);
    GlobusXIONetloggerDebugExit();
}

static
globus_result_t
globus_l_xio_netlogger_accept(
    void *                              driver_server,
    globus_xio_operation_t              op)
{
    xio_l_netlogger_handle_t *          cpy_handle;
    xio_l_netlogger_handle_t *          handle;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_netlogger_accept);

    GlobusXIONetloggerDebugEnter();

    cpy_handle = (xio_l_netlogger_handle_t *) driver_server;
    globus_l_xio_netlogger_attr_copy((void **)&handle, (void *)cpy_handle);
    xio_l_netlogger_create_handle(handle);
    if(handle->log_flag & GLOBUS_XIO_NETLOGGER_LOG_ACCEPT)
    {
        NL_write(handle->nl_log, NL_LVL_INFO,
            handle->accept_start_event, "sock=i uuid=s",
            (int)handle, handle->id);
    }
    res = globus_xio_driver_pass_accept(
        op, globus_l_xio_netlogger_accept_cb, handle);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_pass;
    }

    GlobusXIONetloggerDebugExit();
error_pass:
    GlobusXIONetloggerDebugExitWithError();
    return res;

    return GLOBUS_SUCCESS;
}

static
void
globus_l_xio_netlogger_open_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    xio_l_netlogger_handle_t *          handle;
    GlobusXIOName(globus_l_xio_netlogger_open_cb);

    GlobusXIONetloggerDebugEnter();
    handle = (xio_l_netlogger_handle_t *) user_arg;
    if(handle->log_flag & GLOBUS_XIO_NETLOGGER_LOG_OPEN)
    {
        NL_write(handle->nl_log, NL_LVL_INFO, handle->open_stop_event,
            "sock=i uuid=s",
            (int)handle, handle->id);
    }

    globus_xio_driver_finished_open(user_arg, op, result);
    GlobusXIONetloggerDebugExit();
}


static
globus_result_t
globus_l_xio_netlogger_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    xio_l_netlogger_handle_t *          cpy_handle;
    xio_l_netlogger_handle_t *          handle;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_netlogger_open);

    GlobusXIONetloggerDebugEnter();

    /* then go to link */
    if(driver_link != NULL)
    {
        cpy_handle = (xio_l_netlogger_handle_t *) driver_link;
    }
    /* first copy attr if we have it */
    if(driver_attr != NULL)
    {
        cpy_handle = (xio_l_netlogger_handle_t *) driver_attr;
    }
    /* else copy the default attr */
    else
    {
        cpy_handle = globus_l_xio_netlogger_default_handle;
    }
    globus_l_xio_netlogger_attr_copy((void **)&handle, (void *)cpy_handle);
    xio_l_netlogger_create_handle(handle);

    if(handle->log_flag & GLOBUS_XIO_NETLOGGER_LOG_OPEN)
    {
        NL_write(handle->nl_log, NL_LVL_INFO, handle->open_start_event,
            "sock=i uuid=s",
            (int)handle, handle->id);
    }
    res = globus_xio_driver_pass_open(
        op, contact_info, globus_l_xio_netlogger_open_cb, handle);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_pass;
    }

    GlobusXIONetloggerDebugExit();

    return GLOBUS_SUCCESS;
error_pass:
    GlobusXIONetloggerDebugExitWithError();
    return res;
}

static
void
globus_l_xio_netlogger_read_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    xio_l_netlogger_handle_t *          handle;
    GlobusXIOName(globus_l_xio_netlogger_read_cb);

    GlobusXIONetloggerDebugEnter();
    handle = (xio_l_netlogger_handle_t *) user_arg;
    if(handle->log_flag & GLOBUS_XIO_NETLOGGER_LOG_READ)
    {
        NL_transfer_end(
            handle->nl_log,
            handle->nl_level,
            handle->read_event,
            handle->id,
            (int)handle,
            handle->block_id,
            (double)nbytes);
        /* next line is thread safe because only permit serial access */
        handle->block_id++;
    }

    globus_xio_driver_finished_read(op, result, nbytes);
    GlobusXIONetloggerDebugExit();
}


static
globus_result_t
globus_l_xio_netlogger_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    xio_l_netlogger_handle_t *          handle;
    globus_size_t                       wait_for;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_netlogger_read);

    GlobusXIONetloggerDebugEnter();

    handle = (xio_l_netlogger_handle_t *) driver_specific_handle;
    if(handle->log_flag & GLOBUS_XIO_NETLOGGER_LOG_READ)
    {
        GlobusXIOUtilIovTotalLength(handle->read_buflen, iovec, iovec_count);
        NL_transfer_start(
            handle->nl_log,
            handle->nl_level, 
            handle->read_event, 
            handle->id,
            (int)handle,
            handle->block_id);
    }
    wait_for = globus_xio_operation_get_wait_for(op);
    res = globus_xio_driver_pass_read(op,
        (globus_xio_iovec_t *)iovec, iovec_count, wait_for,
        globus_l_xio_netlogger_read_cb, handle);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_pass;
    }

    GlobusXIONetloggerDebugExit();

    return GLOBUS_SUCCESS;
error_pass:
    GlobusXIONetloggerDebugExitWithError();
    return res;
}

static
void
globus_l_xio_netlogger_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    xio_l_netlogger_handle_t *          handle;
    GlobusXIOName(globus_l_xio_netlogger_write_cb);

    GlobusXIONetloggerDebugEnter();
    handle = (xio_l_netlogger_handle_t *) user_arg;
    if(handle->log_flag & GLOBUS_XIO_NETLOGGER_LOG_WRITE)
    {
        NL_transfer_end(
            handle->nl_log,
            handle->nl_level,
            handle->write_event,
            handle->id,
            (int)handle,
            handle->block_id,
            (double)nbytes);
        /* next line is thread safe because only permit serial access */
        handle->block_id++;
    }

    globus_xio_driver_finished_write(op, result, nbytes);
    GlobusXIONetloggerDebugExit();
}

static
globus_result_t
globus_l_xio_netlogger_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    xio_l_netlogger_handle_t *          handle;
    globus_size_t                       wait_for;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_netlogger_write);

    GlobusXIONetloggerDebugEnter();

    handle = (xio_l_netlogger_handle_t *) driver_specific_handle;
    if(handle->log_flag & GLOBUS_XIO_NETLOGGER_LOG_WRITE)
    {
        GlobusXIOUtilIovTotalLength(handle->write_buflen, iovec, iovec_count);
        NL_transfer_start(
            handle->nl_log,
            handle->nl_level,
            handle->write_event,
            handle->id,
            (int)handle,
            handle->block_id);
    }
    wait_for = globus_xio_operation_get_wait_for(op);
    res = globus_xio_driver_pass_write(op,
        (globus_xio_iovec_t *)iovec, iovec_count, wait_for,
        globus_l_xio_netlogger_write_cb, handle);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_pass;
    }

    GlobusXIONetloggerDebugExit();

    return GLOBUS_SUCCESS;
error_pass:
    GlobusXIONetloggerDebugExitWithError();
    return res;
}

static
void
globus_l_xio_netlogger_close_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    xio_l_netlogger_handle_t *          handle;
    GlobusXIOName(globus_l_xio_netlogger_close_cb);

    GlobusXIONetloggerDebugEnter();
    handle = (xio_l_netlogger_handle_t *) user_arg;
    if(handle->log_flag & GLOBUS_XIO_NETLOGGER_LOG_CLOSE)
    {
        NL_write(handle->nl_log, NL_LVL_INFO, handle->close_stop_event,
            "sock=i uuid=s",
            (int)handle, handle->id);
    }

    NL_transfer_finalize(handle->nl_summ);
    NL_close(handle->nl_log);

    globus_xio_driver_finished_close(op, result);
    GlobusXIONetloggerDebugExit();
}


static
globus_result_t
globus_l_xio_netlogger_close(
    void *                              driver_handle,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    xio_l_netlogger_handle_t *          handle;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_netlogger_close);

    GlobusXIONetloggerDebugEnter();

    handle = (xio_l_netlogger_handle_t *) driver_handle;
    if(handle->log_flag & GLOBUS_XIO_NETLOGGER_LOG_CLOSE)
    {
        NL_write(handle->nl_log, NL_LVL_INFO, handle->close_start_event,
            "sock=i uuid=s",
            (int)handle, handle->id);
    }
    res = globus_xio_driver_pass_close(
        op, globus_l_xio_netlogger_close_cb, handle);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_pass;
    }

    GlobusXIONetloggerDebugExit();

    return GLOBUS_SUCCESS;
error_pass:
    GlobusXIONetloggerDebugExitWithError();
    return res;
}

static
globus_result_t
globus_l_xio_netlogger_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_netlogger_init);

    GlobusXIONetloggerDebugEnter();
    result = globus_xio_driver_init(&driver, "netlogger", NULL);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_driver_init", result);
        goto error_init;
    }
    globus_xio_driver_set_transform(
        driver,
        globus_l_xio_netlogger_open,
        globus_l_xio_netlogger_close,
        globus_l_xio_netlogger_read,
        globus_l_xio_netlogger_write,
        globus_l_xio_netlogger_cntl,
        NULL);

    globus_xio_driver_set_server(
        driver,
        globus_l_xio_netlogger_server_init,
        globus_l_xio_netlogger_accept,
        globus_l_xio_netlogger_handle_destroy,
        /* all controls are the same */
        globus_l_xio_netlogger_cntl,
        globus_l_xio_netlogger_cntl,
        globus_l_xio_netlogger_handle_destroy);

    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_netlogger_attr_init,
        globus_l_xio_netlogger_attr_copy,
        /* attr and handle same struct, same controls */
        globus_l_xio_netlogger_cntl,
        globus_l_xio_netlogger_handle_destroy);

    globus_xio_driver_string_cntl_set_table(
        driver,
        netlog_l_string_opts_table);

    *out_driver = driver;
    GlobusXIONetloggerDebugExit();
    return GLOBUS_SUCCESS;

error_init:
    GlobusXIONetloggerDebugExitWithError();
    return result;
}

static
void
globus_l_xio_netlogger_destroy(
    globus_xio_driver_t                 driver)
{
    globus_xio_driver_destroy(driver);
}

GlobusXIODefineDriver(
    netlogger,
    globus_l_xio_netlogger_init,
    globus_l_xio_netlogger_destroy);
