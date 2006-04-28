/*
 * virterror.h:
 * Summary: error handling interfaces for the libvirt library
 * Description: Provides the interfaces of the libvirt library to handle
 *              errors raised while using the library.
 *
 * Copy:  Copyright (C) 2006 Red Hat, Inc.
 *
 * See COPYING.LIB for the License of this software
 *
 * Author: Daniel Veillard <veillard@redhat.com>
 */

#ifndef __VIR_VIRERR_H__
#define __VIR_VIRERR_H__

#include "libvirt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * virErrorLevel:
 *
 * Indicates the level of an error
 */
typedef enum {
    VIR_ERR_NONE = 0,
    VIR_ERR_WARNING = 1,	/* A simple warning */
    VIR_ERR_ERROR = 2		/* An error */
} virErrorLevel;

/**
 * virErrorDomain:
 *
 * Indicates where an error may have come from
 */
typedef enum {
    VIR_FROM_NONE = 0,
    VIR_FROM_XEN,	/* Error at Xen hypervisor layer */
    VIR_FROM_XEND,	/* Error at connection with xend daemon */
    VIR_FROM_XENSTORE,	/* Error at connection with xen store */
    VIR_FROM_SEXPR,	/* Error in the S-Epression code */
    VIR_FROM_XML,	/* Error in the XML code */
    VIR_FROM_DOM,	/* Error when operating on a domain */
} virErrorDomain;


/**
 * virError:
 *
 * A libvirt Error instance.
 */

typedef struct _virError virError;
typedef virError *virErrorPtr;
struct _virError {
    int		code;	/* The error code, a virErrorNumber */
    int		domain;	/* What part of the library raised this error */
    char       *message;/* human-readable informative error message */
    virErrorLevel level;/* how consequent is the error */
    virConnectPtr conn;	/* the connection if available */
    virDomainPtr dom;	/* the domain if available */
    char       *str1;	/* extra string information */
    char       *str2;	/* extra string information */
    char       *str3;	/* extra string information */
    int		int1;	/* extra number information */
    int		int2;	/* extra number information */
};

/**
 * virErrorNumber:
 *
 * The full list of errors the library can generate
 */
typedef enum {
    VIR_ERR_OK = 0,
    VIR_ERR_INTERNAL_ERROR, /* internal error */
    VIR_ERR_NO_MEMORY,  /* memory allocation failure */
    VIR_ERR_NO_SUPPORT, /* no support for this connection */
    VIR_ERR_UNKNOWN_HOST,/* could not resolve hostname */
    VIR_ERR_NO_CONNECT, /* can't connect to hypervisor */
    VIR_ERR_INVALID_CONN,/* invalid connection object */
    VIR_ERR_INVALID_DOMAIN,/* invalid domain object */
    VIR_ERR_INVALID_ARG,/* invalid function argument */
    VIR_ERR_OPERATION_FAILED,/* a command to hypervisor failed */
    VIR_ERR_GET_FAILED,/* a HTTP GET command to failed */
    VIR_ERR_POST_FAILED,/* a HTTP POST command to failed */
    VIR_ERR_HTTP_ERROR,/* unexpected HTTP error code */
    VIR_ERR_SEXPR_SERIAL,/* failure to serialize an S-Expr */
    VIR_ERR_NO_XEN,/* could not open Xen hypervisor control */
    VIR_ERR_XEN_CALL,/* failure doing an hypervisor call */
    VIR_ERR_OS_TYPE, /* unknown OS type */
    VIR_ERR_NO_KERNEL, /* missing kernel information */
    VIR_ERR_NO_ROOT, /* missing root device information */
    VIR_ERR_NO_SOURCE, /* missing source device information */
    VIR_ERR_NO_TARGET, /* missing target device information */
    VIR_ERR_NO_NAME, /* missing domain name information */
    VIR_ERR_NO_OS, /* missing domain OS information */
    VIR_ERR_NO_DEVICE, /* missing domain devices information */
    VIR_ERR_NO_XENSTORE,/* could not open Xen Store control */
    VIR_ERR_DRIVER_FULL, /* too many drivers registered */
    VIR_ERR_CALL_FAILED, /* not supported by the drivers */
    VIR_ERR_XML_ERROR, /* an XML description is not well formed or broken */
    VIR_ERR_DOM_EXIST /* the domain already exist */
} virErrorNumber;

/**
 * virErrorFunc:
 * @userData:  user provided data for the error callback
 * @error:  the error being raised.
 *
 * Signature of a function to use when there is an error raised by the library.
 */
typedef void (*virErrorFunc) (void *userData, virErrorPtr error);

/*
 * Errors can be handled as asynchronous callbacks or after the routine
 * failed. They can also be handled globally at the library level, or
 * at the connection level (which then has priority
 */

virErrorPtr		virGetLastError		(void);
void			virResetLastError	(void);
void			virResetError		(virErrorPtr err);

virErrorPtr		virConnGetLastError	(virConnectPtr conn);
void			virConnResetLastError	(virConnectPtr conn);
int			virCopyLastError	(virErrorPtr to);

void			virDefaultErrorFunc	(virErrorPtr err);
void			virSetErrorFunc		(void *userData,
						 virErrorFunc handler);
void			virConnSetErrorFunc	(virConnectPtr conn,
						 void *userData,
						 virErrorFunc handler);
int			virConnCopyLastError	(virConnectPtr conn,
						 virErrorPtr to);
#ifdef __cplusplus
}
#endif

#endif /* __VIR_VIRERR_H__ */
