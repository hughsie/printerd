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
	gchar			*sysfs_path;
};

struct _PdPrinterImplClass
{
	PdPrinterSkeletonClass parent_class;
};

enum
{
	PROP_0,
	PROP_SYSFS_PATH
};

static void pd_printer_iface_init (PdPrinterIface *iface);

G_DEFINE_TYPE_WITH_CODE (PdPrinterImpl, pd_printer_impl, PD_TYPE_PRINTER_SKELETON,
			 G_IMPLEMENT_INTERFACE (PD_TYPE_PRINTER, pd_printer_iface_init));

/* ------------------------------------------------------------------ */

static void
pd_printer_impl_finalize (GObject *object)
{
	PdPrinterImpl *printer = PD_PRINTER_IMPL (object);
	g_free (printer->sysfs_path);
	G_OBJECT_CLASS (pd_printer_impl_parent_class)->finalize (object);
}

static void
pd_printer_impl_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	PdPrinterImpl *printer = PD_PRINTER_IMPL (object);

	switch (prop_id) {
	case PROP_SYSFS_PATH:
		g_value_set_string (value, printer->sysfs_path);
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

	switch (prop_id) {
	case PROP_SYSFS_PATH:
		g_assert (printer->sysfs_path == NULL);
		/* we don't take a reference to the sysfs_path */
		printer->sysfs_path = g_value_dup_string (value);
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
pd_printer_impl_class_init (PdPrinterImplClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = pd_printer_impl_finalize;
	gobject_class->set_property = pd_printer_impl_set_property;
	gobject_class->get_property = pd_printer_impl_get_property;

	/**
	 * PdPrinterImpl:sysfs_path:
	 *
	 * The #PdSerial for the object.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_SYSFS_PATH,
					 g_param_spec_string ("sysfs-path",
							      "Serial",
							      "The sysfs path for the object",
							      NULL,
							      G_PARAM_READWRITE));
}

/* ------------------------------------------------------------------ */

/* runs in thread dedicated to handling @invocation */
static gboolean
pd_printer_impl_test_print (PdPrinter *_printer,
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
pd_printer_iface_init (PdPrinterIface *iface)
{
	//iface->handle_test_print = pd_printer_impl_test_print;
}
