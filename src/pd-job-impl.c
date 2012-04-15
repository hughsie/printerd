/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Tim Waugh <twaugh@redhat.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"

#include <glib.h>

#include "pd-job-impl.h"

/**
 * SECTION:pdjob
 * @title: PdJobImpl
 * @short_description: Implementation of #PdJobImpl
 *
 * This type provides an implementation of the #PdJobImpl
 * interface on .
 */

typedef struct _PdJobImplClass	PdJobImplClass;

/**
 * PdJobImpl:
 *
 * The #PdJobImpl structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _PdJobImpl
{
	PdJobSkeleton	 parent_instance;
	gchar		*name;
	GHashTable	*attributes;
};

struct _PdJobImplClass
{
	PdJobSkeletonClass parent_class;
};

enum
{
	PROP_0,
	PROP_NAME,
	PROP_ATTRIBUTES,
};

static void pd_job_iface_init (PdJobIface *iface);

G_DEFINE_TYPE_WITH_CODE (PdJobImpl, pd_job_impl, PD_TYPE_JOB_SKELETON,
			 G_IMPLEMENT_INTERFACE (PD_TYPE_JOB, pd_job_iface_init));

/* ------------------------------------------------------------------ */

static void
pd_job_impl_finalize (GObject *object)
{
	PdJobImpl *job = PD_JOB_IMPL (object);
	g_free (job->name);
	G_OBJECT_CLASS (pd_job_impl_parent_class)->finalize (object);
}

static void
pd_job_impl_get_property (GObject *object,
			  guint prop_id,
			  GValue *value,
			  GParamSpec *pspec)
{
	PdJobImpl *job = PD_JOB_IMPL (object);
	GVariantBuilder builder;
	GHashTableIter iter;
	gchar *dkey;
	GVariant *dvalue;

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, job->name);
		break;
	case PROP_ATTRIBUTES:
		g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
		g_hash_table_iter_init (&iter, job->attributes);
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
pd_job_impl_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec)
{
	PdJobImpl *job = PD_JOB_IMPL (object);
	GVariantIter iter;
	gchar *dkey;
	GVariant *dvalue;

	switch (prop_id) {
	case PROP_NAME:
		g_free (job->name);
		job->name = g_value_dup_string (value);
		break;
	case PROP_ATTRIBUTES:
		g_hash_table_remove_all (job->attributes);
		g_variant_iter_init (&iter, g_value_get_variant (value));
		while (g_variant_iter_next (&iter, "{sv}", &dkey, &dvalue))
			g_hash_table_insert (job->attributes, dkey, dvalue);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_job_impl_init (PdJobImpl *job)
{
	g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (job),
					     G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
pd_job_impl_class_init (PdJobImplClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = pd_job_impl_finalize;
	gobject_class->set_property = pd_job_impl_set_property;
	gobject_class->get_property = pd_job_impl_get_property;

	/**
	 * PdJobImpl:name:
	 *
	 * The name for the job.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "Name",
							      "The name for the job",
							      NULL,
							      G_PARAM_READWRITE));
}

/* ------------------------------------------------------------------ */

static void
pd_job_iface_init (PdJobIface *iface)
{
	//iface->handle_xxx = pd_job_impl_xxx;
}
