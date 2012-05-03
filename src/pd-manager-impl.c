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

#include <glib.h>

#include "pd-manager-impl.h"
#include "pd-daemon.h"
#include "pd-engine.h"
#include "pd-device-impl.h"

/**
 * SECTION:pdmanager
 * @title: PdManagerImpl
 * @short_description: Implementation of #PdManagerImpl
 *
 * This type provides an implementation of the #PdManagerImpl
 * interface on .
 */

typedef struct _PdManagerImplClass	PdManagerImplClass;

/**
 * PdManagerImpl:
 *
 * The #PdManagerImpl structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _PdManagerImpl
{
	PdManagerSkeleton parent_instance;
	PdDaemon *daemon;
};

struct _PdManagerImplClass
{
	PdManagerSkeletonClass parent_class;
};

enum
{
	PROP_0,
	PROP_DAEMON
};

static void pd_manager_iface_init (PdManagerIface *iface);

G_DEFINE_TYPE_WITH_CODE (PdManagerImpl, pd_manager_impl, PD_TYPE_MANAGER_SKELETON,
			 G_IMPLEMENT_INTERFACE (PD_TYPE_MANAGER, pd_manager_iface_init));

/* ------------------------------------------------------------------ */

static void
pd_manager_impl_finalize (GObject *object)
{
	/* PdManagerImpl *manager = PD_MANAGER_IMPL (object); */
	G_OBJECT_CLASS (pd_manager_impl_parent_class)->finalize (object);
}

static void
pd_manager_impl_get_property (GObject *object,
			 guint prop_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	PdManagerImpl *manager = PD_MANAGER_IMPL (object);

	switch (prop_id) {
	case PROP_DAEMON:
		g_value_set_object (value, pd_manager_impl_get_daemon (manager));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_manager_impl_set_property (GObject *object,
			 guint prop_id,
			 const GValue *value,
			 GParamSpec *pspec)
{
	PdManagerImpl *manager = PD_MANAGER_IMPL (object);

	switch (prop_id) {
	case PROP_DAEMON:
		g_assert (manager->daemon == NULL);
		/* we don't take a reference to the daemon */
		manager->daemon = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_manager_impl_init (PdManagerImpl *manager)
{
	g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
					     G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
pd_manager_impl_class_init (PdManagerImplClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = pd_manager_impl_finalize;
	gobject_class->set_property = pd_manager_impl_set_property;
	gobject_class->get_property = pd_manager_impl_get_property;

	/**
	 * PdManagerImpl:daemon:
	 *
	 * The #PdDaemon for the object.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_DAEMON,
					 g_param_spec_object ("daemon",
							      "Daemon",
							      "The daemon for the object",
							      PD_TYPE_DAEMON,
							      G_PARAM_READABLE |
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_STATIC_STRINGS));
}

/**
 * pd_manager_impl_new:
 * @daemon: A #PdDaemon.
 *
 * Creates a new #PdManagerImpl instance.
 *
 * Returns: A new #PdManagerImpl. Free with g_object_unref().
 */
PdManager *
pd_manager_impl_new (PdDaemon *daemon)
{
	g_return_val_if_fail (PD_IS_DAEMON (daemon), NULL);
	return PD_MANAGER (g_object_new (PD_TYPE_MANAGER_IMPL,
					 "daemon", daemon,
					 "version", PACKAGE_VERSION,
					 NULL));
}

/**
 * pd_manager_impl_get_daemon:
 * @manager: A #PdManagerImpl.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #PdDaemon. Do not free, the object is owned by @manager.
 */
PdDaemon *
pd_manager_impl_get_daemon (PdManagerImpl *manager)
{
	g_return_val_if_fail (PD_IS_MANAGER_IMPL (manager), NULL);
	return manager->daemon;
}

/* ------------------------------------------------------------------ */

/* runs in thread dedicated to handling @invocation */
static gboolean
pd_manager_impl_get_printers (PdManager *_manager,
			      GDBusMethodInvocation *invocation)
{
	PdManagerImpl *manager = PD_MANAGER_IMPL (_manager);
	PdDaemon *daemon = pd_manager_impl_get_daemon (manager);
	PdEngine *engine = pd_daemon_get_engine (daemon);
	GList *printer_ids = pd_engine_get_printer_ids (engine);
	GList *each;
	GVariantBuilder builder;
	GString *path = g_string_new ("");

	g_debug ("Handling GetPrinters");
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("(ao)"));
	g_variant_builder_open (&builder, G_VARIANT_TYPE ("ao"));
	for (each = printer_ids; each; each = g_list_next (each)) {
		g_string_printf (path, "/org/freedesktop/printerd/printer/%s",
				 (const gchar *) each->data);
		g_variant_builder_add (&builder, "o", path->str);
	}
	g_variant_builder_close (&builder);
	g_dbus_method_invocation_return_value (invocation,
					       g_variant_builder_end (&builder));
	g_string_free (path, TRUE);
	return TRUE; /* handled the method invocation */
}

/* runs in thread dedicated to handling @invocation */
static gboolean
pd_manager_impl_get_devices (PdManager *_manager,
			     GDBusMethodInvocation *invocation)
{
	PdManagerImpl *manager = PD_MANAGER_IMPL (_manager);
	PdDaemon *daemon = pd_manager_impl_get_daemon (manager);
	PdEngine *engine = pd_daemon_get_engine (daemon);
	GList *devices = pd_engine_get_devices (engine);
	GList *each;
	GVariantBuilder builder;
	GString *path = g_string_new ("");

	g_debug ("Handling GetDevices");
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("(ao)"));
	g_variant_builder_open (&builder, G_VARIANT_TYPE ("ao"));
	for (each = devices; each; each = g_list_next (each)) {
		const gchar *id;
		id = pd_device_impl_get_id (PD_DEVICE_IMPL (each->data));
		g_string_printf (path, "/org/freedesktop/printerd/device/%s", id);
		g_variant_builder_add (&builder, "o", path->str);
	}
	g_variant_builder_close (&builder);
	g_dbus_method_invocation_return_value (invocation,
					       g_variant_builder_end (&builder));

	g_list_foreach (devices, (GFunc) g_object_unref, NULL);
	g_list_free (devices);
	g_string_free (path, TRUE);
	return TRUE; /* handled the method invocation */
}

/* runs in thread dedicated to handling @invocation */
static gboolean
pd_manager_impl_add_printer (PdManager *_manager,
			GDBusMethodInvocation *invocation,
			GVariant *options)
{
//	PdManagerImpl *manager = PD_MANAGER_IMPL (_manager);
//	GError *error;

	/* Check if the user is authorized to create a loop device */
//	if (!pd_daemon_util_check_authorization_sync (manager->daemon,
//							NULL,
//							"org.freedesktop.printerd.printer-add",
//							options,
//							N_("Authentication is required to add a printer"),
//							invocation))
//		goto out;

//	g_variant_lookup (options, "read-only", "b", &option_read_only);
//	g_variant_lookup (options, "offset", "t", &option_offset);
//	g_variant_lookup (options, "size", "t", &option_size);

	if (TRUE) {
		g_dbus_method_invocation_return_error (invocation,
						 PD_ERROR,
						 PD_ERROR_FAILED,
						 "Error setting up loop device");
		goto out;
	}

//	pd_manager_impl_complete_add_printer (object,
//					invocation,
//					NULL, /* fd_list */
//					g_dbus_object_get_object_path (G_DBUS_OBJECT (loop_object)));
out:
	return TRUE; /* handled the method invocation */
}

static void
pd_manager_iface_init (PdManagerIface *iface)
{
	iface->handle_get_printers = pd_manager_impl_get_printers;
	iface->handle_get_devices = pd_manager_impl_get_devices;
}
