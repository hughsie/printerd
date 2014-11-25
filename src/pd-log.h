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

#ifndef __PD_LOG_H__
#define __PD_LOG_H__

#ifdef HAVE_SYSTEMD
# include <systemd/sd-journal.h>
#else
# include <syslog.h>
#endif /* HAVE_SYSTEMD */

G_BEGIN_DECLS

#ifdef HAVE_SYSTEMD
# define printer_log(printer,priority,g_,msg,args...)			\
do {									\
	const gchar *_name = pd_printer_get_name (PD_PRINTER (printer)); \
	sd_journal_send("MESSAGE=[Printer %s] " msg, _name, ##args,	\
			"PRIORITY=%i", priority,			\
			"PRINTERD_PRINTER=%s", _name,			\
			NULL);						\
	g_("[Printer %s] " msg, _name, ##args);				\
} while (0)

# define job_log(job,priority,g_,msg,args...)				\
do {									\
	guint _id = pd_job_get_id (PD_JOB (job));			\
	const gchar *_path = pd_job_get_printer (PD_JOB (job));		\
	PdDaemon *_daemon = pd_job_impl_get_daemon (PD_JOB_IMPL (job));	\
	PdObject *_obj = pd_daemon_find_object (_daemon,		\
						_path);			\
	PdPrinter *_printer = NULL;					\
	const gchar *_name = NULL;					\
	if (_obj) {							\
		_printer = pd_object_get_printer (_obj);		\
		_name = pd_printer_get_name (PD_PRINTER (_printer));	\
	}								\
	sd_journal_send("MESSAGE=[Job %u] " msg, _id, ##args,		\
			"PRIORITY=%i", priority,			\
			"PRINTERD_JOB_ID=%u", _id,			\
			_name ? "PRINTERD_PRINTER=%s" : NULL, _name,	\
			NULL);						\
	if (_obj)							\
		g_object_unref (_obj);					\
	if (_printer)							\
		g_object_unref (_printer);				\
	g_("[Job %u] " msg, _id, ##args);				\
} while (0)

# define manager_log(manager,priority,g_,msg,args...)		\
do {								\
	sd_journal_send("MESSAGE=[Manager] " msg, ##args,	\
			"PRIORITY=%i", priority,		\
			NULL);					\
	g_("[Manager] " msg, ##args);				\
} while (0)

# define engine_log(engine,priority,g_,msg,args...)		\
do {								\
	sd_journal_send("MESSAGE=[Engine] " msg, ##args,	\
			"PRIORITY=%i", priority,		\
			NULL);					\
	g_("[Engine] " msg, ##args);					\
} while (0)

#else /* !defined(HAVE_SYSTEMD) */

# define printer_log(printer,priority,g_,msg,args...)			\
do {									\
	const gchar *_name = pd_printer_get_name (PD_PRINTER (printer)); \
	syslog(priority, "[Printer %s] " msg, _name, ##args);		\
	g_("[Printer %s] " msg, _name, ##args);				\
} while (0)

# define job_log(job,priority,g_,msg,args...)		\
do {							\
	guint _id = pd_job_get_id (PD_JOB (job));	\
	syslog(priority, "[Job %u] " msg, _id, ##args);	\
	g_("[Job %u] " msg, _id, ##args);		\
} while (0)

# define manager_log(manager,priority,g_,msg,args...)	\
do {							\
	syslog(priority, "[Manager] " msg, ##args);	\
	g_("[Manager] " msg, ##args);			\
} while (0);

#define engine_log(engine,priority,g_,msg,args...)	\
do {							\
	syslog(priority, "[Engine] " msg, ##args);	\
	g_("[Engine] " msg, ##args);			\
} while (0);

#endif /* defined(HAVE_SYSTEMD) */

#define printer_debug(printer,msg,args...)			\
	printer_log(printer,LOG_DEBUG,g_debug,msg,##args)

#define printer_warning(printer,msg,args...)			\
	printer_log(printer,LOG_WARNING,g_warning,msg,##args)

#define printer_error(printer,msg,args...)			\
	printer_log(printer,LOG_ERR,g_warning,msg,##args)


#define job_debug(job,msg,args...)				\
	job_log(job,LOG_DEBUG,g_debug,msg,##args)

#define job_warning(job,msg,args...)				\
	job_log(job,LOG_WARNING,g_warning,msg,##args)

#define job_error(job,msg,args...)				\
	job_log(job,LOG_ERR,g_warning,msg,##args)

#define manager_debug(manager,msg,args...)		\
	manager_log(manager,LOG_DEBUG,g_debug,msg,##args)

#define manager_warning(manager,msg,args...)		\
	manager_log(manager,LOG_WARNING,g_warning,msg,##args)

#define manager_error(manager,msg,args...)		\
	manager_log(manager,LOG_ERR,g_warning,msg,##args)

#define engine_debug(engine,msg,args...)		\
	engine_log(engine,LOG_DEBUG,g_debug,msg,##args)

#define engine_warning(engine,msg,args...)		\
	engine_log(engine,LOG_WARNING,g_warning,msg,##args)

#define engine_error(engine,msg,args...)		\
	engine_log(engine,LOG_ERR,g_warning,msg,##args)


G_END_DECLS

#endif /* __PD_LOG_H__ */
