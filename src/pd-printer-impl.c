/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014 Tim Waugh <twaugh@redhat.com>
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
	GPtrArray		*jobs;
	gboolean		 job_outgoing;

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
	PROP_JOB_OUTGOING,
};

static void pd_printer_iface_init (PdPrinterIface *iface);
static void pd_printer_impl_job_state_notify (PdJob *job);
static void pd_printer_impl_job_add_state_reason (PdJobImpl *job,
						  const gchar *reason,
						  PdPrinterImpl *printer);
static void pd_printer_impl_job_remove_state_reason (PdPrinterImpl *printer,
						     const gchar *reason);

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

	g_signal_handlers_disconnect_by_func (job,
					      pd_printer_impl_job_add_state_reason,
					      PD_PRINTER_IMPL (user_data));

	g_signal_handlers_disconnect_by_func (job,
					      pd_printer_impl_job_remove_state_reason,
					      PD_PRINTER_IMPL (user_data));

	job_path = g_strdup_printf ("/org/freedesktop/printerd/job/%u",
				    pd_job_get_id (job));
	pd_engine_remove_job (engine, job_path);
	g_free (job_path);
}

static void
pd_printer_impl_finalize (GObject *object)
{
	PdPrinterImpl *printer = PD_PRINTER_IMPL (object);

	g_debug ("[Printer %s] Finalize", printer->id);
	/* note: we don't hold a reference to printer->daemon */
	g_ptr_array_foreach (printer->jobs,
			     pd_printer_impl_remove_job,
			     printer);
	g_ptr_array_free (printer->jobs, TRUE);
	g_free (printer->id);
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
	case PROP_DAEMON:
		g_value_set_object (value, pd_printer_impl_get_daemon (printer));
		break;
	case PROP_JOB_OUTGOING:
		g_value_set_boolean (value, printer->job_outgoing);
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
	case PROP_DAEMON:
		g_assert (printer->daemon == NULL);
		/* we don't take a reference to the daemon */
		printer->daemon = g_value_get_object (value);
		break;
	case PROP_JOB_OUTGOING:
		printer->job_outgoing = g_value_get_boolean (value);
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
	GVariantBuilder val_builder;

	g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (printer),
					     G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);

	/* Defaults */

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

	/* set initial job template attributes */
	g_variant_builder_add (&builder, "{sv}",
			       g_strdup ("media"),
			       g_variant_ref_sink (g_variant_new ("s",
								  "iso-a4")));

	/* set initial printer description attributes */
	g_variant_builder_add (&builder, "{sv}",
			       g_strdup ("document-format"),
			       g_variant_ref_sink (g_variant_new ("s",
								  "application-pdf")));

	pd_printer_set_defaults (PD_PRINTER (printer),
				 g_variant_ref_sink (g_variant_builder_end (&builder)));

	/* Supported values */

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

	/* set initial job template supported attributes */
	g_variant_builder_init (&val_builder, G_VARIANT_TYPE ("as"));
	g_variant_builder_add (&val_builder, "s", "iso-a4");
	g_variant_builder_add (&val_builder, "s", "na-letter");
	g_variant_builder_add (&builder, "{sv}",
			       g_strdup ("media"),
			       g_variant_builder_end (&val_builder));

	g_variant_builder_init (&val_builder, G_VARIANT_TYPE ("as"));
	g_variant_builder_add (&val_builder, "s", "application/pdf");
	g_variant_builder_add (&builder, "{sv}",
			       g_strdup ("document-format"),
			       g_variant_builder_end (&val_builder));

	pd_printer_set_supported (PD_PRINTER (printer),
				  g_variant_ref_sink (g_variant_builder_end (&builder)));

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
	 * PdPrinterImpl:job-outgoing:
	 *
	 * Whether any job is outgoing.
	 */
	g_object_class_install_property (gobject_class,
					 PROP_JOB_OUTGOING,
					 g_param_spec_boolean ("job-outgoing",
							       "Job outgoing",
							       "Whether any job is outgoing",
							       FALSE,
							       G_PARAM_READWRITE));
}

const gchar *
pd_printer_impl_get_id (PdPrinterImpl *printer)
{
	/* shortcut */
	if (printer->id != NULL)
		goto out;

	printer->id = pd_printer_dup_name (PD_PRINTER (printer));

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

static GVariant *
update_attributes (GVariant *attributes, GVariant *updates)
{
	GVariantBuilder builder;
	GVariantIter iter;
	gchar *dkey;
	GVariant *dvalue;

	/* Add any values from attributes that are not in updates */
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
	g_variant_iter_init (&iter, attributes);
	while (g_variant_iter_next (&iter, "{sv}", &dkey, &dvalue)) {
		if (!g_variant_lookup_value (updates, dkey, NULL))
			g_variant_builder_add (&builder, "{sv}",
					       g_strdup (dkey),
					       g_variant_ref (dvalue));
	}

	/* Now add in the updates */
	g_variant_iter_init (&iter, updates);
	while (g_variant_iter_next (&iter, "{sv}", &dkey, &dvalue))
		g_variant_builder_add (&builder, "{sv}",
				       g_strdup (dkey),
				       g_variant_ref (dvalue));

	return g_variant_builder_end (&builder);
}

void
pd_printer_impl_update_defaults (PdPrinterImpl *printer,
				 GVariant *defaults)
{
	GVariantIter iter;
	GVariant *current_defaults;
	gchar *key;
	GVariant *value;

	g_debug ("[Printer %s] Updating defaults", printer->id);

	/* add/overwrite default values, keeping other existing values */
	g_variant_iter_init (&iter, defaults);
	while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
		gchar *val = g_variant_print (value, TRUE);
		g_debug ("[Printer %s] Defaults: set %s=%s",
			 printer->id, key, val);
		g_free (val);
	}

	current_defaults = pd_printer_get_defaults (PD_PRINTER (printer));
	value = update_attributes (current_defaults,
				   defaults);
	pd_printer_set_defaults (PD_PRINTER (printer),
				 g_variant_ref_sink (value));
}

void
pd_printer_impl_add_state_reason (PdPrinterImpl *printer,
				  const gchar *reason)
{
	const gchar *const *reasons;
	gchar **strv;

	g_debug ("[Printer %s] state-reasons += %s", printer->id, reason);

	reasons = pd_printer_get_state_reasons (PD_PRINTER (printer));
	strv = add_or_remove_state_reason (reasons, '+', reason);
	pd_printer_set_state_reasons (PD_PRINTER (printer),
				      (const gchar *const *) strv);
	g_strfreev (strv);
}

void
pd_printer_impl_remove_state_reason (PdPrinterImpl *printer,
				     const gchar *reason)
{
	const gchar *const *reasons;
	gchar **strv;

	g_debug ("[Printer %s] state-reasons -= %s", printer->id, reason);

	reasons = pd_printer_get_state_reasons (PD_PRINTER (printer));
	strv = add_or_remove_state_reason (reasons, '-', reason);
	pd_printer_set_state_reasons (PD_PRINTER (printer),
				      (const gchar *const *) strv);
	g_strfreev (strv);
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
	GVariant *supported;
	gchar *supported_val;
	const gchar *provided_val;
	gboolean found;

	/* Is this an attribute for which there are restrictions? */
	supported = pd_printer_get_supported (PD_PRINTER (printer));
	supported_values = g_variant_lookup_value (supported,
						   key,
						   G_VARIANT_TYPE ("s"));
	if (!supported_values) {
		ret = TRUE;
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

/**
 * pd_printer_impl_job_add_state_reason
 * @job: A #PdJob.
 *
 * Add a state reason to printer-state-reasons
 */
static void
pd_printer_impl_job_add_state_reason (PdJobImpl *job,
				      const gchar *reason,
				      PdPrinterImpl *printer)
{
	g_return_if_fail (PD_IS_JOB_IMPL (job));
	g_return_if_fail (PD_IS_PRINTER_IMPL (printer));
	pd_printer_impl_add_state_reason (printer, reason);
}

/**
 * pd_printer_impl_job_remove_state_reason
 * @job: A #PdJob.
 *
 * Remove a state reason from printer-state-reasons
 */
static void
pd_printer_impl_job_remove_state_reason (PdPrinterImpl *printer,
					 const gchar *reason)
{
	g_return_if_fail (PD_IS_PRINTER_IMPL (printer));
	pd_printer_impl_remove_state_reason (printer, reason);
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
	GVariant *defaults;
	GVariantBuilder unsupported;
	GVariantIter iter;
	gchar *dkey;
	GVariant *dvalue;
	GVariant *job_attributes;
	gchar *user = NULL;

	g_debug ("[Printer %s] Creating job", printer->id);

	/* set attributes from job template attributes */
	defaults = pd_printer_get_defaults (PD_PRINTER (printer));

	/* Check for unsupported attributes */
	g_variant_builder_init (&unsupported, G_VARIANT_TYPE ("a{sv}"));
	g_variant_iter_init (&iter, attributes);
	while (g_variant_iter_next (&iter, "{sv}", &dkey, &dvalue))
		/* Is there a list of supported values? */
		if (!attribute_value_is_supported (printer, dkey, dvalue)) {
			gchar *val = g_variant_print (dvalue, TRUE);
			g_debug ("[Printer %s] Unsupported attribute %s=%s",
				 printer->id, dkey, val);
			g_free (val);
			g_variant_builder_add (&unsupported, "{sv}",
					       g_strdup (dkey),
					       g_variant_ref (dvalue));
		}

	/* Tell the engine to create the job */
	printer_path = g_strdup_printf ("/org/freedesktop/printerd/printer/%s",
					printer->id);
	job_attributes = update_attributes (defaults,
					    attributes);
	job = pd_engine_add_job (pd_daemon_get_engine (printer->daemon),
				 printer_path,
				 name,
				 g_variant_ref_sink (job_attributes));

	/* Store the job in our array */
	g_ptr_array_add (printer->jobs, (gpointer) job);

	/* Watch state changes */
	g_signal_connect (job,
			  "notify::state",
			  G_CALLBACK (pd_printer_impl_job_state_notify),
			  job);

	/* Watch for printer-state-reasons updates */
	g_signal_connect (job,
			  "add-printer-state-reason",
			  G_CALLBACK (pd_printer_impl_job_add_state_reason),
			  printer);
	g_signal_connect (job,
			  "remove-printer-state-reason",
			  G_CALLBACK (pd_printer_impl_job_remove_state_reason),
			  printer);

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
	g_dbus_method_invocation_return_value (invocation,
					       g_variant_new ("(o@a{sv})",
							      object_path,
							      g_variant_builder_end (&unsupported)));

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
