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
