#ifndef GSS_EXT_COMPAT_H_
#define GSS_EXT_COMPAT_H_

/*
 * Provides a set of compatability fuctions to the
 * gss-extensions included in the globus gsi.
 */


/* 
 * Define windows specific needed parameters.
 */

#ifndef GSS_CALLCONV
#if defined(WIN32) || defined(_WIN32)
#define GSS_CALLCONV __stdcall
#define GSS_CALLCONV_C __cdecl
#else
#define GSS_CALLCONV 
#define GSS_CALLCONV_C
#endif
#endif /* GSS_CALLCONV */

#ifdef GSS_USE_FUNCTION_POINTERS
#define GSS_FUNC(f) (*f##_type)
#define GSS_MAKE_TYPEDEF typedef
#else
#define GSS_FUNC(f) f
#define GSS_MAKE_TYPEDEF
#endif

/*
 * First, include stddef.h to get size_t defined.
 */
#include <stddef.h>

/* define which removes the gss extensions definitions from
 * the gssapi header
 */
#define USE_ONLY_STANDARD_GSSAPI

#include "gssapi.h"

#ifndef EXTERN_C_BEGIN
#ifdef __cplusplus
#define EXTERN_C_BEGIN extern "C" {
#define EXTERN_C_END }
#else
#define EXTERN_C_BEGIN
#define EXTERN_C_END
#endif
#endif

EXTERN_C_BEGIN


/*
 * Error for compatability extensions
 */
#ifndef GSS_S_EXT_COMPAT
#define GSS_S_EXT_COMPAT (19ul << GSS_C_ROUTINE_ERROR_OFFSET)
#endif

typedef struct gss_buffer_set_desc_struct {
    size_t                              count;
    gss_buffer_t                        elements;
} gss_buffer_set_desc, *gss_buffer_set_t;

   GSS_MAKE_TYPEDEF
   OM_uint32 
   GSS_CALLCONV GSS_FUNC(gss_release_buffer_set)
              (OM_uint32 *,        /* minor_status */
               gss_buffer_set_t   /* buffer set to be freed */
              );

   GSS_MAKE_TYPEDEF
   OM_uint32 
   GSS_CALLCONV GSS_FUNC(gss_import_cred)
              (OM_uint32 *,        /* minor_status */
               gss_cred_id_t *,    /* cred to be exported */
               const gss_OID,      /* desired mech*/
               OM_uint32,          /* option req */
               const gss_buffer_t, /* import buffer */
               OM_uint32,          /* time req */
               OM_uint32 *         /* time rec */
              );

   GSS_MAKE_TYPEDEF
   OM_uint32
   GSS_CALLCONV GSS_FUNC(gss_export_cred)
              (OM_uint32 *,        /* minor_status */
               const gss_cred_id_t,/* cred_handle */
               const gss_OID,      /* desired mech */
               OM_uint32,          /* option req */
               gss_buffer_t        /* output buffer */
              );



   GSS_MAKE_TYPEDEF
   OM_uint32
   GSS_CALLCONV GSS_FUNC(gss_init_delegation)
              (OM_uint32 *,            /* minor_status */
               const gss_ctx_id_t,     /* context_handle */
               const gss_cred_id_t,    /* cred_handle */
               const gss_OID,          /* desired_mech */
               const gss_OID_set,      /* restriction_oids */
               const gss_buffer_set_t, /* restriction_buffers */
               const gss_buffer_t,     /* input_token */
               OM_uint32,              /* time_req */
               gss_buffer_t            /* output_token */
              );


   GSS_MAKE_TYPEDEF
   OM_uint32
   GSS_CALLCONV GSS_FUNC(gss_accept_delegation)
              (OM_uint32 *,            /* minor_status */
               const gss_ctx_id_t,     /* context_handle */
               const gss_OID_set,      /* restriction_oids */
               const gss_buffer_set_t, /* restriction_buffers */
               const gss_buffer_t,     /* input_token */
               OM_uint32,              /* time_req */
               OM_uint32 *,            /* time_rec */
               gss_cred_id_t *,        /* cred_handle */
               gss_OID *,              /* desired_mech */
               gss_buffer_t            /* output_token */
              );

   GSS_MAKE_TYPEDEF
   OM_uint32
   GSS_CALLCONV GSS_FUNC(gss_inquire_sec_context_by_oid)
              (OM_uint32 *,
               const gss_ctx_id_t,
               const gss_OID,
               gss_buffer_set_t
              );

   GSS_MAKE_TYPEDEF
   OM_uint32
   GSS_CALLCONV GSS_FUNC(gss_inquire_cred_by_oid)
              (OM_uint32 *,
               const gss_cred_id_t,
               const gss_OID,
               gss_buffer_set_t
              );

   GSS_MAKE_TYPEDEF
   OM_uint32
   GSS_CALLCONV GSS_FUNC(gss_set_sec_context_option)
    (OM_uint32 *,                       /* minor_status */
     gss_ctx_id_t *,                    /* context_handle */
     const gss_OID,                     /* option */
     const gss_buffer_t                 /* value */
    );

extern const gss_OID_desc * const GSS_DISALLOW_ENCRYPTION;
extern const gss_OID_desc * const GSS_PROTECTION_FAIL_ON_CONTEXT_EXPIRATION;
extern const gss_OID_desc * const GSS_APPLICATION_WILL_HANDLE_EXTENSIONS;

EXTERN_C_END

#endif /* GSS_EXT_COMPAT_H_ */
