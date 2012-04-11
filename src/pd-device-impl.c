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

#include "pd-common.h"
#include "pd-device-impl.h"

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
	g_hash_table_unref (ieee1284_id_fields);
	return device->id;
}

/* ------------------------------------------------------------------ */

/* runs in thread dedicated to handling @invocation */
static gboolean
pd_device_impl_test_print (PdDevice *_device,
			   GDBusMethodInvocation *invocation,
			   GVariant *options)
{
	g_dbus_method_invocation_return_error (invocation,
					       PD_ERROR,
					       PD_ERROR_FAILED,
					       "Error doing test print");
	return TRUE; /* handled the method invocation */
}

static void
pd_device_iface_init (PdDeviceIface *iface)
{
	//iface->handle_test_print = pd_device_impl_test_print;
}
