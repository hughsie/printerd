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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"

#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <printerd/printerd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

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
print_files (const char *printer_id,
	     char **files)
{
	GError *error = NULL;
	gint ret = 1;
	GDBusConnection *connection;
	PdPrinter *pd_printer = NULL;
	PdJob *pd_job = NULL;
	gchar *printer_path = NULL;
	gchar *job_path = NULL;
	GVariantBuilder options;
	GVariantBuilder attributes;
	gchar **file;

	/* Get the Printer */
	printer_path = g_strdup_printf ("/org/freedesktop/printerd/printer/%s",
					printer_id);
	g_debug ("Getting printer %s", printer_path);
	pd_printer = pd_printer_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
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
					      NULL,
					      &error)) {
		g_printerr ("Error creating job: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	g_debug ("Job created: %s", job_path);

	/* Add documents to the job. Need to do this with a low-level
	   call to add the file descriptor list. */
	connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (pd_printer));
	for (file = files; *file; file++) {
		gint fd;
		GUnixFDList *fd_list = g_unix_fd_list_new ();
		GDBusMessage *request = NULL;
		GDBusMessage *reply = NULL;
		GVariant *body;

		if (!fd_list)
			goto next_document;

		request = g_dbus_message_new_method_call ("org.freedesktop.printerd",
							  job_path,
							  "org.freedesktop.printerd.Job",
							  "AddDocument");
		fd = open (files[0], O_RDONLY);
		if (fd < 0) {
			g_printerr ("Error opening file: %s\n", error->message);
			g_error_free (error);
			goto next_document;
		}
		if (g_unix_fd_list_append (fd_list, fd, NULL) == -1) {
			close (fd);
			g_printerr ("Error adding fd to list\n");
			goto next_document;
		}

		g_dbus_message_set_unix_fd_list (request, fd_list);

		/* fd was dup'ed by set_unix_fd_list */
		close (fd);

		g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
		body = g_variant_new ("(@a{sv}@h)",
				      g_variant_builder_end (&options),
				      g_variant_new_handle (0));
		g_dbus_message_set_body (request, body);

		/* send sync message to the bus */
		reply = g_dbus_connection_send_message_with_reply_sync (connection,
									request,
									G_DBUS_SEND_MESSAGE_FLAGS_NONE,
									5000,
									NULL,
									NULL,
									&error);
		if (reply == NULL) {
			g_printerr ("Error adding document: %s\n", error->message);
			g_error_free (error);
			goto next_document;
		}

		g_debug ("Document added");
		goto next_document;

	next_document:
		if (fd_list != NULL)
			g_object_unref (fd_list);
		if (request != NULL)
			g_object_unref (request);
		if (reply != NULL)
			g_object_unref (reply);
	}

	/* Now get the job and start it */
	pd_job = pd_job_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
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

	if (!pd_job_call_start_sync (pd_job, NULL, &error)) {
		g_printerr ("Error starting job: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	g_debug ("Job started");
	ret = 0;
 out:
	g_free (printer_path);
	g_free (job_path);
	if (pd_printer)
		g_object_unref (pd_printer);
	if (pd_job != NULL)
		g_object_unref (pd_job);
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
	gchar *destination = NULL;
	GOptionEntry opt_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "destination", 'd', 0, G_OPTION_ARG_STRING, &destination,
		  _("Destination printer ID"), NULL },
		{ NULL }
	};

	g_type_init ();

	opt_context = g_option_context_new ("command");
	g_option_context_set_summary (opt_context,
				      "Commands:\n"
				      "  get-printers\n"
				      "  get-devices\n");
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

	if (argc < 2) {
		g_print ("%s",
			 g_option_context_get_help (opt_context, TRUE, NULL));
		goto out;
	}

	g_debug ("getting printerd manager");
	pd_manager = pd_manager_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
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

	if (!strcmp (argv[1], "get-printers"))
		ret = get_printers (pd_manager);
	else if (!strcmp (argv[1], "get-devices"))
		ret = get_devices (pd_manager);
	else if (!strcmp (argv[1], "print-files"))
		ret = print_files (argv[2], argv + 3);
	else
		fprintf (stderr, "Unrecognized command '%s'\n", argv[1]);

 out:
	if (object_manager)
		g_object_unref (object_manager);
	if (pd_manager)
		g_object_unref (pd_manager);
	if (destination)
		g_free (destination);
	return ret;
}
