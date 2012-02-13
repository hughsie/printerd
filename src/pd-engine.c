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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA	02110-1301	USA
 *
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <gudev/gudev.h>

#include "pd-daemon.h"
#include "pd-engine.h"

/**
 * SECTION:printerdengine
 * @title: PdEngine
 * @short_description: Abstract base class for all data engines
 *
 * Abstract base class for all data engines.
 */

struct _PdEnginePrivate
{
	PdDaemon *daemon;
	GFileMonitor *monitor;
	GUdevClient *gudev_client;
};

enum
{
	PROP_0,
	PROP_DAEMON
};

G_DEFINE_TYPE (PdEngine, pd_engine, G_TYPE_OBJECT);

static void
pd_engine_finalize (GObject *object)
{
	PdEngine *engine = PD_ENGINE (object);

	/* note: we don't hold a ref to engine->priv->daemon */
	g_object_unref (engine->priv->gudev_client);

	if (G_OBJECT_CLASS (pd_engine_parent_class)->finalize != NULL)
		G_OBJECT_CLASS (pd_engine_parent_class)->finalize (object);
}

static void
pd_engine_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	PdEngine *engine = PD_ENGINE (object);

	switch (prop_id) {
	case PROP_DAEMON:
		g_value_set_object (value, pd_engine_get_daemon (engine));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_engine_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	PdEngine *engine = PD_ENGINE (object);

	switch (prop_id) {
	case PROP_DAEMON:
		g_assert (engine->priv->daemon == NULL);
		/* we don't take a reference to the daemon */
		engine->priv->daemon = g_value_get_object (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_engine_handle_uevent (PdEngine *engine,
			 const gchar *action,
			 GUdevDevice *device)
{
	g_debug ("uevent %s %s",
		action,
		g_udev_device_get_sysfs_path (device));
}

static void
on_uevent (GUdevClient *client,
	   const gchar *action,
	   GUdevDevice *device,
	   gpointer user_data)
{
	PdEngine *engine = PD_ENGINE (user_data);
	pd_engine_handle_uevent (engine, action, device);
}

static void
pd_engine_init (PdEngine *engine)
{
	const gchar *subsystems[] = {"usb", NULL};

	engine->priv = G_TYPE_INSTANCE_GET_PRIVATE (engine, PD_TYPE_ENGINE, PdEnginePrivate);

	/* get ourselves an udev client */
	engine->priv->gudev_client = g_udev_client_new (subsystems);
	g_signal_connect (engine->priv->gudev_client,
			  "uevent",
			  G_CALLBACK (on_uevent),
			  engine);
}

static void
pd_engine_class_init (PdEngineClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = pd_engine_finalize;
	gobject_class->set_property = pd_engine_set_property;
	gobject_class->get_property = pd_engine_get_property;

	/**
	 * PdEngine:daemon:
	 *
	 * The #PdDaemon the engine is for.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_DAEMON,
					 g_param_spec_object ("daemon",
							      "Daemon",
							      "The daemon the engine is for",
							      PD_TYPE_DAEMON,
							      G_PARAM_READABLE |
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_STATIC_STRINGS));

	g_type_class_add_private (klass, sizeof (PdEnginePrivate));
}

/**
 * pd_engine_get_daemon:
 * @engine: A #PdEngine.
 *
 * Gets the daemon used by @engine.
 *
 * Returns: A #PdDaemon. Do not free, the object is owned by @engine.
 */
PdDaemon *
pd_engine_get_daemon (PdEngine *engine)
{
	g_return_val_if_fail (PD_IS_ENGINE (engine), NULL);
	return engine->priv->daemon;
}

/**
 * pd_engine_start:
 * @engine: A #PdEngine.
 *
 * Starts the engine.
 */
void
pd_engine_start	(PdEngine *engine)
{
	g_return_if_fail (PD_IS_ENGINE (engine));
	/* do stuff */
}

/**
 * pd_engine_new:
 * @daemon: A #PdDaemon.
 *
 * Create a new engine object for -specific objects / functionality.
 *
 * Returns: A #PdEngine object. Free with g_object_unref().
 */
PdEngine *
pd_engine_new (PdDaemon *daemon)
{
	g_return_val_if_fail (PD_IS_DAEMON (daemon), NULL);
	return PD_ENGINE (g_object_new (PD_TYPE_ENGINE,
					"daemon", daemon,
					NULL));
}
