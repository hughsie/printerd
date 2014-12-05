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

#ifndef __PD_JOB_IMPL_H__
#define __PD_JOB_IMPL_H__

#include "pd-daemontypes.h"

G_BEGIN_DECLS

#define PD_TYPE_JOB_IMPL	(pd_job_impl_get_type ())
#define PD_JOB_IMPL(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), PD_TYPE_JOB_IMPL, PdJobImpl))
#define PD_IS_JOB_IMPL(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PD_TYPE_JOB_IMPL))

GType		 pd_job_impl_get_type		(void) G_GNUC_CONST;
PdDaemon	*pd_job_impl_get_daemon		(PdJobImpl *job);
void		 pd_job_impl_set_attribute	(PdJobImpl *job,
						 const gchar *name,
						 GVariant *value);
void		 pd_job_impl_start_sending	(PdJobImpl *job);

G_END_DECLS

#endif /* __PD_JOB_IMPL_H__ */
