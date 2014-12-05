/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014 Tim Waugh <twaugh@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#if !defined (__PD_INSIDE_PRINTERD_H__) && !defined (PRINTERD_COMPILATION)
#error "Only <printerd/printerd.h> can be included directly."
#endif

#ifndef __PD_ENUMS_H__
#define __PD_ENUMS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * PdError:
 * @PD_ERROR_FAILED: The operation failed.
 * @PD_ERROR_CANCELLED: The operation was cancelled.
 * @PD_ERROR_UNIMPLEMENTED: The operation is not implemented.
 * @PD_ERROR_UNSUPPORTED_DOCUMENT_TYPE: The document type is not supported.
 *
 * Error codes for the #PD_ERROR error domain and the
 * corresponding D-Bus error names.
 */
typedef enum
{
  PD_ERROR_FAILED,                     /* org.freedesktop.printerd.Error.Failed */
  PD_ERROR_CANCELLED,                  /* org.freedesktop.printerd.Error.Cancelled */
  PD_ERROR_UNIMPLEMENTED,	       /* org.freedesktop.printerd.Error.Unimplemented */
  PD_ERROR_UNSUPPORTED_DOCUMENT_TYPE,  /* org.freedesktop.printerd.Error.UnsupportedDocumentType */
} PdError;

#define PD_ERROR_NUM_ENTRIES  (PD_ERROR_UNSUPPORTED_DOCUMENT_TYPE + 1)

/**
 * PdPrinterState:
 * @PD_PRINTER_STATE_IDLE: The printer is idle.
 * @PD_PRINTER_STATE_PROCESSING: The printer is processing a job.
 * @PD_PRINTER_STATE_STOPPED: The printer is stopped.
 *
 * Printer state values (from RFC 2911).
 */
typedef enum
{
  PD_PRINTER_STATE_IDLE = 3,
  PD_PRINTER_STATE_PROCESSING,
  PD_PRINTER_STATE_STOPPED,
} PdPrinterState;

/**
 * PdJobState:
 * @PD_JOB_STATE_PENDING: Candidate to start processing.
 * @PD_JOB_STATE_PENDING_HELD: There are reasons not to process this job.
 * @PD_JOB_STATE_PROCESSING: Job running.
 * @PD_JOB_STATE_PROCESSING_STOPPED: Processing is paused.
 * @PD_JOB_STATE_CANCELED: Terminal state, canceled.
 * @PD_JOB_STATE_ABORTED: Terminal state, aborted on error.
 * @PD_JOB_STATE_COMPLETED: Terminal state, normal completion.
 *
 * Job state values (from RFC 2911).
 */
typedef enum
{
	PD_JOB_STATE_PENDING = 3,	/* candidate to start processing */
	PD_JOB_STATE_PENDING_HELD,	/* reasons not to process */
	PD_JOB_STATE_PROCESSING,	/* job running */
	PD_JOB_STATE_PROCESSING_STOPPED,/* processing is paused */
	PD_JOB_STATE_CANCELED,		/* terminal state */
	PD_JOB_STATE_ABORTED,		/* terminal state */
	PD_JOB_STATE_COMPLETED,		/* terminal state */
} PdJobState;

G_END_DECLS

#endif /* __PD_ENUMS_H__ */
