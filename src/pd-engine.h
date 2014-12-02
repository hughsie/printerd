/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014 Tim Waugh <twaugh@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA	02110-1301	USA
 *
 */

#ifndef __PD_ENGINE_H__
#define __PD_ENGINE_H__

#include "pd-daemontypes.h"

G_BEGIN_DECLS

#define PD_TYPE_ENGINE		(pd_engine_get_type ())
#define PD_ENGINE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PD_TYPE_ENGINE, PdEngine))
#define PD_ENGINE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PD_TYPE_ENGINE, PdEngineClass))
#define PD_ENGINE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PD_TYPE_ENGINE, PdEngineClass))
#define PD_IS_ENGINE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PD_TYPE_ENGINE))
#define PD_IS_ENGINE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PD_TYPE_ENGINE))

typedef struct _PdEngineClass	PdEngineClass;
typedef struct _PdEnginePrivate	PdEnginePrivate;

/**
 * PdEngine:
 *
 * The #PdEngine structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _PdEngine
{
	/*< private >*/
	GObject parent_instance;
	PdEnginePrivate *priv;
};

/**
 * PdEngineClass:
 * @parent_class: The parent class.
 * @start: Virtual function for pd_engine_start(). The default implementation does nothing.
 *
 * Class structure for #PdEngine.
 */
struct _PdEngineClass
{
	GObjectClass parent_class;
	/*< private >*/
	gpointer padding[8];
};


GType		 pd_engine_get_type		(void) G_GNUC_CONST;
PdEngine	*pd_engine_new			(PdDaemon	*daemon);
PdDaemon	*pd_engine_get_daemon		(PdEngine	*engine);
GUdevClient	*pd_engine_get_udev_client	(PdEngine	*engine);
GList		*pd_engine_dup_printer_ids	(PdEngine	*engine);
GList		*pd_engine_get_devices		(PdEngine	*engine);
void		 pd_engine_start		(PdEngine	*engine);
PdPrinter	*pd_engine_add_printer		(PdEngine	*engine,
						 GVariant	*options,
						 const gchar	*name,
						 const gchar	*description,
						 const gchar	*location,
						 const gchar	*ieee1284_id,
						 GError **error);
gboolean	 pd_engine_remove_printer	(PdEngine	*engine,
						 const gchar	*printer_path);
PdPrinter	*pd_engine_get_printer_by_path	(PdEngine	*engine,
						 const gchar	*printer_path);
PdJob		*pd_engine_add_job		(PdEngine	*engine,
						 const gchar	*printer_path,
						 const gchar	*name,
						 GVariant	*attributes);
gboolean	 pd_engine_remove_job		(PdEngine	*engine,
						 const gchar	*job_path);
G_END_DECLS

#endif /* __PD_ENGINE_H__ */
