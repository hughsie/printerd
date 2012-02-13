/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __PD_DAEMON_TYPES_H__
#define __PD_DAEMON_TYPES_H__

#include <gio/gio.h>
#include <polkit/polkit.h>
#include <printerd/printerd.h>
#include <gudev/gudev.h>

#include <sys/types.h>

struct _PdDaemon;
typedef struct _PdDaemon PdDaemon;

struct _PdEngine;
typedef struct _PdEngine PdEngine;

struct _PdManager;
typedef struct _PdManager PdManager;

#endif /* __PD_DAEMON_TYPES_H__ */
