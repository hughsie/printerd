/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Tim Waugh <twaugh@redhat.com>
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

#ifndef __PD_COMMON_H__
#define __PD_COMMON_H__

#include "pd-daemontypes.h"

G_BEGIN_DECLS

GHashTable	*pd_parse_ieee1284_id		(const gchar *idstring);
const gchar	*pd_job_state_as_string		(guint job_state);
const gchar	*pd_printer_state_as_string	(guint printer_state);
gchar		*pd_get_unix_user		(GDBusMethodInvocation *invocation);

G_END_DECLS

#endif /* __PD_COMMON_H__ */
