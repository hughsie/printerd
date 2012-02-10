/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

#ifndef __PD_ERROR_H__
#define __PD_ERROR_H__

#include <printerd/pd-types.h>

G_BEGIN_DECLS

/**
 * PD_ERROR:
 *
 * Error domain for printerd. Errors in this domain will be form the
 * #PdError enumeration. See #GError for more information on error
 * domains.
 */
#define PD_ERROR (pd_error_quark ())

GQuark pd_error_quark (void);

G_END_DECLS

#endif /* __PD_ERROR_H__ */
