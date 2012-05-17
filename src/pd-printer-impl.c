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
#include "pd-printer-impl.h"
#include "pd-daemon.h"
#include "pd-engine.h"
#include "pd-job-impl.h"

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
	PdDaemon		*daemon;
	gchar			*name;
	gchar			*description;
	gchar			*location;
	gchar			*ieee1284_id;
	GHashTable		*defaults;
	GHashTable		*supported;
	GHashTable		*state_reasons;	/* set, i.e. key==value */
	GPtrArray		*jobs;

	gchar			*id;
};

struct _PdPrinterImplClass
{
	PdPrinterSkeletonClass parent_class;
};

enum
{
	PROP_0,
	PROP_DAEMON,
	PROP_NAME,
	PROP_DESCRIPTION,
	PROP_LOCATION,
	PROP_IEEE1284_ID,
	PROP_DEFAULTS,
	PROP_SUPPORTED,
	PROP_STATE_REASONS,
};

static void pd_printer_iface_init (PdPrinterIface *iface);
static void pd_printer_impl_job_state_notify (PdJob *job);

G_DEFINE_TYPE_WITH_CODE (PdPrinterImpl, pd_printer_impl, PD_TYPE_PRINTER_SKELETON,
			 G_IMPLEMENT_INTERFACE (PD_TYPE_PRINTER, pd_printer_iface_init));

/* ------------------------------------------------------------------ */

static void
pd_printer_impl_remove_job (gpointer data,
			    gpointer user_data)
{
	PdJob *job = data;
	PdDaemon *daemon;
	PdEngine *engine;
	gchar *job_path = NULL;

	daemon = pd_job_impl_get_daemon (PD_JOB_IMPL (job));
	engine = pd_daemon_get_engine (daemon);

	g_signal_handlers_disconnect_by_func (job,
					      pd_printer_impl_job_state_notify,
					      job);

	job_path = g_strdup_printf ("/org/freedesktop/printerd/job/%u",
				    pd_job_get_id (job));
	pd_engine_remove_job (engine, job_path);
}

static void
pd_printer_impl_finalize (GObject *object)
{
	PdPrinterImpl *printer = PD_PRINTER_IMPL (object);

	g_debug ("[Printer %s] Finalize", printer->id);
	/* note: we don't hold a reference to printer->daemon */
	g_free (printer->name);
	g_free (printer->description);
	g_free (printer->location);
	g_free (printer->ieee1284_id);
	g_hash_table_unref (printer->defaults);
	g_hash_table_unref (printer->supported);
	g_hash_table_unref (printer->state_reasons);
	g_ptr_array_foreach (printer->jobs,
			     pd_printer_impl_remove_job,
			     NULL);
	g_ptr_array_free (printer->jobs, TRUE);
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
	case PROP_DAEMON:
		g_value_set_object (value, pd_printer_impl_get_daemon (printer));
		break;
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
					       g_strdup (dkey),
					       g_variant_ref (dvalue));

		g_value_set_variant (value, g_variant_builder_end (&builder));
		break;
	case PROP_SUPPORTED:
		g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
		g_hash_table_iter_init (&iter, printer->supported);
		while (g_hash_table_iter_next (&iter,
					       (gpointer *) &dkey,
					       (gpointer *) &dvalue))
			g_variant_builder_add (&builder, "{sv}",
					       g_strdup (dkey),
					       g_variant_ref (dvalue));

		g_value_set_variant (value, g_variant_builder_end (&builder));
		break;
	case PROP_STATE_REASONS:
		g_value_set_boxed (value,
				   g_hash_table_get_keys (printer->state_reasons));
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
	const gchar **state_reasons;
	const gchar **state_reason;

	switch (prop_id) {
	case PROP_DAEMON:
		g_assert (printer->daemon == NULL);
		/* we don't take a reference to the daemon */
		printer->daemon = g_value_get_object (value);
		break;
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
			g_hash_table_insert (printer->defaults,
					     g_strdup (dkey),
					     g_variant_ref (dvalue));
		break;
	case PROP_SUPPORTED:
		g_hash_table_remove_all (printer->supported);
		g_variant_iter_init (&iter, g_value_get_variant (value));
		while (g_variant_iter_next (&iter, "{sv}", &dkey, &dvalue))
			g_hash_table_insert (printer->supported,
					     g_strdup (dkey),
					     g_variant_ref (dvalue));
		break;
	case PROP_STATE_REASONS:
		state_reasons = g_value_get_boxed (value);
		g_hash_table_remove_all (printer->state_reasons);
		for (state_reason = state_reasons;
		     *state_reason;
		     state_reason++) {
			gchar *r = g_strdup (*state_reason);
			g_hash_table_insert (printer->state_reasons, r, r);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_printer_impl_init (PdPrinterImpl *printer)
{
	GVariantBuilder builder;

	g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (printer),
					     G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);

	/* Defaults */

	printer->defaults = g_hash_table_new_full (g_str_hash,
						   g_str_equal,
						   g_free,
						   (GDestroyNotify) g_variant_unref);

	/* set initial job template attributes */
	g_hash_table_insert (printer->defaults,
			     g_strdup ("media"),
			     g_variant_ref_sink (g_variant_new ("s",
								"iso-a4")));
	/* set initial printer description attributes */
	g_hash_table_insert (printer->defaults,
			     g_strdup ("document-format"),
			     g_variant_ref_sink (g_variant_new ("s",
								"application-pdf")));

	/* Supported values */

	printer->supported = g_hash_table_new_full (g_str_hash,
						    g_str_equal,
						    g_free,
						    (GDestroyNotify) g_variant_unref);

	/* set initial job template supported attributes */
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
	g_variant_builder_add (&builder, "s", "iso-a4");
	g_variant_builder_add (&builder, "s", "na-letter");
	g_hash_table_insert (printer->supported,
			     g_strdup ("media"),
			     g_variant_builder_end (&builder));

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
	g_variant_builder_add (&builder, "s", "application/pdf");
	g_hash_table_insert (printer->supported,
			     g_strdup ("document-format"),
			     g_variant_builder_end (&builder));

	/* State reasons */
	printer->state_reasons = g_hash_table_new_full (g_str_hash,
							g_str_equal,
							g_free,
							NULL);

	/* Array of jobs */
	printer->jobs = g_ptr_array_new_full (0,
					      (GDestroyNotify) g_object_unref);

	/* Set initial state */
	pd_printer_set_state (PD_PRINTER (printer), PD_PRINTER_STATE_IDLE);
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
	 * PdPrinterImpl:daemon:
	 *
	 * The #PdDaemon the printer is for.
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
	 * The queue's default attributes.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_DEFAULTS,
					 g_param_spec_variant ("defaults",
							       "Defaults",
							       "The job template and printer description attributes",
							       G_VARIANT_TYPE ("a{sv}"),
							       NULL,
							       G_PARAM_READWRITE));

	/**
	 * PdPrinterImpl:supported:
	 *
	 * The queue's supported attributes.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_SUPPORTED,
					 g_param_spec_variant ("supported",
							       "Supported attributes",
							       "Supported values for attributes",
							       G_VARIANT_TYPE ("a{sv}"),
							       NULL,
							       G_PARAM_READWRITE));

	/**
	 * PdPrinterImpl:state-reasons:
	 *
	 * The queue's state reasons.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_STATE_REASONS,
					 g_param_spec_boxed ("state-reasons",
							     "State reasons",
							     "The queue's state reasons",
							     G_TYPE_STRV,
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

/**
 * pd_printer_impl_get_daemon:
 * @printer: A #PdPrinterImpl.
 *
 * Gets the daemon used by @printer.
 *
 * Returns: A #PdDaemon. Do not free, the object is owned by @printer.
 */
PdDaemon *
pd_printer_impl_get_daemon (PdPrinterImpl *printer)
{
	g_return_val_if_fail (PD_IS_PRINTER_IMPL (printer), NULL);
	return printer->daemon;
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
		g_debug ("[Printer %s] Defaults: set %s=%s",
			 printer->id, key, val);
		g_free (val);
		g_hash_table_replace (printer->defaults,
				      g_strdup (key),
				      g_variant_ref (value));
	}
}

/**
 * pd_printer_impl_get_uri:
 * @printer: A #PdPrinterImpl.
 *
 * Get an appropriate device URI to start a job on.
 *
 * Return: A device URI. Do not free.
 */
const gchar *
pd_printer_impl_get_uri (PdPrinterImpl *printer)
{
	const gchar *const *device_uris;

	/* Simple implementation: always use the first URI in the list */
	device_uris = pd_printer_get_device_uris (PD_PRINTER (printer));
	return device_uris[0];
}

/**
 * pd_printer_impl_get_next_job:
 * @printer: A #PdPrinterImpl.
 *
 * Get the next job which should be processed, or NULL if there is no
 * suitable job.
 *
 * Returns: A #PdJob or NULL.  Free with g_object_unref.
 */
PdJob *
pd_printer_impl_get_next_job (PdPrinterImpl *printer)
{
	PdJob *best = NULL;
	PdJob *job;
	guint index;

	g_return_val_if_fail (PD_IS_PRINTER_IMPL (printer), NULL);

	index = 0;
	for (index = 0; index < printer->jobs->len; index++) {
		job = g_ptr_array_index (printer->jobs, index);
		if (job == NULL)
			break;

		if (pd_job_get_state (job) == PD_JOB_STATE_PENDING) {
			best = job;
			break;
		}

		index++;
	}

	if (best)
		g_object_ref (best);

	return best;
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

static gboolean
attribute_value_is_supported (PdPrinterImpl *printer,
			      const gchar *key,
			      GVariant *value)
{
	gboolean ret = FALSE;
	GVariant *supported_values;
	GVariantIter iter_supported;
	gchar *supported_val;
	const gchar *provided_val;
	gboolean found;

	/* Is this an attribute for which there are restrictions? */
	supported_values = g_hash_table_lookup (printer->supported,
						key);
	if (!supported_values) {
		ret = TRUE;
		goto out;
	}

	/* Type check. We only have string-valued defaults so far. */
	if (!g_variant_is_of_type (value, G_VARIANT_TYPE ("s"))) {
		g_debug ("[Printer %s] Bad variant type for %s",
			 printer->id, key);
		goto out;
	}

	/* Is the supplied value among those supported? */
	provided_val = g_variant_get_string (value, NULL);
	g_variant_iter_init (&iter_supported, supported_values);
	found = FALSE;
	while (g_variant_iter_loop (&iter_supported, "s", &supported_val)) {
		if (!g_strcmp0 (provided_val, supported_val)) {
			/* Yes, found it. */
			found = TRUE;
			break;
		}
	}

	if (!found) {
		g_debug ("[Printer %s] Unsupported value for %s",
			 printer->id, key);
		goto out;
	}

	/* Passed all checks */
	ret = TRUE;
 out:
	return ret;
}

/**
 * pd_printer_impl_job_state_notify
 * @job: A #PdJob.
 *
 * Watch job state changes in order to update printer state.
 */
static void
pd_printer_impl_job_state_notify (PdJob *job)
{
	const gchar *printer_path;
	PdDaemon *daemon;
	PdObject *obj = NULL;
	PdPrinter *printer = NULL;

	printer_path = pd_job_get_printer (job);
	daemon = pd_job_impl_get_daemon (PD_JOB_IMPL (job));
	obj = pd_daemon_find_object (daemon, printer_path);
	if (!obj)
		goto out;

	printer = pd_object_get_printer (obj);

	switch (pd_job_get_state (job)) {
	case PD_JOB_STATE_CANCELED:
	case PD_JOB_STATE_ABORTED:
	case PD_JOB_STATE_COMPLETED:
		/* Only one job can be processing at a time currently */
		if (pd_printer_get_state (printer) == PD_PRINTER_STATE_PROCESSING)
			pd_printer_set_state (printer,
					      PD_PRINTER_STATE_IDLE);
		break;
	}

 out:
	if (obj)
		g_object_unref (obj);
	if (printer)
		g_object_unref (printer);
}

static void
pd_printer_impl_complete_create_job (PdPrinter *_printer,
				     GDBusMethodInvocation *invocation,
				     GVariant *options,
				     const gchar *name,
				     GVariant *attributes)
{
	PdPrinterImpl *printer = PD_PRINTER_IMPL (_printer);
	PdJob *job;
	gchar *object_path = NULL;
	gchar *printer_path = NULL;
	GVariantBuilder builder;
	GVariantBuilder unsupported;
	GHashTableIter iter_attr;
	GVariantIter iter_supplied;
	gchar *dkey;
	GVariant *dvalue;
	gchar *user = NULL;

	g_debug ("[Printer %s] Creating job", printer->id);

	/* set attributes from job template attributes */
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
	g_hash_table_iter_init (&iter_attr, printer->defaults);
	while (g_hash_table_iter_next (&iter_attr,
				       (gpointer *) &dkey,
				       (gpointer *) &dvalue)) {
		if (!g_variant_lookup_value (attributes, dkey, NULL))
			g_variant_builder_add (&builder, "{sv}",
					       g_strdup (dkey),
					       g_variant_ref (dvalue));
	}

	/* now update the attributes from the supplied ones */
	g_variant_iter_init (&iter_supplied, attributes);
	while (g_variant_iter_next (&iter_supplied, "{sv}", &dkey, &dvalue)) {
		/* Is there a list of supported values? */
		if (!attribute_value_is_supported (printer, dkey, dvalue)) {
			g_dbus_method_invocation_return_error (invocation,
							       PD_ERROR,
							       PD_ERROR_FAILED,
							       "Unsupported value");
			goto out;
		}

		g_variant_builder_add (&builder, "{sv}",
				       g_strdup (dkey),
				       g_variant_ref (dvalue));
	}

	/* Tell the engine to create the job */
	printer_path = g_strdup_printf ("/org/freedesktop/printerd/printer/%s",
					printer->id);
	job = pd_engine_add_job (pd_daemon_get_engine (printer->daemon),
				 printer_path,
				 name,
				 g_variant_builder_end (&builder));

	/* Store the job in our array */
	g_ptr_array_add (printer->jobs, (gpointer) job);

	/* Watch state changes */
	g_signal_connect (job,
			  "notify::state",
			  G_CALLBACK (pd_printer_impl_job_state_notify),
			  job);

	/* Set job-originating-user-name */
	user = pd_get_unix_user (invocation);
	g_debug ("[Printer %s] Originating user is %s",
		 printer->id, user);
	pd_job_impl_set_attribute (PD_JOB_IMPL (job),
				   "job-originating-user-name",
				   g_variant_new_string (user));

	object_path = g_strdup_printf ("/org/freedesktop/printerd/job/%u",
				       pd_job_get_id (job));
	g_debug ("[Printer %s] Job path is %s", printer->id, object_path);
	g_variant_builder_init (&unsupported, G_VARIANT_TYPE ("a{sv}"));
	g_dbus_method_invocation_return_value (invocation,
					       g_variant_new ("(o@a{sv})",
							      object_path,
							      g_variant_builder_end (&unsupported)));

 out:
	g_free (user);
	g_free (object_path);
	g_free (printer_path);
}

/* runs in thread dedicated to handling @invocation */
static gboolean
pd_printer_impl_create_job (PdPrinter *_printer,
			    GDBusMethodInvocation *invocation,
			    GVariant *options,
			    const gchar *name,
			    GVariant *attributes)
{
	PdPrinterImpl *printer = PD_PRINTER_IMPL (_printer);

	/* Check if the user is authorized to create a job */
	if (!pd_daemon_check_authorization_sync (printer->daemon,
						 "org.freedesktop.printerd.job-add",
						 options,
						 N_("Authentication is required to add a job"),
						 invocation))
		goto out;

	pd_printer_impl_complete_create_job (_printer,
					     invocation,
					     options,
					     name,
					     attributes);

 out:
	return TRUE; /* handled the method invocation */
}

static void
pd_printer_iface_init (PdPrinterIface *iface)
{
	iface->handle_set_device_uris = pd_printer_impl_set_device_uris;
	iface->handle_create_job = pd_printer_impl_create_job;
}
