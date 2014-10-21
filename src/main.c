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
#include <glib/gi18n.h>
#include <stdio.h>

#include <gio/gio.h>
#include <glib-unix.h>

#include "pd-daemontypes.h"
#include "pd-daemon.h"

static gboolean opt_no_sigint = FALSE;
static gboolean opt_replace = FALSE;
static GMainLoop *loop = NULL;
static PdDaemon *the_daemon = NULL;

static void
on_bus_acquired (GDBusConnection *connection,
		 const gchar *name,
		 gpointer user_data)
{
	the_daemon = pd_daemon_new (connection);
	g_debug ("Connected to the system bus");
}

static void
on_name_lost (GDBusConnection *connection,
	      const gchar *name,
	      gpointer user_data)
{
	if (the_daemon == NULL)
		g_error ("Failed to connect to the system message bus");
	else
		g_debug ("Lost (or failed to acquire) the name %s on the system message bus", name);
	g_main_loop_quit (loop);
}

static void
on_name_acquired (GDBusConnection *connection,
		  const gchar *name,
		  gpointer user_data)
{
	g_debug ("Acquired the name %s on the system message bus", name);
}

static gboolean
on_sigint (gpointer user_data)
{
	g_debug ("Caught SIGINT. Initiating shutdown");
	g_main_loop_quit (loop);
	return FALSE;
}

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

int
main (int argc, char **argv)
{
	GError *error = NULL;
	gint ret = 1;
	GOptionContext *opt_context = NULL;
	guint name_owner_id = 0;
	guint sigint_id = 0;
	gboolean verbose = FALSE;
	GOptionEntry opt_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "replace", 'r', 0, G_OPTION_ARG_NONE, &opt_replace,
			"Replace existing daemon", NULL},
		{ "no-sigint", 's', 0, G_OPTION_ARG_NONE, &opt_no_sigint,
			"Do not handle SIGINT for controlled shutdown", NULL},
		{NULL }
	};

	/* avoid gvfs (http://bugzilla.gnome.org/show_bug.cgi?id=526454) */
	if (!g_setenv ("GIO_USE_VFS", "local", TRUE)) {
		g_printerr ("Error setting GIO_USE_GVFS\n");
		goto out;
	}

	opt_context = g_option_context_new ("printer daemon");
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

	loop = g_main_loop_new (NULL, FALSE);

	if (!opt_no_sigint) {
		sigint_id = g_unix_signal_add_full (G_PRIORITY_DEFAULT,
						    SIGINT,
						    on_sigint,
						    NULL,	/* user_data */
						    NULL); /* GDestroyNotify */
	}

	name_owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
					"org.freedesktop.printerd",
					G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
						(opt_replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : 0),
					on_bus_acquired,
					on_name_acquired,
					on_name_lost,
					NULL,
					NULL);
	g_debug ("Entering main event loop");
	g_main_loop_run (loop);

	/* success */
	ret = 0;
 out:
	if (sigint_id > 0)
		g_source_remove (sigint_id);
	if (the_daemon != NULL)
		g_object_unref (the_daemon);
	if (name_owner_id != 0)
		g_bus_unown_name (name_owner_id);
	if (loop != NULL)
		g_main_loop_unref (loop);
	if (opt_context != NULL)
		g_option_context_free (opt_context);
	g_debug ("printerd daemon version %s exiting", PACKAGE_VERSION);
	return ret;
}
