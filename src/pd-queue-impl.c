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

#include "pd-queue-impl.h"

/**
 * SECTION:pdqueue
 * @title: PdQueueImpl
 * @short_description: Implementation of #PdQueueImpl
 *
 * This type provides an implementation of the #PdQueueImpl
 * interface on .
 */

typedef struct _PdQueueImplClass	PdQueueImplClass;

/**
 * PdQueueImpl:
 *
 * The #PdQueueImpl structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _PdQueueImpl
{
	PdQueueSkeleton	 parent_instance;
	gchar			*sysfs_path;
};

struct _PdQueueImplClass
{
	PdQueueSkeletonClass parent_class;
};

enum
{
	PROP_0,
	PROP_SYSFS_PATH
};

static void pd_queue_iface_init (PdQueueIface *iface);

G_DEFINE_TYPE_WITH_CODE (PdQueueImpl, pd_queue_impl, PD_TYPE_QUEUE_SKELETON,
			 G_IMPLEMENT_INTERFACE (PD_TYPE_QUEUE, pd_queue_iface_init));

/* ------------------------------------------------------------------ */

static void
pd_queue_impl_finalize (GObject *object)
{
	PdQueueImpl *queue = PD_QUEUE_IMPL (object);
	g_free (queue->sysfs_path);
	G_OBJECT_CLASS (pd_queue_impl_parent_class)->finalize (object);
}

static void
pd_queue_impl_get_property (GObject *object,
			 guint prop_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	PdQueueImpl *queue = PD_QUEUE_IMPL (object);

	switch (prop_id) {
	case PROP_SYSFS_PATH:
		g_value_set_string (value, queue->sysfs_path);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_queue_impl_set_property (GObject *object,
			 guint prop_id,
			 const GValue *value,
			 GParamSpec *pspec)
{
	PdQueueImpl *queue = PD_QUEUE_IMPL (object);

	switch (prop_id) {
	case PROP_SYSFS_PATH:
		g_assert (queue->sysfs_path == NULL);
		/* we don't take a reference to the sysfs_path */
		queue->sysfs_path = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_queue_impl_init (PdQueueImpl *queue)
{
	g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (queue),
					     G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
pd_queue_impl_class_init (PdQueueImplClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = pd_queue_impl_finalize;
	gobject_class->set_property = pd_queue_impl_set_property;
	gobject_class->get_property = pd_queue_impl_get_property;

	/**
	 * PdQueueImpl:sysfs_path:
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
pd_queue_impl_test_print (PdQueue *_queue,
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
pd_queue_iface_init (PdQueueIface *iface)
{
	iface->handle_test_print = pd_queue_impl_test_print;
}
