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
#include "pd-manager-impl.h"
#include "pd-printer-impl.h"

/**
 * SECTION:printerdengine
 * @title: PdEngine
 * @short_description: Abstract base class for all data engines
 *
 * Abstract base class for all data engines.
 */

struct _PdEnginePrivate
{
	gboolean	 coldplug;
	GFileMonitor	*monitor;
	GUdevClient	*gudev_client;
	PdDaemon	*daemon;
	PdObjectSkeleton *manager_object;
	GHashTable	*path_to_printer;
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
	g_hash_table_unref (engine->priv->path_to_printer);

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
pd_engine_printer_remove (PdEngine *engine,
			  PdPrinter *printer)
{
	g_debug ("remove printer");
}

/**
 * pd_ensure_dbus_path:
 **/
static gchar *
pd_ensure_dbus_path (gchar *object_path)
{
	gchar *object_path_tmp;
	object_path_tmp = g_strdup (object_path);
	g_strcanon (object_path_tmp,
		    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		    "abcdefghijklmnopqrstuvwxyz"
		    "1234567890_/",
		    '_');
	return object_path_tmp;
}

static PdPrinter *
pd_engine_printer_add (PdEngine *engine,
		       GUdevDevice *device)
{
	const gchar *id_model;
	const gchar *id_vendor;
	const gchar *ieee1284_id;
	const gchar *serial;
	gchar *object_path_tmp = NULL;
	GString *object_path = NULL;
	GUdevDevice *parent = NULL;
	PdDaemon *daemon;
	PdPrinter *printer = NULL;

	/* get the IEEE1284 ID from the interface device */
	ieee1284_id = g_udev_device_get_sysfs_attr (device, "ieee1284_id");
	if (ieee1284_id == NULL) {
		g_warning ("failed to get IEEE1284 from %s",
			   g_udev_device_get_sysfs_path (device));
		goto out;
	}

	/* get the device properties from the parent device */
	parent = g_udev_device_get_parent (device);
	id_vendor = g_udev_device_get_property (parent, "ID_VENDOR");
	id_model = g_udev_device_get_property (parent, "ID_MODEL");
	serial = g_udev_device_get_property (parent, "ID_SERIAL_SHORT");

	printer = PD_PRINTER (g_object_new (PD_TYPE_PRINTER_IMPL,
					    "serial", serial,
					    "model", id_model,
					    "vendor", id_vendor,
					    "ieee1284id", ieee1284_id,
					    "sysfs-path", g_udev_device_get_sysfs_path (device),
					    NULL));

	g_debug ("add printer %s:%s:%s [%s]",
		 id_vendor, id_model, serial, ieee1284_id);

	/* devise object path */
	object_path = g_string_new ("/org/freedesktop/printerd/printer/");
	if (id_vendor != NULL)
		g_string_append_printf (object_path, "%s_", id_vendor);
	if (id_model != NULL)
		g_string_append_printf (object_path, "%s_", id_model);
	if (serial != NULL)
		g_string_append_printf (object_path, "%s_", serial);
	g_string_set_size (object_path, object_path->len - 1);
	object_path_tmp = pd_ensure_dbus_path (object_path->str);

	/* export on bus */
	engine->priv->manager_object = pd_object_skeleton_new (object_path_tmp);
	daemon = pd_engine_get_daemon (PD_ENGINE (engine));
	pd_object_skeleton_set_printer (engine->priv->manager_object, printer);
	g_dbus_object_manager_server_export (pd_daemon_get_object_manager (daemon),
					     G_DBUS_OBJECT_SKELETON (engine->priv->manager_object));
out:
	g_free (object_path_tmp);
	if (object_path != NULL)
		g_string_free (object_path, TRUE);
	if (parent != NULL)
		g_object_unref (parent);
	return printer;
}

static void
pd_engine_handle_uevent (PdEngine *engine,
			 const gchar *action,
			 GUdevDevice *device)
{
	gint i_class;
	gint i_subclass;
	PdPrinter *printer;

	if (g_strcmp0 (action, "remove") == 0) {

		/* look for existing device */
		printer = g_hash_table_lookup (engine->priv->path_to_printer,
					       g_udev_device_get_sysfs_path (device));
		if (printer != NULL) {
			pd_engine_printer_remove (engine, printer);
			g_hash_table_remove (engine->priv->path_to_printer,
					     g_udev_device_get_sysfs_path (device));
		}
		return;
	}
	if (g_strcmp0 (action, "add") == 0) {

		/* not a printer */
		i_class = g_udev_device_get_sysfs_attr_as_int (device, "bInterfaceClass");
		i_subclass = g_udev_device_get_sysfs_attr_as_int (device, "bInterfaceSubClass");
		if (i_class != 0x07 ||
		    i_subclass != 0x01)
			return;

		/* add to hash and add to database */
		printer = pd_engine_printer_add (engine, device);
		g_hash_table_insert (engine->priv->path_to_printer,
				     g_strdup (g_udev_device_get_sysfs_path (device)),
				     (gpointer) printer);
		return;
	}
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
 * pd_engine_get_coldplug:
 * @engine: A #PdEngine.
 *
 * Gets whether @engine is in the coldplug phase.
 *
 * Returns: %TRUE if in the coldplug phase, %FALSE otherwise.
 **/
gboolean
pd_engine_get_coldplug (PdEngine *engine)
{
	g_return_val_if_fail (PD_IS_ENGINE (engine), FALSE);
	return engine->priv->coldplug;
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
	GList *devices;
	GList *l;
	PdDaemon *daemon;
	PdManager *manager;

	engine->priv->manager_object = pd_object_skeleton_new ("/org/freedesktop/printerd/Manager");

	/* create manager object */
	daemon = pd_engine_get_daemon (PD_ENGINE (engine));
	manager = pd_manager_impl_new (daemon);
	pd_object_skeleton_set_manager (engine->priv->manager_object, manager);
	g_dbus_object_manager_server_export (pd_daemon_get_object_manager (daemon),
					     G_DBUS_OBJECT_SKELETON (engine->priv->manager_object));

	/* keep a hash to detect removal */
	engine->priv->path_to_printer = g_hash_table_new_full (g_str_hash,
							       g_str_equal,
							       g_free,
							       g_object_unref);

	/* coldplug */
	engine->priv->coldplug = TRUE;
	devices = g_udev_client_query_by_subsystem (engine->priv->gudev_client, "usb");
	for (l = devices; l != NULL; l = l->next)
		pd_engine_handle_uevent (engine, "add", G_UDEV_DEVICE (l->data));
	engine->priv->coldplug = FALSE;

	g_object_unref (manager);
	g_list_foreach (devices, (GFunc) g_object_unref, NULL);
	g_list_free (devices);
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
