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
#include <glib/gi18n-lib.h>

#include <gudev/gudev.h>

#include "pd-common.h"
#include "pd-daemon.h"
#include "pd-engine.h"
#include "pd-manager-impl.h"
#include "pd-device-impl.h"
#include "pd-printer-impl.h"
#include "pd-job-impl.h"
#include "pd-log.h"

/**
 * SECTION:printerdengine
 * @title: PdEngine
 * @short_description: Abstract base class for all data engines
 *
 * Abstract base class for all data engines.
 */

struct _PdEnginePrivate
{
	PdDaemon	*daemon;
	PdObjectSkeleton *manager_object;
	GUdevClient	*gudev_client;
	GHashTable	*path_to_device;
	GHashTable	*id_to_printer;
	guint		 next_job_id;
};

enum
{
	PROP_0,
	PROP_DAEMON
};

static void on_uevent (GUdevClient *client,
		       const gchar *action,
		       GUdevDevice *udevdevice,
		       gpointer user_data);
static void pd_engine_printer_state_notify (PdPrinter *printer);
static void pd_engine_job_state_reasons_notify (PdJob *printer);

G_DEFINE_TYPE (PdEngine, pd_engine, G_TYPE_OBJECT);

static void
pd_engine_dispose (GObject *object)
{
	PdEngine *engine = PD_ENGINE (object);

	engine_debug (engine, "Dispose");

	/* note: we don't hold a ref to engine->priv->daemon */
	if (engine->priv->path_to_device) {
		g_hash_table_unref (engine->priv->path_to_device);
		engine->priv->path_to_device = NULL;
	}

	if (engine->priv->id_to_printer) {
		g_hash_table_unref (engine->priv->id_to_printer);
		engine->priv->id_to_printer = NULL;
	}

	if (engine->priv->gudev_client) {
		g_signal_handlers_disconnect_by_func (engine->priv->gudev_client,
						      on_uevent,
						      engine);

		g_object_unref (engine->priv->gudev_client);
		engine->priv->gudev_client = NULL;
	}

	if (G_OBJECT_CLASS (pd_engine_parent_class)->dispose != NULL)
		G_OBJECT_CLASS (pd_engine_parent_class)->dispose (object);
}

static void
pd_engine_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	PdEngine *engine = PD_ENGINE (object);

	switch (prop_id) {
	case PROP_DAEMON:
		g_value_set_object (value, pd_engine_get_daemon (engine));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_engine_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	PdEngine *engine = PD_ENGINE (object);

	switch (prop_id) {
	case PROP_DAEMON:
		g_assert (engine->priv->daemon == NULL);
		/* we don't take a reference to the daemon */
		engine->priv->daemon = g_value_get_object (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_engine_device_remove (PdEngine *engine,
			 PdDevice *device)
{
	PdDaemon *daemon;
	gchar *object_path = NULL;
	daemon = pd_engine_get_daemon (engine);
	object_path = g_strdup_printf ("/org/freedesktop/printerd/device/%s",
				       pd_device_impl_get_id (PD_DEVICE_IMPL (device)));
	engine_debug (engine, "remove device %s", object_path);
	g_dbus_object_manager_server_unexport (pd_daemon_get_object_manager (daemon),
					       object_path);
	g_free (object_path);
}

static PdDevice *
pd_engine_device_add (PdEngine *engine,
		      GUdevDevice *udevdevice)
{
	const gchar *id;
	const gchar *ieee1284_id;
	GHashTable *ieee1284_id_fields = NULL;
	gchar *object_path = NULL;
	GString *uri = NULL;
	GString *description = NULL;
	PdDaemon *daemon;
	PdObjectSkeleton *device_object = NULL;
	PdDevice *device = NULL;
	gchar *mfg;
	gchar *mdl;
	gchar *sn;

	/* get the IEEE1284 ID from the interface device */
	ieee1284_id = g_udev_device_get_sysfs_attr (udevdevice, "ieee1284_id");
	if (ieee1284_id == NULL) {
		engine_warning (engine,
				"failed to get IEEE1284 from %s "
				"(perhaps no usblp?)",
				g_udev_device_get_sysfs_path (udevdevice));
		goto out;
	}

	ieee1284_id_fields = pd_parse_ieee1284_id (ieee1284_id);
	if (ieee1284_id_fields == NULL) {
		engine_warning (engine, "failed to parse IEEE1284 Device ID");
		goto out;
	}

	mfg = g_hash_table_lookup (ieee1284_id_fields, "mfg");
	if (!g_ascii_strcasecmp (mfg, "hewlett-packard"))
		mfg = "HP";
	else if (!g_ascii_strcasecmp (mfg, "lexmark international"))
		mfg = "Lexmark";

	mdl = g_hash_table_lookup (ieee1284_id_fields, "mdl");
	uri = g_string_new ("usb://");
	g_string_append_uri_escaped (uri, mfg, NULL, TRUE);
	g_string_append (uri, "/");
	g_string_append_uri_escaped (uri, mdl, NULL, TRUE);

	sn = g_hash_table_lookup (ieee1284_id_fields, "sn");
	if (sn != NULL) {
		g_string_append (uri, "?serial=");
		g_string_append_uri_escaped (uri, sn, NULL, FALSE);
	}

	daemon = pd_engine_get_daemon (engine);
	description = g_string_new ("");
	g_string_printf (description, "%s %s (USB)", mfg, mdl);

	device = PD_DEVICE (g_object_new (PD_TYPE_DEVICE_IMPL,
					  "daemon", daemon,
					  "ieee1284-id", ieee1284_id,
					  "uri", uri->str,
					  "description", description->str,
					  NULL));
	engine_debug (engine, "add device %s [%s]", uri->str, ieee1284_id);

	/* export on bus */
	id = pd_device_impl_get_id (PD_DEVICE_IMPL (device));
	object_path = g_strdup_printf ("/org/freedesktop/printerd/device/%s", id);
	device_object = pd_object_skeleton_new (object_path);
	pd_object_skeleton_set_device (device_object, device);
	g_dbus_object_manager_server_export (pd_daemon_get_object_manager (daemon),
					     G_DBUS_OBJECT_SKELETON (device_object));
out:
	if (uri)
		g_string_free (uri, TRUE);
	if (description)
		g_string_free (description, TRUE);
	if (ieee1284_id_fields)
		g_hash_table_unref (ieee1284_id_fields);
	g_free (object_path);
	if (device_object)
		g_object_unref (device_object);
	return device;
}

static void
pd_engine_handle_uevent (PdEngine *engine,
			 const gchar *action,
			 GUdevDevice *udevdevice)
{
	gint i_class;
	gint i_subclass;
	PdDevice *device;

	if (g_strcmp0 (action, "remove") == 0) {

		/* look for existing device */
		device = g_hash_table_lookup (engine->priv->path_to_device,
					      g_udev_device_get_sysfs_path (udevdevice));
		if (device != NULL) {
			pd_engine_device_remove (engine, device);
			g_hash_table_remove (engine->priv->path_to_device,
					     g_udev_device_get_sysfs_path (udevdevice));
		}
		return;
	}
	if (g_strcmp0 (action, "add") == 0) {

		/* not a printer */
		i_class = g_udev_device_get_sysfs_attr_as_int (udevdevice, "bInterfaceClass");
		i_subclass = g_udev_device_get_sysfs_attr_as_int (udevdevice, "bInterfaceSubClass");
		if (i_class != 0x07 ||
		    i_subclass != 0x01)
			return;

		/* add to hash and add to database */
		device = pd_engine_device_add (engine, udevdevice);
		if (device)
			g_hash_table_insert (engine->priv->path_to_device,
					     g_strdup (g_udev_device_get_sysfs_path (udevdevice)),
					     (gpointer) device);
		return;
	}
}

static void
on_uevent (GUdevClient *client,
	   const gchar *action,
	   GUdevDevice *udevdevice,
	   gpointer user_data)
{
	PdEngine *engine = PD_ENGINE (user_data);
	pd_engine_handle_uevent (engine, action, udevdevice);
}

static void
pd_engine_init (PdEngine *engine)
{
	const gchar *subsystems[] = {"usb", NULL};

	engine->priv = G_TYPE_INSTANCE_GET_PRIVATE (engine, PD_TYPE_ENGINE, PdEnginePrivate);

	engine->priv->next_job_id = 1;

	/* get ourselves an udev client */
	engine->priv->gudev_client = g_udev_client_new (subsystems);
	g_signal_connect (engine->priv->gudev_client,
			  "uevent",
			  G_CALLBACK (on_uevent),
			  engine);
}

static void
pd_engine_class_init (PdEngineClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->dispose = pd_engine_dispose;
	gobject_class->set_property = pd_engine_set_property;
	gobject_class->get_property = pd_engine_get_property;

	/**
	 * PdEngine:daemon:
	 *
	 * The #PdDaemon the engine is for.
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

	g_type_class_add_private (klass, sizeof (PdEnginePrivate));
}

/**
 * pd_engine_start_job:
 * @printer: A #PdPrinter.
 * @job: A #PdJob.
 *
 * Starts processing the job, and (if possible) sending it to the
 * printer.
 */
static void
pd_engine_start_job	(PdPrinter *printer,
			 PdJob *job)
{
	GValue job_outgoing = G_VALUE_INIT;

	g_value_init (&job_outgoing, G_TYPE_BOOLEAN);

	g_return_if_fail (PD_IS_PRINTER (printer));
	g_return_if_fail (PD_IS_JOB (job));

	pd_printer_set_state (printer, PD_PRINTER_STATE_PROCESSING);
	pd_job_set_state (job, PD_JOB_STATE_PROCESSING);

	g_object_get_property (G_OBJECT (printer),
			       "job-outgoing",
			       &job_outgoing);
	if (!g_value_get_boolean (&job_outgoing)) {
		g_value_set_boolean (&job_outgoing, TRUE);
		g_object_set_property (G_OBJECT (printer),
				       "job-outgoing",
				       &job_outgoing);

		engine_debug (engine,
			      "Job %u ready to start sending to printer",
			      pd_job_get_id (job));

		/* Watch the job-outgoing state reason for this job */
		g_signal_connect (job,
				  "notify::state-reasons",
				  G_CALLBACK (pd_engine_job_state_reasons_notify),
				  job);

		/* Start sending the job to the printer */
		pd_job_impl_start_sending (PD_JOB_IMPL (job));
	}
}

/**
 * pd_engine_job_state_notify:
 * @job: A #PdJob.
 *
 * Examines a job to see if it can be moved from a pending state to a
 * processing state, and moves it if so.
 */
static void
pd_engine_job_state_notify	(PdJob *job)
{
	PdDaemon *daemon;
	const gchar *printer_path;
	PdObject *obj = NULL;
	PdPrinter *printer = NULL;
	guint job_state, printer_state;

	g_return_if_fail (PD_IS_JOB (job));

	job_state = pd_job_get_state (job);
	engine_debug (NULL, "Job %u changed state: %s",
		      pd_job_get_id (job),
		      pd_job_state_as_string (job_state));

	daemon = pd_job_impl_get_daemon (PD_JOB_IMPL (job));
	printer_path = pd_job_get_printer (job);
	obj = pd_daemon_find_object (daemon, printer_path);
	if (!obj)
		goto out;

	printer = pd_object_get_printer (obj);
	printer_state = pd_printer_get_state (printer);

	switch (job_state) {
	case PD_JOB_STATE_PENDING:
		/* This is a now candidate for processing. */

		if (printer_state == PD_PRINTER_STATE_IDLE) {
			engine_debug (NULL,
				      "Printer for job %u idle "
				      "so starting job",
				      pd_job_get_id (job));
			pd_engine_start_job (printer, job);
		}

		break;

	case PD_JOB_STATE_PENDING_HELD:
		break;

	default:
		g_signal_handlers_disconnect_by_func (job,
						      pd_engine_job_state_notify,
						      job);
		break;
	}

 out:
	if (obj)
		g_object_unref (obj);
	if (printer)
		g_object_unref (printer);
}

/**
 * pd_engine_job_state_reasons_notify:
 * @job: A #PdJob.
 *
 * Watches the state reasons of an outgoing job to see when it has
 * finished sending to the printer.
 */
static void
pd_engine_job_state_reasons_notify	(PdJob *job)
{
	PdDaemon *daemon;
	const gchar *printer_path;
	guint job_id = pd_job_get_id (job);
	PdObject *obj = NULL;
	PdPrinter *printer = NULL;
	const gchar *const *state_reasons;
	GValue job_outgoing = G_VALUE_INIT;
	gint i;

	g_return_if_fail (PD_IS_JOB (job));

	g_value_init (&job_outgoing, G_TYPE_BOOLEAN);
	g_value_set_boolean (&job_outgoing, FALSE);

	state_reasons = pd_job_get_state_reasons (PD_JOB (job));
	engine_debug (NULL, "Job %u changed state reasons", job_id);
	for (i = 0; state_reasons[i] != NULL; i++)
		if (!strcmp (state_reasons[i], "job-outgoing"))
			break;

	if (state_reasons[i] == NULL) {
		engine_debug (NULL, "Job %u no longer outgoing", job_id);

		daemon = pd_job_impl_get_daemon (PD_JOB_IMPL (job));
		printer_path = pd_job_get_printer (job);
		obj = pd_daemon_find_object (daemon, printer_path);
		if (!obj)
			goto out;

		printer = pd_object_get_printer (obj);
		g_object_set_property (G_OBJECT (printer),
				       "job-outgoing",
				       &job_outgoing);

		g_signal_handlers_disconnect_by_func (job,
						      pd_engine_job_state_reasons_notify,
						      job);
	}

 out:
	if (obj)
		g_object_unref (obj);
	if (printer)
		g_object_unref (printer);
	g_value_unset (&job_outgoing);
}

/**
 * pd_engine_get_daemon:
 * @engine: A #PdEngine.
 *
 * Gets the daemon used by @engine.
 *
 * Returns: A #PdDaemon. Do not free, the object is owned by @engine.
 */
PdDaemon *
pd_engine_get_daemon (PdEngine *engine)
{
	g_return_val_if_fail (PD_IS_ENGINE (engine), NULL);
	return engine->priv->daemon;
}

/**
 * pd_engine_start:
 * @engine: A #PdEngine.
 *
 * Starts the engine.
 */
void
pd_engine_start	(PdEngine *engine)
{
	GList *devices;
	GList *l;
	PdDaemon *daemon;
	PdManager *manager;

	engine->priv->manager_object = pd_object_skeleton_new ("/org/freedesktop/printerd/Manager");

	/* create manager object */
	daemon = pd_engine_get_daemon (PD_ENGINE (engine));
	manager = pd_manager_impl_new (daemon);
	pd_object_skeleton_set_manager (engine->priv->manager_object, manager);
	g_dbus_object_manager_server_export (pd_daemon_get_object_manager (daemon),
					     G_DBUS_OBJECT_SKELETON (engine->priv->manager_object));

	/* keep a hash to detect removal */
	engine->priv->path_to_device = g_hash_table_new_full (g_str_hash,
							      g_str_equal,
							      g_free,
							      g_object_unref);

	/* keep a hash of printers by id */
	engine->priv->id_to_printer = g_hash_table_new_full (g_str_hash,
							     g_str_equal,
							     g_free,
							     g_object_unref);

	/* start device scanning (for demo) */
	devices = g_udev_client_query_by_subsystem (engine->priv->gudev_client,
						    "usb");
	for (l = devices; l != NULL; l = l->next)
		pd_engine_handle_uevent (engine, "add", G_UDEV_DEVICE (l->data));

	g_list_foreach (devices, (GFunc) g_object_unref, NULL);
	g_list_free (devices);
	g_object_unref (manager);
}

/**
 * pd_engine_add_printer:
 * @engine: A #PdEngine.
 * @printer: A #PdPrinter.
 *
 * Adds a printer and exports it on the bus.  Returns a newly-
 * allocated object.
 */
PdPrinter *
pd_engine_add_printer	(PdEngine *engine,
			 GVariant *options,
			 const gchar *name,
			 const gchar *description,
			 const gchar *location,
			 const gchar *ieee1284_id)
{
	const gchar *id;
	GString *objid = NULL;
	PdObjectSkeleton *printer_object = NULL;
	PdPrinter *printer = NULL;
	gchar *object_path = NULL;
	gchar *driver = NULL;
	PdDaemon *daemon;

	g_return_val_if_fail (PD_IS_ENGINE (engine), NULL);

	if (options)
		g_variant_lookup (options,
				  "driver-name",
				  "s", &driver);

	daemon = pd_engine_get_daemon (engine);
	printer = PD_PRINTER (g_object_new (PD_TYPE_PRINTER_IMPL,
					    "daemon", daemon,
					    "name", name,
					    "description", description,
					    "location", location,
					    "ieee1284-id", ieee1284_id,
					    "driver", driver ? driver : "",
					    NULL));

	/* add it to the hash */
	id = pd_printer_impl_get_id (PD_PRINTER_IMPL (printer));
	if (g_hash_table_lookup (engine->priv->id_to_printer, id) != NULL) {
		/* collision so choose another id */
		unsigned int i;
		engine_debug (engine, "add printer %s - collision", id);
		for (i = 2; i < 1000; i++) {
			objid = g_string_new (id);
			g_string_append_printf (objid, "_%d", i);
			if (g_hash_table_lookup (engine->priv->id_to_printer,
						 objid->str) == NULL)
				break;

			g_string_free (objid, TRUE);
		}

		if (i == 1000)
			goto out;

		pd_printer_impl_set_id (PD_PRINTER_IMPL (printer), objid->str);
	} else
		objid = g_string_new (id);

	g_hash_table_insert (engine->priv->id_to_printer,
			     g_strdup (objid->str),
			     (gpointer) printer);
	engine_debug (engine, "add printer %s", objid->str);

	/* watch for state changes */
	g_signal_connect (printer,
			  "notify::state",
			  G_CALLBACK (pd_engine_printer_state_notify),
			  printer);

	/* export on bus */
	object_path = g_strdup_printf ("/org/freedesktop/printerd/printer/%s",
				       objid->str);
	printer_object = pd_object_skeleton_new (object_path);
	pd_object_skeleton_set_printer (printer_object, printer);
	g_dbus_object_manager_server_export (pd_daemon_get_object_manager (daemon),
					     G_DBUS_OBJECT_SKELETON (printer_object));

 out:
	if (driver)
		g_free (driver);
	if (printer_object)
		g_object_unref (printer_object);
	g_string_free (objid, TRUE);
	g_free (object_path);
	return printer;
}

/**
 * pd_engine_remove_printer:
 * @engine: A #PdEngine.
 * @printer_path: An object path.
 *
 * Removes the printer.
 *
 * Returns: True if the printer was removed.
 */
gboolean
pd_engine_remove_printer	(PdEngine *engine,
				 const gchar *printer_path)
{
	gboolean ret = FALSE;
	PdDaemon *daemon = pd_engine_get_daemon (engine);
	PdObject *obj = NULL;
	PdPrinter *printer = NULL;
	const gchar *id;

	obj = pd_daemon_find_object (daemon, printer_path);
	if (!obj)
		goto out;

	printer = pd_object_get_printer (obj);
	id = pd_printer_impl_get_id (PD_PRINTER_IMPL (printer));

	g_dbus_object_manager_server_unexport (pd_daemon_get_object_manager (daemon),
					       printer_path);
	g_hash_table_remove (engine->priv->id_to_printer, id);
	g_signal_handlers_disconnect_by_func (printer,
					      pd_engine_printer_state_notify,
					      printer);

	engine_debug (engine, "remove printer %s", id);
	ret = TRUE;

 out:
	if (obj)
		g_object_unref (obj);
	if (printer) {
		g_object_unref (printer);
	}
	return ret;
}

/**
 * pd_engine_get_printer_by_path:
 * @engine: A #PdEngine.
 * @printer_path: An object path.
 *
 * Gets a reference to the given printer.
 */
PdPrinter *
pd_engine_get_printer_by_path	(PdEngine *engine,
				 const gchar *printer_path)
{
	const gchar *printer_id = g_strrstr (printer_path, "/");
	if (!printer_id)
		return NULL;

	printer_id++;
	return g_object_ref (g_hash_table_lookup (engine->priv->id_to_printer,
						  printer_id));
}

/**
 * pd_engine_add_job:
 * @engine: A #PdEngine.
 * @job: A #PdJob.
 *
 * Creates a new PdJob and exports it on the bus.  Returns the
 * newly-allocated object.
 */
PdJob *
pd_engine_add_job	(PdEngine *engine,
			 const gchar *printer_path,
			 const gchar *name,
			 GVariant *attributes)
{
	PdJob *job = NULL;
	gchar *object_path = NULL;
	PdObjectSkeleton *job_object = NULL;
	PdDaemon *daemon;
	guint job_id;
	g_return_val_if_fail (PD_IS_ENGINE (engine), NULL);

	/* create the job */
	daemon = pd_engine_get_daemon (engine);
	job_id = engine->priv->next_job_id;
	engine->priv->next_job_id++;
	job = PD_JOB (g_object_new (PD_TYPE_JOB_IMPL,
				    "daemon", daemon,
				    "id", job_id,
				    "name", name,
				    "attributes", attributes,
				    "printer", printer_path,
				    NULL));

	/* watch for state changes */
	g_signal_connect (job,
			  "notify::state",
			  G_CALLBACK (pd_engine_job_state_notify),
			  job);

	/* export on bus */
	object_path = g_strdup_printf ("/org/freedesktop/printerd/job/%u",
				       job_id);
	engine_debug (engine, "New job path is %s", object_path);
	job_object = pd_object_skeleton_new (object_path);
	pd_object_skeleton_set_job (job_object, job);
	g_dbus_object_manager_server_export (pd_daemon_get_object_manager (daemon),
					     G_DBUS_OBJECT_SKELETON (job_object));

	if (job_object)
		g_object_unref (job_object);
	g_free (object_path);
	return job;
}

/**
 * pd_engine_remove_job:
 * @engine: A #PdEngine.
 * @job_path: An object path.
 *
 * Removes the job.
 *
 * Returns: True if the job was removed.
 */
gboolean
pd_engine_remove_job	(PdEngine *engine,
			 const gchar *job_path)
{
	gboolean ret = FALSE;
	PdDaemon *daemon = pd_engine_get_daemon (engine);
	PdObject *obj = NULL;
	PdJob *job = NULL;
	guint job_id;

	obj = pd_daemon_find_object (daemon, job_path);
	if (!obj)
		goto out;

	job = pd_object_get_job (obj);
	job_id = pd_job_get_id (job);
	g_dbus_object_manager_server_unexport (pd_daemon_get_object_manager (daemon),
					       job_path);

	g_signal_handlers_disconnect_by_func (job,
					      pd_engine_job_state_notify,
					      job);

	engine_debug (engine, "remove job %u", job_id);
	ret = TRUE;

 out:
	if (obj)
		g_object_unref (obj);
	if (job) {
		g_object_unref (job);
	}
	return ret;
}

/**
 * pd_engine_printer_state_notify:
 * @printer: A #PdPrinter.
 *
 * Examines a printer's state and array of jobs to see whether any
 * should be moved from a pending state to a processing state, and
 * moves them if so.
 */
static void
pd_engine_printer_state_notify	(PdPrinter *printer)
{
	PdJob *job = NULL;
	guint job_id;

	g_return_if_fail (PD_IS_PRINTER (printer));

	engine_debug (NULL, "Printer %s changed state: %s",
		      pd_printer_impl_get_id (PD_PRINTER_IMPL (printer)),
		      pd_printer_state_as_string (pd_printer_get_state (printer)));

	if (pd_printer_get_state (printer) != PD_PRINTER_STATE_IDLE)
		/* Already busy */
		goto out;

	job = pd_printer_impl_get_next_job (PD_PRINTER_IMPL (printer));
	if (job == NULL)
		goto out;

	g_assert (pd_job_get_state (job) == PD_JOB_STATE_PENDING);

	job_id = pd_job_get_id (job);
	engine_debug (NULL,
		      "Job %u now ready to start processing",
		      job_id);
	pd_engine_start_job (printer, job);
 out:
	if (job)
		g_object_unref (job);
}

/**
 * pd_engine_get_printer_ids:
 * @engine: A #PdEngine.
 *
 * Returns a newly-allocated list of printer IDs.  Use g_list_free()
 * when done; do not free or modify the content.
 */
GList *
pd_engine_get_printer_ids	(PdEngine *engine)
{
	g_return_val_if_fail (PD_IS_ENGINE (engine), NULL);
	return g_hash_table_get_keys (engine->priv->id_to_printer);
}

/**
 * pd_engine_get_devices:
 * @engine: A #PdEngine.
 *
 * Returns a newly-allocated list of Devices.  Use g_list_free()
 * when done and g_object_unref() on the content.
 */
GList *
pd_engine_get_devices	(PdEngine *engine)
{
	GList *devices;
	g_return_val_if_fail (PD_IS_ENGINE (engine), NULL);
	devices = g_hash_table_get_values (engine->priv->path_to_device);
	g_list_foreach (devices, (GFunc) g_object_ref, NULL);
	return devices;
}

/**
 * pd_engine_new:
 * @daemon: A #PdDaemon.
 *
 * Create a new engine object for -specific objects / functionality.
 *
 * Returns: A #PdEngine object. Free with g_object_unref().
 */
PdEngine *
pd_engine_new (PdDaemon *daemon)
{
	g_return_val_if_fail (PD_IS_DAEMON (daemon), NULL);
	return PD_ENGINE (g_object_new (PD_TYPE_ENGINE,
					"daemon", daemon,
					NULL));
}
