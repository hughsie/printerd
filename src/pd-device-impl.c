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
#include <glib/gi18n.h>

#include "pd-common.h"
#include "pd-device-impl.h"
#include "pd-printer-impl.h"
#include "pd-daemon.h"
#include "pd-engine.h"

/**
 * SECTION:pddevice
 * @title: PdDeviceImpl
 * @short_description: Implementation of #PdDeviceImpl
 *
 * This type provides an implementation of the #PdDeviceImpl
 * interface on .
 */

typedef struct _PdDeviceImplClass	PdDeviceImplClass;

/**
 * PdDeviceImpl:
 *
 * The #PdDeviceImpl structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _PdDeviceImpl
{
	PdDeviceSkeleton	 parent_instance;
	PdEngine		*engine;
	gchar			*sysfs_path;
	gchar			*id;
};

struct _PdDeviceImplClass
{
	PdDeviceSkeletonClass parent_class;
};

enum
{
	PROP_0,
	PROP_SYSFS_PATH
};

static void pd_device_iface_init (PdDeviceIface *iface);

G_DEFINE_TYPE_WITH_CODE (PdDeviceImpl, pd_device_impl, PD_TYPE_DEVICE_SKELETON,
			 G_IMPLEMENT_INTERFACE (PD_TYPE_DEVICE, pd_device_iface_init));

/* ------------------------------------------------------------------ */

static void
pd_device_impl_finalize (GObject *object)
{
	PdDeviceImpl *device = PD_DEVICE_IMPL (object);
	/* note: we don't hold a reference to device->engine */
	g_free (device->sysfs_path);
	g_free (device->id);
	G_OBJECT_CLASS (pd_device_impl_parent_class)->finalize (object);
}

static void
pd_device_impl_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	PdDeviceImpl *device = PD_DEVICE_IMPL (object);

	switch (prop_id) {
	case PROP_SYSFS_PATH:
		g_value_set_string (value, device->sysfs_path);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_device_impl_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	PdDeviceImpl *device = PD_DEVICE_IMPL (object);

	switch (prop_id) {
	case PROP_SYSFS_PATH:
		g_assert (device->sysfs_path == NULL);
		/* we don't take a reference to the sysfs_path */
		device->sysfs_path = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_device_impl_init (PdDeviceImpl *device)
{
	g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (device),
					     G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
pd_device_impl_class_init (PdDeviceImplClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = pd_device_impl_finalize;
	gobject_class->set_property = pd_device_impl_set_property;
	gobject_class->get_property = pd_device_impl_get_property;

	/**
	 * PdDeviceImpl:sysfs_path:
	 *
	 * The sysfs path for the object.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_SYSFS_PATH,
					 g_param_spec_string ("sysfs-path",
							      "Serial",
							      "The sysfs path for the object",
							      NULL,
							      G_PARAM_READWRITE));
}

void
pd_device_impl_set_engine (PdDeviceImpl *device,
			   PdEngine *engine)
{
	device->engine = engine;
}

const gchar *
pd_device_impl_get_id (PdDeviceImpl *device)
{
	GHashTable *ieee1284_id_fields = NULL;
	const gchar *idstring;
	GString *id;
	gchar *mfg;
	gchar *mdl;
	gchar *sn;

	/* shortcut */
	if (device->id != NULL)
		goto out;

	/* make a unique ID for this device */
	idstring = pd_device_get_ieee1284_id (PD_DEVICE (device));
	ieee1284_id_fields = pd_parse_ieee1284_id (idstring);
	if (ieee1284_id_fields == NULL)
		goto out;

	mfg = g_hash_table_lookup (ieee1284_id_fields, "mfg");
	mdl = g_hash_table_lookup (ieee1284_id_fields, "mdl");
	sn = g_hash_table_lookup (ieee1284_id_fields, "sn");
	id = g_string_new ("");
	if (mfg)
		g_string_append_printf (id, "%s_", mfg);
	if (mdl)
		g_string_append_printf (id, "%s_", mdl);
	if (sn)
		g_string_append_printf (id, "%s_", sn);
	if (id->len != 0)
		g_string_set_size (id, id->len - 1);
	else
		g_string_append_printf (id, "unknown-device");

	device->id = g_string_free (id, FALSE);

	/* ensure valid */
	g_strcanon (device->id,
		    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		    "abcdefghijklmnopqrstuvwxyz"
		    "1234567890_",
		    '_');
out:
	if (ieee1284_id_fields)
		g_hash_table_unref (ieee1284_id_fields);

	return device->id;
}

/* ------------------------------------------------------------------ */

static void
pd_device_impl_complete_create_printer (PdDevice *_device,
					GDBusMethodInvocation *invocation,
					GVariant *options,
					const gchar *name,
					const gchar *description,
					const gchar *location,
					GVariant *defaults)
{
	PdDeviceImpl *device = PD_DEVICE_IMPL (_device);
	PdPrinter *printer;
	GString *path;
	const gchar *device_uris[2];
	const gchar *ieee1284_id = pd_device_get_ieee1284_id (_device);

	g_debug ("Creating printer from device %s", device->id);

	printer = pd_engine_add_printer (device->engine,
					 name, description, location,
					 ieee1284_id);

	/* set device uri */
	device_uris[0] = pd_device_get_uri (_device);
	device_uris[1] = NULL;
	pd_printer_set_device_uris (printer, device_uris);

	/* set job template attributes */
	pd_printer_impl_update_defaults (PD_PRINTER_IMPL (printer), defaults);

	/* return object path */
	path = g_string_new ("");
	g_string_printf (path, "/org/freedesktop/printerd/printer/%s",
			 pd_printer_impl_get_id (PD_PRINTER_IMPL (printer)));
	g_dbus_method_invocation_return_value (invocation,
					       g_variant_new ("(o)",
							      path->str));

	/* clean up */
	g_string_free (path, TRUE);
}

/* runs in thread dedicated to handling @invocation */
static gboolean
pd_device_impl_create_printer (PdDevice *_device,
			       GDBusMethodInvocation *invocation,
			       GVariant *options,
			       const gchar *name,
			       const gchar *description,
			       const gchar *location,
			       GVariant *defaults)
{
	PdDeviceImpl *device = PD_DEVICE_IMPL (_device);
	PdDaemon *daemon = PD_DAEMON (pd_engine_get_daemon (device->engine));

	/* Check if the user is authorized to create a printer */
	if (!pd_daemon_check_authorization_sync (daemon,
						 "org.freedesktop.printerd.printer-add",
						 options,
						 N_("Authentication is required to add a printer"),
						 invocation))
		goto out;

	pd_device_impl_complete_create_printer (_device,
						invocation,
						options,
						name,
						description,
						location,
						defaults);

 out:
	return TRUE; /* handled the method invocation */
}

static void
pd_device_iface_init (PdDeviceIface *iface)
{
	iface->handle_create_printer = pd_device_impl_create_printer;
}
