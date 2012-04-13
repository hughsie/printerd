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

#include "pd-printer-impl.h"

/**
 * SECTION:pdprinter
 * @title: PdPrinterImpl
 * @short_description: Implementation of #PdPrinterImpl
 *
 * This type provides an implementation of the #PdPrinterImpl
 * interface on .
 */

typedef struct _PdPrinterImplClass	PdPrinterImplClass;

/**
 * PdPrinterImpl:
 *
 * The #PdPrinterImpl structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _PdPrinterImpl
{
	PdPrinterSkeleton	 parent_instance;
	gchar			*name;
	gchar			*description;
	gchar			*location;
	gchar			*ieee1284_id;
	GHashTable		*defaults;

	gchar			*id;
};

struct _PdPrinterImplClass
{
	PdPrinterSkeletonClass parent_class;
};

enum
{
	PROP_0,
	PROP_NAME,
	PROP_DESCRIPTION,
	PROP_LOCATION,
	PROP_IEEE1284_ID,
	PROP_DEFAULTS,
};

static void pd_printer_iface_init (PdPrinterIface *iface);

G_DEFINE_TYPE_WITH_CODE (PdPrinterImpl, pd_printer_impl, PD_TYPE_PRINTER_SKELETON,
			 G_IMPLEMENT_INTERFACE (PD_TYPE_PRINTER, pd_printer_iface_init));

/* ------------------------------------------------------------------ */

static void
pd_printer_impl_finalize (GObject *object)
{
	PdPrinterImpl *printer = PD_PRINTER_IMPL (object);
	g_free (printer->name);
	g_free (printer->description);
	g_free (printer->location);
	g_free (printer->ieee1284_id);
	g_hash_table_unref (printer->defaults);
	G_OBJECT_CLASS (pd_printer_impl_parent_class)->finalize (object);
}

static void
pd_printer_impl_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	PdPrinterImpl *printer = PD_PRINTER_IMPL (object);
	GVariantBuilder builder;
	GHashTableIter iter;
	gchar *dkey;
	GVariant *dvalue;

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, printer->name);
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, printer->description);
		break;
	case PROP_LOCATION:
		g_value_set_string (value, printer->location);
		break;
	case PROP_IEEE1284_ID:
		g_value_set_string (value, printer->ieee1284_id);
		break;
	case PROP_DEFAULTS:
		g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
		g_hash_table_iter_init (&iter, printer->defaults);
		while (g_hash_table_iter_next (&iter,
					       (gpointer *) &dkey,
					       (gpointer *) &dvalue))
			g_variant_builder_add (&builder, "{sv}",
					       g_strdup (dkey), dvalue);

		g_value_set_variant (value, g_variant_builder_end (&builder));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_printer_impl_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	PdPrinterImpl *printer = PD_PRINTER_IMPL (object);
	GVariantIter iter;
	gchar *dkey;
	GVariant *dvalue;

	switch (prop_id) {
	case PROP_NAME:
		g_free (printer->name);
		printer->name = g_value_dup_string (value);
		break;
	case PROP_DESCRIPTION:
		g_free (printer->description);
		printer->description = g_value_dup_string (value);
		break;
	case PROP_LOCATION:
		g_free (printer->location);
		printer->location = g_value_dup_string (value);
		break;
	case PROP_IEEE1284_ID:
		g_free (printer->ieee1284_id);
		printer->ieee1284_id = g_value_dup_string (value);
		break;
	case PROP_DEFAULTS:
		g_hash_table_remove_all (printer->defaults);
		g_variant_iter_init (&iter, g_value_get_variant (value));
		while (g_variant_iter_next (&iter, "{sv}", &dkey, &dvalue))
			g_hash_table_insert (printer->defaults, dkey, dvalue);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_printer_impl_init (PdPrinterImpl *printer)
{
	g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (printer),
					     G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
pd_printer_impl_constructed (GObject *object)
{
	PdPrinterImpl *printer = PD_PRINTER_IMPL (object);
	GVariantBuilder *builder, *builder_sub;

	printer->defaults = g_hash_table_new_full (g_str_hash,
						   g_str_equal,
						   g_free,
						   (GDestroyNotify) g_variant_unref);

	/* set initial job template attributes */
	g_hash_table_insert (printer->defaults,
			     g_strdup ("media"),
			     g_variant_ref_sink (g_variant_new ("s",
								"iso-a4")));

	/* set initial job template supported attributes */
	builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));

	builder_sub = g_variant_builder_new (G_VARIANT_TYPE ("as"));
	g_variant_builder_add (builder_sub, "s", "iso-a4");
	g_variant_builder_add (builder_sub, "s", "na-letter");
	g_variant_builder_add (builder,
			       "{sv}",
			       "media",
			       g_variant_new ("as", builder_sub));
	g_variant_builder_unref (builder_sub);

	pd_printer_set_supported (PD_PRINTER (printer),
				  g_variant_builder_end (builder));
	g_variant_builder_unref (builder);
}

static void
pd_printer_impl_class_init (PdPrinterImplClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->constructed = pd_printer_impl_constructed;
	gobject_class->finalize = pd_printer_impl_finalize;
	gobject_class->set_property = pd_printer_impl_set_property;
	gobject_class->get_property = pd_printer_impl_get_property;

	/**
	 * PdPrinterImpl:name:
	 *
	 * The name for the queue.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "Name",
							      "The name for the queue",
							      NULL,
							      G_PARAM_READWRITE));

	/**
	 * PdPrinterImpl:description:
	 *
	 * The name for the queue.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_DESCRIPTION,
					 g_param_spec_string ("description",
							      "Description",
							      "The description for the queue",
							      NULL,
							      G_PARAM_READWRITE));

	/**
	 * PdPrinterImpl:location:
	 *
	 * The location of the queue.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_LOCATION,
					 g_param_spec_string ("location",
							      "Location",
							      "The location of the queue",
							      NULL,
							      G_PARAM_READWRITE));

	/**
	 * PdPrinterImpl:ieee1284-id:
	 *
	 * The IEEE 1284 Device ID used by the queue.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_IEEE1284_ID,
					 g_param_spec_string ("ieee1284-id",
							      "IEEE1284 ID",
							      "The IEEE 1284 Device ID used by the queue",
							      NULL,
							      G_PARAM_READWRITE));

	/**
	 * PdPrinterImpl:defaults:
	 *
	 * The IEEE 1284 Device ID used by the queue.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_DEFAULTS,
					 g_param_spec_variant ("defaults",
							       "Defaults",
							       "The job template attributes",
							       G_VARIANT_TYPE ("a{sv}"),
							       NULL,
							       G_PARAM_READWRITE));
}

const gchar *
pd_printer_impl_get_id (PdPrinterImpl *printer)
{
	/* shortcut */
	if (printer->id != NULL)
		goto out;

	printer->id = g_strdup (printer->name);

	/* ensure valid */
	g_strcanon (printer->id,
		    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		    "abcdefghijklmnopqrstuvwxyz"
		    "1234567890_",
		    '_');

 out:
	return printer->id;
}

void
pd_printer_impl_set_id (PdPrinterImpl *printer,
			const gchar *id)
{
	if (printer->id)
		g_free (printer->id);

	printer->id = g_strdup (id);
}

void
pd_printer_impl_update_defaults (PdPrinterImpl *printer,
				 GVariant *defaults)
{
	GVariantIter iter;
	gchar *key;
	GVariant *value;

	/* add/overwrite default values, keeping other existing values */
	g_variant_iter_init (&iter, defaults);
	while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
		gchar *val = g_variant_print (value, TRUE);
		g_debug ("Defaults: set %s=%s", key, val);
		g_free (val);
		g_hash_table_replace (printer->defaults,
				      g_strdup (key),
				      g_variant_ref (value));
}
}

/* ------------------------------------------------------------------ */

static void
pd_printer_impl_complete_set_device_uris (PdPrinter *_printer,
					  GDBusMethodInvocation *invocation,
					  const gchar *const *device_uris)
{
	pd_printer_set_device_uris (_printer, device_uris);
	g_dbus_method_invocation_return_value (invocation, NULL);
}

/* runs in thread dedicated to handling @invocation */
static gboolean
pd_printer_impl_set_device_uris (PdPrinter *_printer,
				 GDBusMethodInvocation *invocation,
				 const gchar *const *device_uris)
{
	/* Check if the user is authorized to create a printer */
	//if (!pd_daemon_util_check_authorization_sync ())
	//	goto out;

	pd_printer_impl_complete_set_device_uris (_printer,
						  invocation,
						  device_uris);

	//out:
	return TRUE; /* handled the method invocation */
}

static void
pd_printer_iface_init (PdPrinterIface *iface)
{
	iface->handle_set_device_uris = pd_printer_impl_set_device_uris;
}
