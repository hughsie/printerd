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

#include "config.h"
#include <glib/gi18n-lib.h>

#include "pd-daemon.h"

/**
 * SECTION:printerddaemon
 * @title: PdDaemon
 * @short_description: Main daemon object
 *
 * Object holding all global state.
 */

typedef struct _PdDaemonClass	PdDaemonClass;

/**
 * PdDaemon:
 *
 * The #PdDaemon structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _PdDaemon
{
	GObject parent_instance;
	GDBusConnection *connection;
	GDBusObjectManagerServer *object_manager;
	PolkitAuthority *authority;
};

struct _PdDaemonClass
{
	GObjectClass parent_class;
};

enum
{
	PROP_0,
	PROP_CONNECTION,
	PROP_OBJECT_MANAGER,
};

G_DEFINE_TYPE (PdDaemon, pd_daemon, G_TYPE_OBJECT);

static void
pd_daemon_finalize (GObject *object)
{
	PdDaemon *daemon = PD_DAEMON (object);

	g_object_unref (daemon->authority);
	g_object_unref (daemon->object_manager);
	g_object_unref (daemon->connection);

	if (G_OBJECT_CLASS (pd_daemon_parent_class)->finalize != NULL)
		G_OBJECT_CLASS (pd_daemon_parent_class)->finalize (object);
}

static void
pd_daemon_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	PdDaemon *daemon = PD_DAEMON (object);
	switch (prop_id) {
	case PROP_CONNECTION:
		g_value_set_object (value, pd_daemon_get_connection (daemon));
		break;
	case PROP_OBJECT_MANAGER:
		g_value_set_object (value, pd_daemon_get_object_manager (daemon));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_daemon_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	PdDaemon *daemon = PD_DAEMON (object);
	switch (prop_id) {
	case PROP_CONNECTION:
		g_assert (daemon->connection == NULL);
		daemon->connection = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_daemon_init (PdDaemon *daemon)
{
}

static void
pd_daemon_constructed (GObject *object)
{
	PdDaemon *daemon = PD_DAEMON (object);
	GError *error = NULL;

	daemon->authority = polkit_authority_get_sync (NULL, &error);
	if (daemon->authority == NULL) {
		g_error ("Error initializing PolicyKit authority: %s (%s, %d)",
			 error->message, g_quark_to_string (error->domain), error->code);
		g_error_free (error);
	}
	daemon->object_manager = g_dbus_object_manager_server_new ("/org/freedesktop/printerd");

	/* Export the ObjectManager */
	g_dbus_object_manager_server_set_connection (daemon->object_manager,
						     daemon->connection);

	if (G_OBJECT_CLASS (pd_daemon_parent_class)->constructed != NULL)
		G_OBJECT_CLASS (pd_daemon_parent_class)->constructed (object);
}


static void
pd_daemon_class_init (PdDaemonClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize     = pd_daemon_finalize;
	gobject_class->constructed  = pd_daemon_constructed;
	gobject_class->set_property = pd_daemon_set_property;
	gobject_class->get_property = pd_daemon_get_property;

	/**
	* PdDaemon:connection:
	*
	* The #GDBusConnection the daemon is for.
	*/
	g_object_class_install_property (gobject_class,
					 PROP_CONNECTION,
					 g_param_spec_object ("connection",
							      "Connection",
							      "The D-Bus connection the daemon is for",
							      G_TYPE_DBUS_CONNECTION,
							      G_PARAM_READABLE |
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_STATIC_STRINGS));

	/**
	* PdDaemon:object-manager:
	*
	* The #GDBusObjectManager used by the daemon
	*/
	g_object_class_install_property (gobject_class,
					 PROP_OBJECT_MANAGER,
					 g_param_spec_object ("object-manager",
							      "Object Manager",
							      "The D-Bus Object Manager server used by the daemon",
							      G_TYPE_DBUS_OBJECT_MANAGER_SERVER,
							      G_PARAM_READABLE |
							      G_PARAM_STATIC_STRINGS));
}

/**
 * pd_daemon_new:
 * @connection: A #GDBusConnection.
 *
 * Create a new daemon object for exporting objects on @connection.
 *
 * Returns: A #PdDaemon object. Free with g_object_unref().
 */
PdDaemon *
pd_daemon_new (GDBusConnection *connection)
{
	g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
	return PD_DAEMON (g_object_new (PD_TYPE_DAEMON,
					"connection", connection,
					NULL));
}

/**
 * pd_daemon_get_connection:
 * @daemon: A #PdDaemon.
 *
 * Gets the D-Bus connection used by @daemon.
 *
 * Returns: A #GDBusConnection. Do not free, the object is owned by @daemon.
 */
GDBusConnection *
pd_daemon_get_connection (PdDaemon *daemon)
{
	g_return_val_if_fail (PD_IS_DAEMON (daemon), NULL);
	return daemon->connection;
}

/**
 * pd_daemon_get_object_manager:
 * @daemon: A #PdDaemon.
 *
 * Gets the D-Bus object manager used by @daemon.
 *
 * Returns: A #GDBusObjectManagerServer. Do not free, the object is owned by @daemon.
 */
GDBusObjectManagerServer *
pd_daemon_get_object_manager (PdDaemon *daemon)
{
	g_return_val_if_fail (PD_IS_DAEMON (daemon), NULL);
	return daemon->object_manager;
}

/**
 * pd_daemon_get_authority:
 * @daemon: A #PdDaemon.
 *
 * Gets the PolicyKit authority used by @daemon.
 *
 * Returns: A #PolkitAuthority instance. Do not free, the object is owned by @daemon.
 */
PolkitAuthority *
pd_daemon_get_authority (PdDaemon *daemon)
{
	g_return_val_if_fail (PD_IS_DAEMON (daemon), NULL);
	return daemon->authority;
}

/**
 * pd_daemon_find_object:
 * @daemon: A #PdDaemon.
 * @object_path: An object path
 *
 * Finds an exported object with the object path given by @object_path.
 *
 * Returns: (transfer full): A #PdObject or %NULL if not found. Free with g_object_unref().
 */
PdObject *
pd_daemon_find_object (PdDaemon *daemon,
		       const gchar *object_path)
{
	return (PdObject *) g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (daemon->object_manager),
							      object_path);
}
