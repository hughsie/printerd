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

#ifndef __PD_CLIENT_H__
#define __PD_CLIENT_H__

#include <printerd/pd-types.h>
#include <printerd/pd-generated.h>

G_BEGIN_DECLS

#define PD_TYPE_CLIENT  (pd_client_get_type ())
#define PD_CLIENT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), PD_TYPE_CLIENT, PdClient))
#define PD_IS_CLIENT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), PD_TYPE_CLIENT))

GType               pd_client_get_type           (void) G_GNUC_CONST;
void                pd_client_new                (GCancellable        *cancellable,
                                                      GAsyncReadyCallback  callback,
                                                      gpointer             user_data);
PdClient       *pd_client_new_finish         (GAsyncResult        *res,
                                                      GError             **error);
PdClient       *pd_client_new_sync           (GCancellable        *cancellable,
                                                      GError             **error);
GDBusObjectManager *pd_client_get_object_manager (PdClient        *client);
PdManager      *pd_client_get_manager        (PdClient        *client);
void                pd_client_settle             (PdClient        *client);

PdObject       *pd_client_get_object          (PdClient        *client,
                                                       const gchar         *object_path);
PdObject       *pd_client_peek_object         (PdClient        *client,
                                                       const gchar         *object_path);

G_END_DECLS

#endif /* __PD_CLIENT_H__ */
