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

#ifndef __PD_DAEMON_H__
#define __PD_DAEMON_H__

#include "pd-daemontypes.h"

G_BEGIN_DECLS

#define PD_TYPE_DAEMON		(pd_daemon_get_type ())
#define PD_DAEMON(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PD_TYPE_DAEMON, PdDaemon))
#define PD_IS_DAEMON(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PD_TYPE_DAEMON))

GType				 pd_daemon_get_type		(void) G_GNUC_CONST;
PdDaemon			*pd_daemon_new			(GDBusConnection *connection,
								 gboolean is_session);
GDBusConnection			*pd_daemon_get_connection	(PdDaemon	*daemon);
GDBusObjectManagerServer	*pd_daemon_get_object_manager	(PdDaemon	*daemon);
PolkitAuthority			*pd_daemon_get_authority	(PdDaemon	*daemon);
PdObject			*pd_daemon_find_object		(PdDaemon	*daemon,
								 const gchar	*object_path);
PdEngine			*pd_daemon_get_engine		(PdDaemon	*daemon);
gboolean		 pd_daemon_check_authorization_sync	(PdDaemon	*daemon,
								 GVariant	*options,
								 const gchar	*description,
								 GDBusMethodInvocation *invocation,
								 const gchar	*action_id,
								 ...);

G_END_DECLS

#endif /* __PD_DAEMON_H__ */
