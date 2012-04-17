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

#include <glib.h>
#include <glib/gi18n.h>
#include <printerd/printerd.h>
#include <stdio.h>

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
		g_printerr ("Error getting printerd manager proxy: %s",
			    error->message);
		g_error_free (error);
		goto out;
	}

	if (!strcmp (argv[1], "get-printers"))
		ret = get_printers (pd_manager);
	else if (!strcmp (argv[1], "get-devices"))
		ret = get_devices (pd_manager);
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
