/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012, 2014 Tim Waugh <twaugh@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <printerd/printerd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

static GBusType Bus = G_BUS_TYPE_SYSTEM;

static void
pd_log_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
                  const gchar *message, gpointer user_data)
{
}

static void
pd_log_handler_cb (const gchar *log_domain, GLogLevelFlags log_level,
                   const gchar *message, gpointer user_data)
{
        gchar str_time[255];
        time_t the_time;

        /* not a console */
        if (isatty (fileno (stdout)) == 0) {
                g_print ("%s\n", message);
                return;
        }

        /* header always in green */
        time (&the_time);
        strftime (str_time, 254, "%H:%M:%S", localtime (&the_time));
        g_print ("%c[%dmTI:%s\t", 0x1B, 32, str_time);

        /* critical is also in red */
        if (log_level == G_LOG_LEVEL_CRITICAL ||
            log_level == G_LOG_LEVEL_WARNING ||
            log_level == G_LOG_LEVEL_ERROR) {
                g_print ("%c[%dm%s\n%c[%dm", 0x1B, 31, message, 0x1B, 0);
        } else {
                /* debug in blue */
                g_print ("%c[%dm%s\n%c[%dm", 0x1B, 34, message, 0x1B, 0);
        }
}

static gint
get_devices (PdManager *pd_manager)
{
	GError *error = NULL;
	gint ret = 1;
	gchar **devices = NULL;
	gchar **device;

	if (!pd_manager_call_get_devices_sync (pd_manager,
					       &devices,
					       NULL,
					       &error)) {
		g_printerr ("GetDevices failed: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	for (device = devices; *device; device++) {
		g_debug ("Device path: %s", *device);
		gchar *id = g_strrstr (*device, "/");
		if (id != NULL)
			g_print ("%s\n", id + 1);
	}

	ret = 0;
 out:
	g_strfreev (devices);
	return ret;
}

static gint
get_printers (PdManager *pd_manager)
{
	GError *error = NULL;
	gint ret = 1;
	gchar **printers = NULL;
	gchar **printer;

	if (!pd_manager_call_get_printers_sync (pd_manager,
						&printers,
						NULL,
						&error)) {
		g_printerr ("GetPrinters failed: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	for (printer = printers; *printer; printer++) {
		g_debug ("Printer path: %s", *printer);
		gchar *id = g_strrstr (*printer, "/");
		if (id != NULL)
			g_print ("%s\n", id + 1);
	}

	ret = 0;
 out:
	g_strfreev (printers);
	return ret;
}

static gint
cancel_job (const gchar *job_id)
{
	gint ret = 1;
	GError *error = NULL;
	gchar *job_path = NULL;
	PdJob *pd_job = NULL;
	GVariantBuilder options;

	if (job_id[0] == '/')
		job_path = g_strdup (job_id);
	else
		job_path = g_strdup_printf ("/org/freedesktop/printerd/job/%s",
					    job_id);

	pd_job = pd_job_proxy_new_for_bus_sync (Bus,
						G_DBUS_PROXY_FLAGS_NONE,
						"org.freedesktop.printerd",
						job_path,
						NULL,
						&error);
	if (!pd_job) {
		g_printerr ("Error getting job: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
	if (!pd_job_call_cancel_sync (pd_job,
				      g_variant_builder_end (&options),
				      NULL,
				      &error)) {
		g_printerr ("Error canceling job: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	ret = 0;
 out:
	if (pd_job)
		g_object_unref (pd_job);
	g_free (job_path);
	return ret;
}

static gint
add_documents (const gchar *job_id,
	       const gchar **documents)
{
	GError *error = NULL;
	PdJob *pd_job = NULL;
	gboolean docs_added = FALSE;
	GString *job_path = g_string_new ("");
	const gchar **file;

	g_string_printf (job_path,
			 "/org/freedesktop/printerd/job/%s",
			 job_id);

	/* Add documents to the job. */
	pd_job = pd_job_proxy_new_for_bus_sync (Bus,
						G_DBUS_PROXY_FLAGS_NONE,
						"org.freedesktop.printerd",
						job_path->str,
						NULL,
						&error);
	g_string_free (job_path, TRUE);
	if (!pd_job) {
		g_printerr ("Error getting job: %s\n", error->message);
		g_error_free (error);
		return 1;
	}

	for (file = documents; *file; file++) {
		GVariantBuilder options;
		GUnixFDList *fd_list = g_unix_fd_list_new ();
		gint fd = -1;

		if (!fd_list)
			goto next_document;

		fd = open ((const char *) *file, O_RDONLY);
		if (fd < 0) {
			g_printerr ("Error opening file: %s\n",
				    strerror (errno));
			goto next_document;
		}
		if (g_unix_fd_list_append (fd_list, fd, NULL) == -1) {
			close (fd);
			g_printerr ("Error adding fd to list\n");
			goto next_document;
		}

		g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
		if (!pd_job_call_add_document_sync (pd_job,
						    g_variant_builder_end (&options),
						    g_variant_new_handle (0),
						    fd_list,
						    NULL, /* out_fd_list */
						    NULL, /* cancellable */
						    &error)) {
			g_printerr ("Error adding document: %s\n",
				    error->message);
			g_error_free (error);
			goto next_document;
		}
		if (error) {
			g_printerr ("Error adding document: %s\n",
				    error->message);
			g_error_free (error);
			goto next_document;
		}
		g_debug ("Document added");
		docs_added = TRUE;

	next_document:
		if (fd != -1)
			close (fd);
		if (fd_list != NULL)
			g_object_unref (fd_list);
	}

	return docs_added ? 0 : 1;
}

static gint
print_files (const char *printer_id,
	     char **files)
{
	GError *error = NULL;
	gint ret = 1;
	PdPrinter *pd_printer = NULL;
	PdJob *pd_job = NULL;
	gchar *printer_path = NULL;
	gchar *job_path = NULL;
	GVariant *unsupported = NULL;
	GVariantBuilder options;
	GVariantBuilder attributes;
	GVariantBuilder start_options;

	/* Get the Printer */
	printer_path = g_strdup_printf ("/org/freedesktop/printerd/printer/%s",
					printer_id);
	g_debug ("Getting printer %s", printer_path);
	pd_printer = pd_printer_proxy_new_for_bus_sync (Bus,
							G_DBUS_PROXY_FLAGS_NONE,
							"org.freedesktop.printerd",
							printer_path,
							NULL,
							&error);

	if (!pd_printer) {
		g_printerr ("Error getting printerd printer proxy: %s\n",
			    error->message);
		g_error_free (error);
		goto out;
	}

	/* Create a job for the printer */
	g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_init (&attributes, G_VARIANT_TYPE ("a{sv}"));
	if (!pd_printer_call_create_job_sync (pd_printer,
					      g_variant_builder_end (&options),
					      "New job",
					      g_variant_builder_end (&attributes),
					      &job_path,
					      &unsupported,
					      NULL,
					      &error)) {
		g_printerr ("Error creating job: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	g_debug ("Job created: %s", job_path);

	if (add_documents (strrchr (job_path, '/') + 1,
			   (const gchar **) files)) {
		cancel_job (job_path);
		goto out;
	}

	/* Now get the job and start it */
	pd_job = pd_job_proxy_new_for_bus_sync (Bus,
						G_DBUS_PROXY_FLAGS_NONE,
						"org.freedesktop.printerd",
						job_path,
						NULL,
						&error);
	if (!pd_job) {
		g_printerr ("Error getting job: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	g_variant_builder_init (&start_options, G_VARIANT_TYPE ("a{sv}"));
	if (!pd_job_call_start_sync (pd_job,
				     g_variant_builder_end (&start_options),
				     NULL,
				     &error)) {
		g_printerr ("Error starting job: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	g_debug ("Job started");
	g_print ("Job path is %s\n", job_path);
	ret = 0;
 out:
	g_free (printer_path);
	g_free (job_path);
	if (unsupported)
		g_variant_unref (unsupported);
	if (pd_printer)
		g_object_unref (pd_printer);
	if (pd_job)
		g_object_unref (pd_job);
	return ret;
}

static gint
create_printer_from_device (const gchar *name,
			    const gchar *device)
{
	GError *error = NULL;
	gint ret = 1;
	gchar *device_path = NULL;
	gchar *printer_path = NULL;
	PdDevice *pd_device = NULL;
	GVariantBuilder options, defaults;

	device_path = g_strdup_printf ("/org/freedesktop/printerd/device/%s",
				       device);
	pd_device = pd_device_proxy_new_for_bus_sync (Bus,
						      G_DBUS_PROXY_FLAGS_NONE,
						      "org.freedesktop.printerd",
						      device_path,
						      NULL,
						      &error);
	if (!pd_device) {
		g_printerr ("Error getting device: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_init (&defaults, G_VARIANT_TYPE ("a{sv}"));
	if (!pd_device_call_create_printer_sync (pd_device,
						 g_variant_builder_end (&options),
						 name,
						 "" /* description */,
						 "" /* location */,
						 g_variant_builder_end (&defaults),
						 &printer_path,
						 NULL,
						 &error)) {
		g_printerr ("Error creating printer: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	g_debug ("Printer path is %s\n", printer_path);
	ret = 0;
 out:
	if (pd_device)
		g_object_unref (pd_device);
	g_free (device_path);
	g_free (printer_path);
	return ret;
}

static gint
create_printer (const gchar *name,
		const gchar *device_uri)
{
	gint ret = 1;
	GError *error = NULL;
	PdManager *pd_manager = NULL;
	gchar *printer_path = NULL;
	GVariantBuilder options, defaults;
	const gchar *device_uris[2];

	/* Have we got a device URI? */
	if (!strchr (device_uri, '/'))
		return create_printer_from_device (name, device_uri);

	pd_manager = pd_manager_proxy_new_for_bus_sync (Bus,
							G_DBUS_PROXY_FLAGS_NONE,
							"org.freedesktop.printerd",
							"/org/freedesktop/printerd/Manager",
							NULL,
							&error);
	if (!pd_manager) {
		g_printerr ("Error getting manager: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	device_uris[0] = device_uri;
	device_uris[1] = NULL;
	g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_init (&defaults, G_VARIANT_TYPE ("a{sv}"));
	if (!pd_manager_call_create_printer_sync (pd_manager,
						  g_variant_builder_end (&options),
						  name,
						  "" /* description */,
						  "" /* location */,
						  device_uris,
						  g_variant_builder_end (&defaults),
						  &printer_path,
						  NULL,
						  &error)) {
		g_printerr ("Error creating printer: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	g_debug ("Printer path is %s\n", printer_path);
	ret = 0;
 out:
	if (pd_manager)
		g_object_unref (pd_manager);
	g_free (printer_path);
	return ret;
}

static gint
delete_printer (const gchar *name)
{
	gint ret = 1;
	GError *error = NULL;
	PdManager *pd_manager = NULL;
	gchar *printer_path = NULL;
	GVariantBuilder options;

	pd_manager = pd_manager_proxy_new_for_bus_sync (Bus,
							G_DBUS_PROXY_FLAGS_NONE,
							"org.freedesktop.printerd",
							"/org/freedesktop/printerd/Manager",
							NULL,
							&error);
	if (!pd_manager) {
		g_printerr ("Error getting manager: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	printer_path = g_strdup_printf ("/org/freedesktop/printerd/printer/%s",
					name);
	g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
	if (!pd_manager_call_delete_printer_sync (pd_manager,
						  g_variant_builder_end (&options),
						  printer_path,
						  NULL,
						  &error)) {
		g_printerr ("Error deleting printer: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	ret = 0;
 out:
	if (pd_manager)
		g_object_unref (pd_manager);
	g_free (printer_path);
	return ret;
}

int
main (int argc, char **argv)
{
	GError *error = NULL;
	gint ret = 1;
	GDBusObjectManager *object_manager = NULL;
	PdManager *pd_manager = NULL;
	GOptionContext *opt_context = NULL;
	gboolean verbose = FALSE;
	gboolean session = FALSE;
	GOptionEntry opt_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "session", 'S', 0, G_OPTION_ARG_NONE, &session,
		  _("Use the session D-Bus (for testing)"), NULL},
		{ NULL }
	};

#if !GLIB_CHECK_VERSION(2,36,0)
	g_type_init ();
#endif /* glib < 2.36 */

	opt_context = g_option_context_new ("command");
	g_option_context_set_summary (opt_context,
				      "Commands:\n"
				      "  get-printers\n"
				      "  get-devices\n"
				      "  create-printer <name> <device|URI>\n"
				      "  print-files <name> <files...>\n"
				      "  add-documents <jobid> <files...>\n"
				      "  cancel-job <id>\n");
	g_option_context_add_main_entries (opt_context, opt_entries, NULL);
	if (!g_option_context_parse (opt_context, &argc, &argv, &error)) {
		g_printerr ("Error parsing options: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* verbose? */
	if (verbose) {
		g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
		g_log_set_handler ("printerd",
                                   G_LOG_LEVEL_ERROR |
                                   G_LOG_LEVEL_CRITICAL |
                                   G_LOG_LEVEL_DEBUG |
                                   G_LOG_LEVEL_WARNING,
                                   pd_log_handler_cb, NULL);
	} else {
		g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
		g_log_set_handler ("printerd", G_LOG_LEVEL_DEBUG,
                                   pd_log_ignore_cb, NULL);
	}

	/* using session bus? */
	if (session)
		Bus = G_BUS_TYPE_SESSION;

	if (argc < 2) {
		g_print ("%s",
			 g_option_context_get_help (opt_context, TRUE, NULL));
		goto out;
	}

	g_debug ("getting printerd manager");
	pd_manager = pd_manager_proxy_new_for_bus_sync (Bus,
							G_DBUS_PROXY_FLAGS_NONE,
							"org.freedesktop.printerd",
							"/org/freedesktop/printerd/Manager",
							NULL,
							&error);

	if (!pd_manager) {
		g_printerr ("Error getting printerd manager proxy: %s\n",
			    error->message);
		g_error_free (error);
		goto out;
	}

	if (!strcmp (argv[1], "get-printers")) {
		if (argc != 2) {
			g_print ("%s",
				 g_option_context_get_help (opt_context,
							    TRUE,
							    NULL));
			goto out;
		}

		ret = get_printers (pd_manager);
	} else if (!strcmp (argv[1], "get-devices")) {
		if (argc != 2) {
			g_print ("%s",
				 g_option_context_get_help (opt_context,
							    TRUE,
							    NULL));
			goto out;
		}

		ret = get_devices (pd_manager);
	} else if (!strcmp (argv[1], "print-files")) {
		if (argc < 4) {
			g_print ("%s",
				 g_option_context_get_help (opt_context,
							    TRUE,
							    NULL));
			goto out;
		}

		ret = print_files (argv[2], argv + 3);
	} else if (!strcmp (argv[1], "create-printer")) {
		if (argc != 4) {
			g_print ("%s",
				 g_option_context_get_help (opt_context,
							    TRUE,
							    NULL));
			goto out;
		}

		ret = create_printer ((const gchar *) argv[2],
				      (const gchar *) argv[3]);
	} else if (!strcmp (argv[1], "delete-printer")) {
		if (argc != 3) {
			g_print ("%s",
				 g_option_context_get_help (opt_context,
							    TRUE,
							    NULL));
			goto out;
		}

		ret = delete_printer ((const gchar *) argv[2]);
	} else if (!strcmp (argv[1], "cancel-job")) {
		if (argc != 3) {
			g_print ("%s",
				 g_option_context_get_help (opt_context,
							    TRUE,
							    NULL));
			goto out;
		}

		ret = cancel_job ((const gchar *) argv[2]);
	} else if (!strcmp (argv[1], "add-documents")) {
		if (argc < 4) {
			g_print ("%s",
				 g_option_context_get_help (opt_context,
							    TRUE, NULL));
			goto out;
		}

		ret = add_documents ((const gchar *) argv[2],
				     (const gchar **) (argv + 3));
	} else
		fprintf (stderr, "Unrecognized command '%s'\n", argv[1]);

 out:
	if (object_manager)
		g_object_unref (object_manager);
	if (pd_manager)
		g_object_unref (pd_manager);
	return ret;
}
