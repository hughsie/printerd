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

#ifndef __PD_H__
#define __PD_H__

#if !defined(PRINTERD_API_IS_SUBJECT_TO_CHANGE) && !defined(PRINTERD_COMPILATION)
#error  libprinterd is unstable API. You must define PRINTERD_API_IS_SUBJECT_TO_CHANGE before including printerd/pd-.h
#endif

#define __PD_INSIDE_PRINTERD_H__
#include <printerd/pd-types.h>
#include <printerd/pd-enums.h>
#include <printerd/pd-enumtypes.h>
#include <printerd/pd-error.h>
#include <printerd/pd-generated.h>
#include <printerd/pd-client.h>
#undef __PD_INSIDE_PRINTERD_H__

#endif /* __PD_H__ */
