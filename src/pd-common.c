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
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>

#include "pd-common.h"
#include "pd-job-impl.h"
#include "pd-printer-impl.h"

GHashTable *
pd_parse_ieee1284_id (const gchar *idstring)
{
	GHashTable *fields;
	gchar **fvs;
	gchar **each;

	g_return_val_if_fail (idstring, NULL);

	fields = g_hash_table_new_full (g_str_hash,
					g_str_equal,
					g_free,
					g_free);

	fvs = g_strsplit (idstring, ";", 0);

	/* parse the IEEE 1284 Device ID */
	for (each = fvs; *each; each++) {
		gchar *fieldname;
		gchar **fv = g_strsplit (*each, ":", 2);
		if (fv[0] == NULL || fv[1] == NULL)
			continue;

		g_strstrip (fv[0]);
		g_strstrip (fv[1]);
		if (!g_ascii_strcasecmp (fv[0], "mfg") ||
		    !g_ascii_strcasecmp (fv[0], "manufacturer")) {
			fieldname = g_strdup ("mfg");
		} else if (!g_ascii_strcasecmp (fv[0], "mdl") ||
			   !g_ascii_strcasecmp (fv[0], "model")) {
			fieldname = g_strdup ("mdl");
		} else if (!g_ascii_strcasecmp (fv[0], "des") ||
			   !g_ascii_strcasecmp (fv[0], "description")) {
			fieldname = g_strdup ("des");
		} else
			fieldname = g_ascii_strdown (fv[0], -1);

		g_hash_table_insert (fields, fieldname, g_strdup (fv[1]));
		g_strfreev (fv);
	}

	g_strfreev (fvs);
	return fields;
}

const gchar *
pd_job_state_as_string (guint job_state)
{
	static const gchar *text[] = {
		"pending",
		"pending-held",
		"processing",
		"processing-stopped",
		"canceled",
		"aborted",
		"completed"
	};

	if (job_state < PD_JOB_STATE_PENDING ||
	    job_state - 3 > sizeof (text))
		return "unknown";

	return text[job_state - PD_JOB_STATE_PENDING];
}

const gchar *
pd_printer_state_as_string (guint printer_state)
{
	static const gchar *text[] = {
		"idle",
		"processing",
		"stopped"
	};

	if (printer_state < PD_PRINTER_STATE_IDLE ||
	    printer_state - 3 > sizeof (text))
		return "unknown";

	return text[printer_state - PD_PRINTER_STATE_IDLE];
}

gchar *
pd_get_unix_user (GDBusMethodInvocation *invocation)
{
	GError *error = NULL;
	gchar *ret;
	GDBusConnection *connection;
	GDBusProxy *dbus_proxy = NULL;
	GVariant *uid_reply = NULL;
	GVariantIter iter_uid;
	GVariant *uid = NULL;
	struct passwd pwd, *result;
	gchar *buf = NULL;
	gsize bufsize;
	int err;
	const gchar *sender;

	connection = g_dbus_method_invocation_get_connection (invocation);
	dbus_proxy = g_dbus_proxy_new_sync (connection,
					    G_DBUS_PROXY_FLAGS_NONE,
					    NULL,
					    "org.freedesktop.DBus",
					    "/org/freedesktop/DBus",
					    "org.freedesktop.DBus",
					    NULL,
					    &error);
	if (dbus_proxy == NULL) {
		g_warning ("Unable to get DBus proxy: %s", error->message);
		g_error_free (error);
		goto out;
	}

	sender = g_dbus_method_invocation_get_sender (invocation);
	uid_reply = g_dbus_proxy_call_sync (dbus_proxy,
					    "GetConnectionUnixUser",
					    g_variant_new ("(s)", sender),
					    G_DBUS_CALL_FLAGS_NONE,
					    -1,
					    NULL,
					    &error);
	if (uid_reply == NULL) {
		g_warning ("GetConnectionUnixUser failed: %s", error->message);
		g_error_free (error);
		goto out;
	}

	if (g_variant_iter_init (&iter_uid, uid_reply) != 1) {
		g_warning ("Bad reply from GetConnectionUnixUser");
		goto out;
	}

	uid = g_variant_iter_next_value (&iter_uid);
	if (!uid ||
	    !g_variant_is_of_type (uid, G_VARIANT_TYPE_UINT32)) {
		g_warning ("Bad value type from GetConnectionUnixUser");
		goto out;
	}

	bufsize = sysconf (_SC_GETPW_R_SIZE_MAX);
	if (bufsize == -1)
		bufsize = 16384;

	buf = g_malloc (bufsize);
	err = getpwuid_r (g_variant_get_uint32 (uid),
			  &pwd,
			  buf,
			  bufsize,
			  &result);
	if (result == NULL) {
		if (err != 0)
			g_warning ("Error looking up unix user: %s",
				   g_strerror (errno));

		g_free (buf);
		buf = NULL;
	}

 out:
	if (uid_reply)
		g_variant_unref (uid_reply);
	if (uid)
		g_variant_unref (uid);

	if (buf) {
		ret = g_strdup (pwd.pw_name);
		g_free (buf);
	} else
		ret = g_strdup (":unknown:");

	return ret;
}

gchar **
add_or_remove_state_reason (const gchar *const *reasons,
			    gchar add_or_remove,
			    const gchar *reason)
{
	gchar **strv;
	guint length;
	gint i, j;

	if (reasons == NULL)
		length = 0;
	else
		length = g_strv_length ((gchar **) reasons);

	strv = g_malloc0_n (2 + length, sizeof (gchar *));
	for (i = 0, j = 0; reasons != NULL && reasons[i] != NULL; i++) {
		if (!g_strcmp0 (reasons[i], reason)) {
			/* Found the state reason */
			if (add_or_remove == '+')
				/* Add: nothing to do */
				break;

			/* Remove: skip it */
			continue;
		}

		strv[j++] = g_strdup (reasons[i]);
	}

	if ((add_or_remove == '+' && reasons != NULL && reasons[i] != NULL) ||
	    (add_or_remove == '-' && i == j))
		/* Nothing to do. */
		goto out;

	if (add_or_remove == '+')
		strv[j++] = g_strdup (reason);

out:
	return strv;
}
