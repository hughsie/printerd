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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
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
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-unix.h>

#include "pd-common.h"
#include "pd-daemon.h"
#include "pd-engine.h"
#include "pd-job-impl.h"
#include "pd-printer-impl.h"
#include "pd-log.h"

/**
 * SECTION:pdjob
 * @title: PdJobImpl
 * @short_description: Implementation of #PdJobImpl
 *
 * This type provides an implementation of the #PdJobImpl
 * interface on .
 */

typedef struct _PdJobImplClass	PdJobImplClass;

typedef enum
{
	FILTERCHAIN_CMD,
	FILTERCHAIN_FILE_OUTPUT,
	FILTERCHAIN_CUPSFILTER
} PdJobProcessType;

struct _PdJobProcess
{
	PdJobImpl	*job;
	const gchar	*what;
	PdJobProcessType type;
	gchar		*cmd;
	gboolean	 started;
	gboolean	 finished;
	GPid		 pid;
	guint		 process_watch_source;

	guint		 io_source[5];
	GIOChannel	*channel[5];

	gint		 child_fd[5];
	gint		 parent_fd[5];
};

/**
 * PdJobImpl:
 *
 * The #PdJobImpl structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _PdJobImpl
{
	PdJobSkeleton	 parent_instance;
	PdDaemon	*daemon;

	gint		 document_fd;
	gchar		*document_filename;
	gchar		*document_mimetype;

	GList		*filterchain; /* of _PdJobProcess* */
	struct _PdJobProcess *backend;
	gint		 pending_job_state;

	gint		 fd_back[2];
	gint		 fd_side[2];

	/* Data ready to send to the backend */
	gchar		 buffer[1024];
	gsize		 buflen;
	gsize		 bufsent;

	GMutex		 lock;
};

struct _PdJobImplClass
{
	PdJobSkeletonClass parent_class;
};

enum
{
	PROP_0,
	PROP_DAEMON,
};

enum
{
	ADD_PRINTER_STATE_REASON,
	REMOVE_PRINTER_STATE_REASON,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void pd_job_iface_init (PdJobIface *iface);
static void pd_job_impl_add_state_reason (PdJobImpl *job,
					  const gchar *reason);
static void pd_job_impl_remove_state_reason (PdJobImpl *job,
					     const gchar *reason);
static void pd_job_impl_job_state_notify (PdJobImpl *job);
static void pd_job_impl_do_cancel_with_reason (PdJobImpl *job,
					       gint job_state,
					       const gchar *reason);
static gboolean pd_job_impl_message_io_cb (GIOChannel *channel,
					   GIOCondition condition,
					   gpointer data);
static gboolean pd_job_impl_data_io_cb (GIOChannel *channel,
					GIOCondition condition,
					gpointer data);

G_DEFINE_TYPE_WITH_CODE (PdJobImpl, pd_job_impl, PD_TYPE_JOB_SKELETON,
			 G_IMPLEMENT_INTERFACE (PD_TYPE_JOB, pd_job_iface_init));

/* ------------------------------------------------------------------ */

static void
pd_job_impl_finalize_jp (gpointer data)
{
	struct _PdJobProcess *jp = data;
	gint i;

	if (jp->started &&
	    !jp->finished) {
		job_debug (PD_JOB (jp->job),
			   "Sending KILL signal to backend PID %d",
			   jp->pid);
		kill (jp->pid, SIGKILL);
		g_spawn_close_pid (jp->pid);
	}

	g_free (jp->cmd);

	if (jp->process_watch_source) {
		g_source_remove (jp->process_watch_source);
	}
	for (i = 0; i < PD_FD_MAX; i++) {
		if (jp->io_source[i])
			g_source_remove (jp->io_source[i]);
		if (jp->channel[i])
			g_io_channel_unref (jp->channel[i]);
	}

	g_free (jp);
}

static void
pd_job_impl_finalize (GObject *object)
{
	PdJobImpl *job = PD_JOB_IMPL (object);

	job_debug (PD_JOB (job), "Finalize");
	/* note: we don't hold a reference to job->daemon */
	if (job->document_fd != -1)
		close (job->document_fd);
	if (job->document_filename) {
		g_unlink (job->document_filename);
		g_free (job->document_filename);
	}
	if (job->document_mimetype)
		g_free (job->document_mimetype);

	if (job->fd_back[STDIN_FILENO] != -1)
		close (job->fd_back[STDIN_FILENO]);
	if (job->fd_back[STDOUT_FILENO] != -1)
		close (job->fd_back[STDOUT_FILENO]);
	if (job->fd_side[0] != -1)
		close (job->fd_side[0]);
	if (job->fd_side[1] != -1)
		close (job->fd_side[1]);

	/* Shut down filter chain */
	g_list_free_full (job->filterchain,
			  pd_job_impl_finalize_jp);

	/* Shut down backend */
	pd_job_impl_finalize_jp (job->backend);

	g_signal_handlers_disconnect_by_func (job,
					      pd_job_impl_job_state_notify,
					      job);

	g_mutex_clear (&job->lock);
	G_OBJECT_CLASS (pd_job_impl_parent_class)->finalize (object);
}

static void
pd_job_impl_get_property (GObject *object,
			  guint prop_id,
			  GValue *value,
			  GParamSpec *pspec)
{
	PdJobImpl *job = PD_JOB_IMPL (object);

	switch (prop_id) {
	case PROP_DAEMON:
		g_value_set_object (value, job->daemon);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_job_impl_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec)
{
	PdJobImpl *job = PD_JOB_IMPL (object);

	switch (prop_id) {
	case PROP_DAEMON:
		g_assert (job->daemon == NULL);
		/* we don't take a reference to the daemon */
		job->daemon = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pd_job_impl_init_jp (PdJobImpl *job,
		     struct _PdJobProcess *jp)
{
	gint i;
	jp->job = job;
	jp->pid = -1;
	for (i = 0; i < PD_FD_MAX; i++) {
		jp->io_source[i] = 0;
		jp->child_fd[i] = -1;
		jp->parent_fd[i] = -1;
	}
}

static void
pd_job_impl_init (PdJobImpl *job)
{
	g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (job),
					     0);

	job->document_fd = -1;
	job->document_mimetype = NULL;

	job->backend = g_malloc0 (sizeof (struct _PdJobProcess));
	pd_job_impl_init_jp (job, job->backend);

	job->fd_back[0] = job->fd_back[1] = -1;
	job->fd_side[0] = job->fd_side[1] = -1;

	g_mutex_init (&job->lock);

	pd_job_set_state (PD_JOB (job), PD_JOB_STATE_PENDING_HELD);
	gchar *incoming[] = { g_strdup ("job-incoming"), NULL };
	pd_job_set_state_reasons (PD_JOB (job),
				  (const gchar *const *) incoming);
	g_free (incoming[0]);
}

static void
pd_job_impl_constructed (GObject *object)
{
	PdJobImpl *job = PD_JOB_IMPL (object);

	/* Watch our own state so we can update job state reasons. */
	g_signal_connect (job,
			  "notify::state",
			  G_CALLBACK (pd_job_impl_job_state_notify),
			  job);
}

static void
pd_job_impl_class_init (PdJobImplClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = pd_job_impl_finalize;
	gobject_class->constructed = pd_job_impl_constructed;
	gobject_class->set_property = pd_job_impl_set_property;
	gobject_class->get_property = pd_job_impl_get_property;

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
	 * PdJobImpl::add-printer-state-reason
	 *
	 * This signal is emitted by the #PdJob when a filter chain
	 * indicates that a printer state reason should be added.
	 */
	signals[ADD_PRINTER_STATE_REASON] = g_signal_new ("add-printer-state-reason",
							  G_OBJECT_CLASS_TYPE (klass),
							  G_SIGNAL_RUN_LAST,
							  0, /* G_STRUCT_OFFSET */
							  NULL, /* accu */
							  NULL, /* accu data */
							  g_cclosure_marshal_generic,
							  G_TYPE_NONE,
							  1,
							  G_TYPE_STRING);

	/**
	 * PdJobImpl::remove-printer-state-reason
	 *
	 * This signal is emitted by the #PdJob when a filter chain
	 * indicates that a printer state reason should be removed.
	 */
	signals[REMOVE_PRINTER_STATE_REASON] = g_signal_new ("remove-printer-state-reason",
							     G_OBJECT_CLASS_TYPE (klass),
							     G_SIGNAL_RUN_LAST,
							     0, /* G_STRUCT_OFFSET */
							     NULL, /* accu */
							     NULL, /* accu data */
							     g_cclosure_marshal_generic,
							     G_TYPE_NONE,
							     1,
							     G_TYPE_STRING);
}

/**
 * pd_job_impl_get_daemon:
 * @job: A #PdJobImpl.
 *
 * Gets the daemon used by @job.
 *
 * Returns: A #PdDaemon. Do not free, the object is owned by @job.
 */
PdDaemon *
pd_job_impl_get_daemon (PdJobImpl *job)
{
	g_return_val_if_fail (PD_IS_JOB_IMPL (job), NULL);
	return job->daemon;
}

/**
 * pd_job_impl_get_printer:
 * @job: A #PdJobImpl.
 *
 * Gets the printer the @job is for.
 *
 * Returns: (transfer full): A #PdPrinter. The returned value should
 * be freed with g_object_unref().
 */
static PdPrinter *
pd_job_impl_get_printer (PdJobImpl *job)
{
	const gchar *printer_path;
	PdEngine *engine;
	PdPrinter *printer;

	printer_path = pd_job_get_printer (PD_JOB (job));
	engine = pd_daemon_get_engine (job->daemon);
	printer = pd_engine_get_printer_by_path (engine, printer_path);
	return printer;
}

static gboolean
state_reason_is_set (PdJobImpl *job,
		     const gchar *reason)
{
	const gchar *const *strv;
	const gchar *const *r;
	strv = pd_job_get_state_reasons (PD_JOB (job));
	for (r = strv; *r != NULL; r++)
		if (!strcmp (*r, reason))
			return TRUE;

	return FALSE;
}

static void
pd_job_impl_add_state_reason (PdJobImpl *job,
			      const gchar *reason)
{
	const gchar *const *reasons;
	gchar **strv;

	job_debug (PD_JOB (job), "state-reasons += %s", reason);

	reasons = pd_job_get_state_reasons (PD_JOB (job));
	strv = add_or_remove_state_reason (reasons, '+', reason);
	pd_job_set_state_reasons (PD_JOB (job),
				  (const gchar *const *) strv);
	g_strfreev (strv);
}

static void
pd_job_impl_remove_state_reason (PdJobImpl *job,
				 const gchar *reason)
{
	const gchar *const *reasons;
	gchar **strv;

	job_debug (PD_JOB (job), "state-reasons -= %s", reason);

	reasons = pd_job_get_state_reasons (PD_JOB (job));
	strv = add_or_remove_state_reason (reasons, '-', reason);
	pd_job_set_state_reasons (PD_JOB (job),
				  (const gchar *const *) strv);
	g_strfreev (strv);
}

static gboolean
pd_job_impl_check_job_transforming (PdJobImpl *job)
{
	gboolean job_transforming = TRUE;
	GList *filter;
	struct _PdJobProcess *jp;

	for (filter = g_list_first (job->filterchain);
	     filter;
	     filter = g_list_next (filter)) {
		jp = filter->data;

		if (jp->started &&
		    !jp->finished) {
			/* There is still a filter chain
			   process running. */
			job_debug (PD_JOB (job), "PID %u (%s) still running",
				   jp->pid, jp->what);
			break;
		}
	}

	job_transforming = (filter != NULL);
	if (!job_transforming)
		pd_job_impl_remove_state_reason (job,
						 "job-transforming");

	return job_transforming;
}

static void
pd_job_impl_process_watch_cb (GPid pid,
			      gint status,
			      gpointer user_data)
{
	struct _PdJobProcess *jp = (struct _PdJobProcess *) user_data;
	PdJobImpl *job = PD_JOB_IMPL (jp->job);

	g_mutex_lock (&job->lock);
	g_object_freeze_notify (G_OBJECT (job));
	g_spawn_close_pid (pid);
	jp->finished = TRUE;
	jp->process_watch_source = 0;
	job_debug (PD_JOB (jp->job),
		   "PID %d (%s) finished with status %d",
		   pid, jp->what, WEXITSTATUS (status));

	/* Close its input file descriptors */
	if (jp->io_source[STDIN_FILENO]) {
		g_source_remove (jp->io_source[STDIN_FILENO]);
		jp->io_source[STDIN_FILENO] = 0;
	}

	if (jp->channel[STDIN_FILENO]) {
		g_io_channel_unref (jp->channel[STDIN_FILENO]);
		jp->channel[STDIN_FILENO] = NULL;
	}

	if (jp != job->backend)
		/* Filter. */
		pd_job_impl_check_job_transforming (job);
	else
		/* Backend. */
		pd_job_impl_remove_state_reason (job, "job-outgoing");

	/* Adjust job state. */
	if (WEXITSTATUS (status) != 0) {
		job_debug (PD_JOB (jp->job),
			   "%s failed: aborting job", jp->what);

		job->pending_job_state = PD_JOB_STATE_ABORTED;
	}

	if (job->backend->started &&
	    job->backend->finished) {
		GList *filter;
		struct _PdJobProcess *eachjp;
		for (filter = g_list_first (job->filterchain);
		     filter;
		     filter = g_list_next (filter)) {
			eachjp = filter->data;
			if (eachjp->started &&
			    !eachjp->finished)
				break;
		}

		if (!filter) {
			/* No more processes running. */
			job_debug (PD_JOB (job), "Set job state to %s",
				   pd_job_state_as_string (job->pending_job_state));
			pd_job_set_state (PD_JOB (job),
					  job->pending_job_state);
		}
	}

	g_mutex_unlock (&job->lock);
	g_object_thaw_notify (G_OBJECT (job));
}

static void
pd_job_impl_parse_stderr (PdJobImpl *job,
			  const gchar *line)
{
	if (!strncmp (line, "STATE:", 6)) {
		const gchar *token = line + 6;
		const gchar *end;
		gchar *reason;
		gchar add_or_remove;

		/* Skip whitespace */
		token += strspn (token, " \t");
		add_or_remove = token[0];
		if (add_or_remove == '+' ||
		    add_or_remove == '-') {
			token++;
			while (token[0] != '\0') {
				end = token + strcspn (token, ", \t\n");
				reason = g_strndup (token, end - token);

				/* Process this reason */
				if (add_or_remove == '+')
					g_signal_emit (PD_JOB (job),
						       signals[ADD_PRINTER_STATE_REASON],
						       0,
						       reason);
				else
					g_signal_emit (PD_JOB (job),
						       signals[REMOVE_PRINTER_STATE_REASON],
						       0,
						       reason);

				g_free (reason);

				/* Next reason */
				token = end + strspn (end, ", \t\n");
			}
		}
	}
}

static gboolean
pd_job_impl_data_io_cb (GIOChannel *channel,
			GIOCondition condition,
			gpointer data)
{
	struct _PdJobProcess *thisjp = (struct _PdJobProcess *) data;
	PdJobImpl *job = PD_JOB_IMPL (thisjp->job);
	gboolean keep_source = TRUE;
	GError *error = NULL;
	GIOStatus status;
	gsize got, wrote;
	struct _PdJobProcess *nextjp;
	GIOChannel *nextchannel;
	gint thisfd;
	gint nextfd;

	g_mutex_lock (&job->lock);
	g_object_freeze_notify (G_OBJECT (job));
	if (condition & (G_IO_IN | G_IO_HUP)) {
		g_assert (thisjp != job->backend);
		thisfd = STDOUT_FILENO;
		nextjp = job->backend;
		nextfd = STDIN_FILENO;

		/* Read some data */
		status = g_io_channel_read_chars (channel,
						  job->buffer,
						  sizeof (job->buffer),
						  &got,
						  &error);
		switch (status) {
		case G_IO_STATUS_NORMAL:
			job->buflen = got;
			job->bufsent = 0;
			job_debug (PD_JOB (job), "Read %zu bytes from %s",
				   got,
				   thisjp->what);

			nextchannel = nextjp->channel[nextfd];
			if (nextchannel)
				nextjp->io_source[nextfd] =
					g_io_add_watch (nextchannel,
							G_IO_OUT,
							pd_job_impl_data_io_cb,
							nextjp);

			keep_source = FALSE;
			break;

		case G_IO_STATUS_EOF:
			job_debug (PD_JOB (job), "Output from %s closed",
				   thisjp->what);

			g_io_channel_unref (channel);
			keep_source = FALSE;
			break;

		case G_IO_STATUS_ERROR:
			job_warning (PD_JOB (job), "read() from %s failed: %s",
				     thisjp->what,
				     error->message);
			g_error_free (error);
			pd_job_impl_do_cancel_with_reason (job,
							   PD_JOB_STATE_ABORTED,
							   "job-aborted-by-system");
			g_io_channel_unref (channel);
			keep_source = FALSE;
			break;

		case G_IO_STATUS_AGAIN:
			/* End of data for now */
			break;
		}

		if (!keep_source) {
			thisjp->io_source[thisfd] = 0;

			if (status == G_IO_STATUS_EOF) {
				thisjp->channel[thisfd] = NULL;

				nextchannel = nextjp->channel[nextfd];
				if (nextchannel)
					nextjp->io_source[nextfd] =
						g_io_add_watch (nextchannel,
								G_IO_OUT,
								pd_job_impl_data_io_cb,
								nextjp);

			}
		}
	} else {
		g_assert (condition == G_IO_OUT);
		thisfd = STDIN_FILENO;
		nextfd = STDOUT_FILENO;
		if (thisjp == job->backend)
			nextjp = g_list_last (job->filterchain)->data;
		else
			nextjp = job->backend;

		if (job->buflen - job->bufsent == 0) {
			/* End of input */
			g_io_channel_shutdown (channel,
					       TRUE,
					       NULL);
			keep_source = FALSE;
			thisjp->io_source[thisfd] = 0;
			job_debug (PD_JOB (job), "Closing input to %s",
				   thisjp->what);
			thisjp->channel[thisfd] = NULL;
			g_io_channel_unref (channel);
			pd_job_impl_check_job_transforming (job);
			goto out;
		}

		status = g_io_channel_write_chars (channel,
						   job->buffer + job->bufsent,
						   job->buflen - job->bufsent,
						   &wrote,
						   &error);
		switch (status) {
		case G_IO_STATUS_ERROR:
			job_warning (PD_JOB (job), "Error writing to %s: %s",
				     thisjp->what, error->message);
			g_error_free (error);
			pd_job_impl_do_cancel_with_reason (job,
							   PD_JOB_STATE_ABORTED,
							   "job-aborted-by-system");
			goto out;
		case G_IO_STATUS_EOF:
			job_debug (PD_JOB (job), "EOF on write?");
			pd_job_impl_do_cancel_with_reason (job,
							   PD_JOB_STATE_ABORTED,
							   "job-canceled-by-system");
			break;
		case G_IO_STATUS_AGAIN:
			job_debug (PD_JOB (job),
				   "Resource temporarily unavailable");
			break;
		case G_IO_STATUS_NORMAL:
			job_debug (PD_JOB (job), "Wrote %zu bytes to %s",
				   wrote, thisjp->what);
			break;
		}

		job->bufsent += wrote;

		if (job->buflen - job->bufsent == 0) {
			/* Need to read more data now */
			keep_source = FALSE;
			thisjp->io_source[thisfd] = 0;

			nextchannel = nextjp->channel[nextfd];
			if (nextchannel)
				nextjp->io_source[nextfd] = g_io_add_watch (nextchannel,
									    G_IO_IN |
									    G_IO_HUP,
									    pd_job_impl_data_io_cb,
									    nextjp);
		}

	}

 out:
	g_mutex_unlock (&job->lock);
	g_object_thaw_notify (G_OBJECT (job));
	return keep_source;
}

static gboolean
pd_job_impl_message_io_cb (GIOChannel *channel,
			   GIOCondition condition,
			   gpointer data)
{
	struct _PdJobProcess *jp = (struct _PdJobProcess *) data;
	PdJobImpl *job = PD_JOB_IMPL (jp->job);
	gboolean keep_source = TRUE;
	GError *error = NULL;
	GIOStatus status;
	gchar *line = NULL;
	gsize got;
	gint thisfd;

	g_assert (condition & (G_IO_IN | G_IO_HUP));

	g_mutex_lock (&job->lock);
	g_object_freeze_notify (G_OBJECT (job));

	if (channel == jp->channel[STDERR_FILENO])
		thisfd = STDERR_FILENO;
	else {
		g_assert (channel == jp->channel[STDOUT_FILENO]);
		thisfd = STDOUT_FILENO;
	}

	while ((status = g_io_channel_read_line (channel,
						 &line,
						 &got,
						 NULL,
						 &error)) ==
	       G_IO_STATUS_NORMAL) {
		if (thisfd == STDERR_FILENO) {
			job_debug (PD_JOB (job), "%s: %s",
				   jp->what, g_strchomp (line));
			pd_job_impl_parse_stderr (job, line);
		}
		else
			job_debug (PD_JOB (job), "backend(stdout): %s",
				   g_strchomp (line));

		g_free (line);
	}

	switch (status) {
	case G_IO_STATUS_ERROR:
		job_warning (PD_JOB (job), "Error reading from channel: %s",
			     error->message);
		g_error_free (error);
		g_io_channel_unref (channel);
		keep_source = FALSE;
		pd_job_impl_add_state_reason (job, "job-canceled-by-system");
		pd_job_set_state (PD_JOB (job), PD_JOB_STATE_CANCELED);
		goto out;
	case G_IO_STATUS_EOF:
		g_io_channel_unref (channel);
		job_debug (PD_JOB (job), "%s fd %d closed", jp->what, thisfd);
		keep_source = FALSE;
		break;
	case G_IO_STATUS_AGAIN:
		/* End of messages for now */
		break;
	case G_IO_STATUS_NORMAL:
		g_assert_not_reached ();
		break;
	}

	if (!keep_source) {
		jp->channel[thisfd] = NULL;
		jp->io_source[thisfd] = 0;
	}

 out:
	g_mutex_unlock (&job->lock);
	g_object_thaw_notify (G_OBJECT (job));
	if (line)
		g_free (line);
	return keep_source;
}

static gboolean
pd_job_impl_create_pipe_for (PdJobImpl *job,
			     struct _PdJobProcess *jp,
			     gint fd,
			     gboolean out)
{
	GError *error = NULL;
	gint pipe_fd[2];

	if (!g_unix_open_pipe (pipe_fd, 0, &error)) {
		job_warning (PD_JOB (job), "Failed to create pipe: %s",
			     error->message);
		g_error_free (error);
		return FALSE;
	}

	jp->child_fd[fd] = pipe_fd[out ? STDOUT_FILENO : STDIN_FILENO];
	jp->parent_fd[fd] = pipe_fd[out ? STDIN_FILENO : STDOUT_FILENO];
	return TRUE;
}

static void
pd_job_impl_process_setup (gpointer user_data)
{
	struct _PdJobProcess *jp = (struct _PdJobProcess *) user_data;
	gint i;

	/* Close the parent's ends of any pipes */
	for (i = 0; i < PD_FD_MAX; i++)
		if (jp->parent_fd[i] != -1)
			close (jp->parent_fd[i]);

	/* Watch out for clashes */
	for (i = 0; i < PD_FD_MAX; i++)
		if (jp->child_fd[i] < PD_FD_MAX &&
		    jp->child_fd[i] != i) {
			gint newfd = PD_FD_MAX + i;
			dup2 (jp->child_fd[i], newfd);
			close (jp->child_fd[i]);
			jp->child_fd[i] = newfd;
		}

	/* Set our standard file descriptors to existing ones */
	for (i = 0; i < PD_FD_MAX; i++)
		if (jp->child_fd[i] != -1 &&
		    jp->child_fd[i] != i) {
			if (dup2 (jp->child_fd[i], i) == -1 &&
			    i > STDERR_FILENO)
				fprintf (stderr,
					 "ERROR: dup2(%d,%d) failed with %s\n",
					 jp->child_fd[i], i, strerror (errno));
			if (close (jp->child_fd[i]) && i > STDERR_FILENO)
				fprintf (stderr,
					 "ERROR: close(%d) failed with %s\n",
					 jp->child_fd[i], strerror (errno));
		}
}

static GVariant *
get_attribute_value (PdJob *job,
		     const gchar *key)
{
	GVariant *attributes;
	GVariantIter iter;
	gchar *dkey;
	GVariant *dvalue;

	attributes = pd_job_get_attributes (PD_JOB (job));
	g_variant_iter_init (&iter, attributes);
	while (g_variant_iter_loop (&iter, "{sv}", &dkey, &dvalue))
		if (!strcmp (dkey, key)) {
			g_free (dkey);
			return dvalue;
		}

	return NULL;
}

static gboolean
run_file_output (gchar **argv,
		 gchar **envp,
		 GSpawnChildSetupFunc child_setup,
		 gpointer user_data,
		 GPid *child_pid,
		 GError **error)
{
	pid_t pid = fork ();

	if (pid == -1) {
		*error = g_error_new (PD_ERROR,
				      PD_ERROR_FAILED,
				      "fork: %s",
				      strerror (errno));
		return FALSE;
	} else if (pid == 0) {
		gchar **env;
		gchar *uri = NULL;
		gchar *options;
		GInputStream *input;
		GFile *file;
		GFileIOStream *fileio = NULL;
		GOutputStream *output = NULL;
		GError *error = NULL;
		guint64 delay = 0;
		gint ret = 0;

		/* Child */
		(*child_setup) (user_data);

		/* Write output to file */
		for (env = envp; *env; env++)
			if (!strncmp (*env, "DEVICE_URI=", 11)) {
				uri = g_strdup (*env + 11);
				break;
			}

		if (!uri) {
			fprintf (stderr,
				 "ERROR: no DEVICE_URI in environment\n");
			exit (1);
		}

		/* Process any options (for testing) */
		options = strchr (uri, '?');
		if (options) {
			char *next_option;
			char *option;
			*options++ = '\0';
			for (option = options;
			     option && *option;
			     option = next_option) {
				char *opt;
				char *value;
				char *end;

				next_option = strchr (option, '&');
				if (next_option) {
					end = next_option;
					*next_option++ = '\0';
				} else
					end = option + strlen (option);

				opt = g_uri_unescape_segment (option,
							      end,
							      NULL);
				if (!opt)
					continue;

				value = strchr (opt, '=');
				if (!value)
					value = "true";
				else
					*value++ = '\0';

				if (!g_strcmp0 (opt, "wait")) {
					delay = g_ascii_strtoull (value,
								  &end,
								  10);
					if (delay > G_MAXUINT)
						delay = G_MAXUINT;
				}

				g_free (opt);
			}
		}

		file = g_file_new_for_uri (uri);
		fileio = g_file_open_readwrite (file, NULL, &error);
		if (!fileio) {
			fprintf (stderr, "ERROR: %s\n", error->message);
			g_error_free (error);
			exit (1);
		}

		if (delay) {
			fprintf (stderr, "DEBUG: Sleeping for %us\n",
				 (unsigned int) delay);
			sleep ((unsigned int) delay);
		}

		output = g_io_stream_get_output_stream (G_IO_STREAM (fileio));
		input = g_unix_input_stream_new (STDIN_FILENO, FALSE);
		if (g_output_stream_splice (output,
					    input,
					    0,
					    NULL,
					    &error) == -1)
			ret = 1;

		if (ret == 0 && g_io_stream_close (G_IO_STREAM (fileio),
						   NULL,
						   &error) == -1)
			ret = 1;

		if (ret == 0 && !g_input_stream_close (input, NULL, &error))
			ret = 1;

		if (ret) {
			fprintf (stderr, "ERROR: %s\n", error->message);
			g_error_free (error);
		}

		if (fileio)
			g_io_stream_close (G_IO_STREAM (fileio), NULL, NULL);
		if (input)
			g_input_stream_close (input, NULL, NULL);

		g_object_unref (fileio);
		g_object_unref (file);
		g_free (uri);
		exit (ret);
	}

	/* Parent */
	*child_pid = pid;
	return TRUE;
}

static gboolean
run_cupsfilter (PdJobImpl *job,
		gchar **envp,
		GSpawnChildSetupFunc child_setup,
		gpointer user_data,
		GPid *child_pid,
		GError **error)
{
	PdPrinter *printer;
	const gchar *driver;
	const gchar *final_content_type;
	gboolean ret;
	char **argv;
	gchar **s;

	if ((printer = pd_job_impl_get_printer (PD_JOB_IMPL (job))) == NULL) {
		*error = g_error_new (PD_ERROR,
				      PD_ERROR_FAILED,
				      "no printer for job");
		return FALSE;
	}

	driver = pd_printer_get_driver (printer);
	if (driver == NULL) {
		*error = g_error_new (PD_ERROR,
				      PD_ERROR_FAILED,
				      "no driver for printer");
		g_object_unref (printer);
		return FALSE;
	}

	final_content_type = pd_printer_impl_get_final_content_type (PD_PRINTER_IMPL (printer));
	if (final_content_type == NULL) {
		*error = g_error_new (PD_ERROR,
				      PD_ERROR_FAILED,
				      "no final content type for printer");
		g_object_unref (printer);
		return FALSE;
	}

	g_object_unref (printer);

	argv = g_malloc (sizeof (char *) * 8);
	argv[0] = g_strdup ("/usr/sbin/cupsfilter");
	argv[1] = g_strdup ("-p");
	argv[2] = g_strdup (driver);
	argv[3] = g_strdup ("-i");
	argv[4] = g_strdup ("application/vnd.cups-pdf");
	argv[5] = g_strdup ("-m");
	argv[6] = g_strdup (final_content_type);
	argv[7] = NULL;

	for (s = argv + 1; *s; s++)
		job_debug (PD_JOB (job), " Arg: %s", *s);

	ret = g_spawn_async ("/" /* wd */,
			     argv,
			     envp,
			     G_SPAWN_DO_NOT_REAP_CHILD,
			     child_setup,
			     user_data,
			     child_pid,
			     error);

	g_strfreev (argv);
	return ret;
}

static gboolean
pd_job_impl_run_process (PdJobImpl *job,
			 struct _PdJobProcess *jp,
			 GError **error)
{
	gboolean ret = FALSE;
	guint job_id = pd_job_get_id (PD_JOB (job));
	gchar *username;
	GString *options = NULL;
	GVariant *attributes;
	GVariant *variant;
	GVariantIter iter;
	gchar *dkey;
	GVariant *dvalue;
	const gchar *uri;
	char **argv = NULL;
	char **envp = NULL;
	gchar **s;
	gint i;

	uri = pd_job_get_device_uri (PD_JOB (job));

	variant = get_attribute_value (PD_JOB (job),
				       "job-originating-user-name");
	if (variant) {
		username = g_variant_dup_string (variant, NULL);
		g_variant_unref (variant);
	} else
		username = g_strdup ("unknown");

	attributes = pd_job_get_attributes (PD_JOB (job));
	g_variant_iter_init (&iter, attributes);
	while (g_variant_iter_loop (&iter, "{sv}", &dkey, &dvalue)) {
		gchar *val;

		if (g_variant_is_of_type (dvalue, G_VARIANT_TYPE_STRING))
			val = g_variant_dup_string (dvalue, NULL);
		else
			val = g_variant_print (dvalue, FALSE);

		if (options == NULL) {
			options = g_string_new ("");
			g_string_printf (options, "%s=%s", dkey, val);
		} else
			g_string_append_printf (options, " %s=%s", dkey, val);

		g_free (val);
	}

	if (options == NULL)
		options = g_string_new ("");

	argv = g_malloc0 (sizeof (char *) * 8);
	argv[0] = g_strdup (jp->cmd);
	if (jp->type != FILTERCHAIN_CUPSFILTER) {
		/* URI */
		argv[1] = g_strdup (uri);
		/* Job ID */
		argv[2] = g_strdup_printf ("%u", job_id);
		/* User name */
		argv[3] = username;
		/* Job title */
		argv[4] = g_strdup (pd_job_get_name (PD_JOB (job)));
		/* Copies */
		argv[5] = g_strdup ("1");
		/* Options */
		argv[6] = options->str;
		g_string_free (options, FALSE);
		argv[7] = NULL;
	}

	envp = g_malloc0 (sizeof (char *) * 2);
	envp[0] = g_strdup_printf ("DEVICE_URI=%s", uri);
	envp[1] = NULL;

	job_debug (PD_JOB (job), "Executing %s", argv[0]);
	for (s = envp; *s; s++)
		job_debug (PD_JOB (job), " Env: %s", *s);
	if (jp->type != FILTERCHAIN_CUPSFILTER)
		for (s = argv + 1; *s; s++)
			job_debug (PD_JOB (job), " Arg: %s", *s);

	switch (jp->type) {
	case FILTERCHAIN_CMD:
		ret = g_spawn_async ("/" /* wd */,
				     argv,
				     envp,
				     G_SPAWN_DO_NOT_REAP_CHILD |
				     G_SPAWN_FILE_AND_ARGV_ZERO,
				     pd_job_impl_process_setup,
				     jp,
				     &jp->pid,
				     error);
		break;

	case FILTERCHAIN_FILE_OUTPUT:
		ret = run_file_output (argv,
				       envp,
				       pd_job_impl_process_setup,
				       jp,
				       &jp->pid,
				       error);
		break;

	case FILTERCHAIN_CUPSFILTER:
		ret = run_cupsfilter (job,
				      envp,
				      pd_job_impl_process_setup,
				      jp,
				      &jp->pid,
				      error);
		break;

	default:
		g_assert_not_reached ();
	}

	if (!ret)
		goto out;

	jp->started = TRUE;

	/* Close the child's end of the in/out/err pipes now they've started */
	for (i = 0; i <= STDERR_FILENO; i++)
		if (jp->child_fd[i] != -1)
			close (jp->child_fd[i]);

	/* Set up IO channels */
	for (i = 0; i < PD_FD_MAX; i++) {
		GIOChannel *channel;

		if (jp->parent_fd[i] == -1)
			continue;

		channel = g_io_channel_unix_new (jp->parent_fd[i]);
		g_io_channel_set_flags (channel,
					G_IO_FLAG_NONBLOCK,
					NULL);
		g_io_channel_set_close_on_unref (channel,
						 TRUE);

		/* Set the data channels up for binary data. */
		if ((jp == job->backend && i < 1) ||
		    (jp != job->backend && i < 2))
			g_io_channel_set_encoding (channel, NULL, NULL);

		jp->channel[i] = channel;
	}

 out:
	if (argv)
		g_strfreev (argv);

	g_strfreev (envp);
	return ret;
}

/**
 * pd_job_impl_start_processing:
 * @job: A #PdJobImpl
 *
 * The job state is processing and the printer is ready so start
 * processing the job.
 *
 * This must be called while holding the @job's lock.
 */
static void
pd_job_impl_start_processing (PdJobImpl *job)
{
	GError *error = NULL;
	const gchar *uri;
	PdPrinter *printer = NULL;
	char *scheme = NULL;
	GIOChannel *channel;
	struct _PdJobProcess *jp;
	gint document_fd = -1;
	GList *filter, *next_filter;
	const gchar *driver;

	if (job->filterchain) {
		job_warning (PD_JOB (job), "Already processing!");
		goto out;
	}

	job_debug (PD_JOB (job), "Starting to process job");

	/* Get the device URI to use from the Printer */
	if ((printer = pd_job_impl_get_printer (job)) == NULL) {
		job_warning (PD_JOB (job), "No printer for job");
		goto fail;
	}

	uri = pd_printer_impl_get_uri (PD_PRINTER_IMPL (printer));
	job_debug (PD_JOB (job), "Using device URI %s", uri);
	pd_job_set_device_uri (PD_JOB (job), uri);
	scheme = g_uri_parse_scheme (uri);

	/* Open document */
	document_fd = open (job->document_filename, O_RDONLY);
	if (document_fd == -1) {
		job_warning (PD_JOB (job), "Failed to open spool file %s: %s",
			     job->document_filename, g_strerror (errno));
		goto fail;
	}

	/* Set up pipeline */
	if (!g_strcmp0 (scheme, "file")) {
		job->backend->cmd = g_strdup ("(file output)");
		job->backend->type = FILTERCHAIN_FILE_OUTPUT;
	} else {
		job->backend->type = FILTERCHAIN_CMD;
		job->backend->cmd = g_strdup_printf ("/usr/lib/cups/backend/%s",
						     scheme);
	}

	job->backend->what = "backend";

	/* Set up the arranger */
	jp = g_malloc0 (sizeof (struct _PdJobProcess));
	pd_job_impl_init_jp (job, jp);
	jp->type = FILTERCHAIN_CMD;
	jp->cmd = g_strdup ("/usr/lib/cups/filter/pdftopdf");
	jp->what = "arranger";

	/* Add the arranger to the filter chain */
	job->filterchain = g_list_insert (job->filterchain, jp, -1);

	driver = pd_printer_get_driver (printer);
	if (driver && *driver) {
		/* Set up the transformer */
		jp = g_malloc0 (sizeof (struct _PdJobProcess));
		pd_job_impl_init_jp (job, jp);
		jp->type = FILTERCHAIN_CUPSFILTER;
		jp->cmd = g_strdup ("(cupsfilter)");
		jp->what = "transformer";

		/* Add the transformer to the filter chain */
		job->filterchain = g_list_insert (job->filterchain, jp, -1);
	}

	/* Set up a pipe to write to the backend's stdin */
	if (!pd_job_impl_create_pipe_for (job, job->backend,
					  STDIN_FILENO, FALSE))
		goto fail_setup;

	/* Set up a pipe to read the backend's stdout */
	if (!pd_job_impl_create_pipe_for (job, job->backend,
					  STDOUT_FILENO, TRUE))
		goto fail_setup;

	/* Set up a pipe to read the backend's stderr */
	if (!pd_job_impl_create_pipe_for (job, job->backend,
					  STDERR_FILENO, TRUE))
		goto fail_setup;

	/* Connect the back-channel between backend and filter-chain */
	if (!g_unix_open_pipe (job->fd_back, 0, &error)) {
		job_warning (PD_JOB (job), "Failed to create pipe: %s",
			     error->message);
		g_error_free (error);
		goto fail;
	}

	job->backend->child_fd[PD_FD_BACK] = job->fd_back[STDOUT_FILENO];
	for (filter = g_list_first (job->filterchain);
	     filter;
	     filter = g_list_next (filter)) {
		jp = filter->data;
		jp->child_fd[PD_FD_BACK] = job->fd_back[STDIN_FILENO];
	}

	/* Connect the side-channel between filter-chain and backend */
	if (socketpair (AF_LOCAL, SOCK_STREAM, 0, job->fd_side) != 0) {
		job_warning (PD_JOB (job), "Failed to create socket pair: %s",
			     g_strerror (errno));
		goto fail;
	}

	job->backend->child_fd[PD_FD_SIDE] = job->fd_side[1];
	for (filter = g_list_first (job->filterchain);
	     filter;
	     filter = g_list_next (filter)) {
		jp = filter->data;
		jp->child_fd[PD_FD_SIDE] = job->fd_side[0];
	}

	/* Set up pipes to read stderr from the filters */
	for (filter = g_list_first (job->filterchain);
	     filter;
	     filter = g_list_next (filter)) {
		jp = filter->data;
		if (!pd_job_impl_create_pipe_for (job, jp,
						  STDERR_FILENO, TRUE)) {
		fail_setup:
			g_free (jp);
			close (document_fd);
			goto fail;
		}
	}

	/* Set up pipes between the filters in the filter chain */
	filter = g_list_first (job->filterchain);
	for (next_filter = g_list_next (filter);
	     next_filter;
	     next_filter = g_list_next (next_filter),
		     filter = next_filter) {
		gint pipe_fd[2];
		struct _PdJobProcess *nextjp;

		jp = filter->data;
		nextjp = next_filter->data;
		if (!g_unix_open_pipe (pipe_fd, 0, &error)) {
			job_warning (PD_JOB (job), "Failed to create pipe: %s",
				     error->message);
			g_error_free (error);
			goto fail_setup;
		}

		jp->child_fd[STDOUT_FILENO] = pipe_fd[STDOUT_FILENO];
		nextjp->child_fd[STDIN_FILENO] = pipe_fd[STDIN_FILENO];
	}

	/* Connect the document fd to the input of the first filter in
	 * the chain. */
	filter = g_list_first (job->filterchain);
	jp = filter->data;
	jp->child_fd[STDIN_FILENO] = document_fd;

	/* Set up a pipe for the output of last filter in the filter chain */
	filter = g_list_last (job->filterchain);
	jp = filter->data;
	if (!pd_job_impl_create_pipe_for (job, jp, STDOUT_FILENO, TRUE))
		goto fail_setup;
	
	/* When the filters have finished, the state will still be
	   processing (the backend hasn't run yet). */
	job->pending_job_state = PD_JOB_STATE_PROCESSING;

	/* Run filter chain */
	pd_job_impl_add_state_reason (job, "job-transforming");
	for (filter = g_list_first (job->filterchain);
	     filter;
	     filter = g_list_next (filter)) {
		jp = filter->data;
		if (!pd_job_impl_run_process (job, jp, &error)) {
			job_warning (PD_JOB (job), "Running %s: %s",
				     jp->what, error->message);
			g_error_free (error);
			goto fail;
		}
	}

	close (job->fd_back[STDIN_FILENO]);
	close (job->fd_side[0]);
	job->fd_back[STDIN_FILENO] = job->fd_side[0] = -1;

	/* Start watching output */
	for (filter = g_list_first (job->filterchain);
	     filter;
	     filter = g_list_next (filter)) {
		jp = filter->data;
		channel = jp->channel[STDERR_FILENO];
		jp->io_source[STDERR_FILENO] =
			g_io_add_watch (channel,
					G_IO_IN |
					G_IO_HUP,
					pd_job_impl_message_io_cb,
					jp);
	}

	/* Watch for processes exiting */
	for (filter = g_list_first (job->filterchain);
	     filter;
	     filter = g_list_next (filter)) {
		jp = filter->data;
		jp->process_watch_source =
			g_child_watch_add (jp->pid,
					   pd_job_impl_process_watch_cb,
					   jp);
	}

	/* Don't add an IO watch to the end of the chain yet. Let it
	 * buffer until the backend has started. */

 out:
	if (printer)
		g_object_unref (printer);
	g_free (scheme);
	return;

 fail:
	pd_job_set_state (PD_JOB (job),
			  PD_JOB_STATE_ABORTED);
	goto out;
}

/**
 * pd_job_impl_start_sending:
 * @job: A #PdJobImpl
 *
 * Start sending the job to the printer.
 */
void
pd_job_impl_start_sending (PdJobImpl *job)
{
	GError *error = NULL;
	GIOChannel *channel;
	struct _PdJobProcess *jp;

	g_mutex_lock (&job->lock);
	g_object_freeze_notify (G_OBJECT (job));

	pd_job_impl_add_state_reason (job, "job-outgoing");

	if (!job->filterchain) {
		pd_job_impl_start_processing (job);
		if (pd_job_get_state (PD_JOB (job)) ==
		    PD_JOB_STATE_ABORTED)
			goto out;
	}

	/* When the backend finishes the job state will be 'completed'. */
	job->pending_job_state = PD_JOB_STATE_COMPLETED;

	/* Run backend */
	if (!pd_job_impl_run_process (job, job->backend, &error)) {
		job_warning (PD_JOB (job), "Running backend: %s",
			     error->message);
		g_error_free (error);

		/* Update job state */
		pd_job_set_state (PD_JOB (job),
				  PD_JOB_STATE_ABORTED);
		goto out;
	}

	/* Close the backend's back/side fds */
	close (job->fd_back[STDOUT_FILENO]);
	close (job->fd_side[1]);
	job->fd_back[STDOUT_FILENO] = job->fd_side[1] = -1;

	job->backend->io_source[STDIN_FILENO] = 0;

	/* Watch for messages from the backend */
	channel = job->backend->channel[STDOUT_FILENO];
	job->backend->io_source[STDOUT_FILENO] =
		g_io_add_watch (channel,
				G_IO_IN |
				G_IO_HUP,
				pd_job_impl_message_io_cb,
				job->backend);

	channel = job->backend->channel[STDERR_FILENO];
	job->backend->io_source[STDERR_FILENO] =
		g_io_add_watch (channel,
				G_IO_IN |
				G_IO_HUP,
				pd_job_impl_message_io_cb,
				job->backend);

	/* Watch for the backend exiting */
	jp = job->backend;
	jp->process_watch_source =
		g_child_watch_add (jp->pid,
				   pd_job_impl_process_watch_cb,
				   jp);

	/* Now there's somewhere to send the data to, add an IO watch
	 * to the stdout of the last filter in the chain. */
	jp = g_list_last (job->filterchain)->data;
	channel = jp->channel[STDOUT_FILENO];
	jp->io_source[STDOUT_FILENO] =
		g_io_add_watch (channel,
				G_IO_IN |
				G_IO_HUP,
				pd_job_impl_data_io_cb,
				jp);

 out:
	g_mutex_unlock (&job->lock);
	g_object_thaw_notify (G_OBJECT (job));
	return;
}

static void
pd_job_impl_job_state_notify (PdJobImpl *job)
{
	/* This function watches changes to the job state and
	   starts/stop things accordingly. */

	switch (pd_job_get_state (PD_JOB (job))) {
	case PD_JOB_STATE_CANCELED:
	case PD_JOB_STATE_ABORTED:
	case PD_JOB_STATE_COMPLETED:
		/* Job is now terminated. */
		pd_job_impl_remove_state_reason (job, "job-incoming");
		pd_job_impl_remove_state_reason (job,
						 "processing-to-stop-point");

		g_signal_handlers_disconnect_by_func (job,
						      pd_job_impl_job_state_notify,
						      job);
	default:
		;
	}
}

/**
 * pd_job_impl_set_attribute:
 * @job: A #PdJobImpl
 * @name: Attribute name
 * @value: Attribute value
 *
 * Set a job attribute or update an existing job attribute.
 */
void
pd_job_impl_set_attribute (PdJobImpl *job,
			   const gchar *name,
			   GVariant *value)
{
	GVariant *attributes;
	GVariantBuilder builder;
	GVariantIter viter;
	GHashTable *ht;
	GHashTableIter htiter;
	gchar *dkey;
	GVariant *dvalue;

	g_mutex_lock (&job->lock);
	g_object_freeze_notify (G_OBJECT (job));

	/* Read the current value of the 'attributes' property */
	attributes = pd_job_get_attributes (PD_JOB (job));

	/* Convert to a GHashTable */
	ht = g_hash_table_new_full (g_str_hash,
				    g_str_equal,
				    g_free,
				    (GDestroyNotify) g_variant_unref);

	g_variant_iter_init (&viter, attributes);
	while (g_variant_iter_loop (&viter, "{sv}", &dkey, &dvalue))
		g_hash_table_insert (ht,
				     g_strdup (dkey),
				     g_variant_ref_sink (dvalue));

	/* Set the attribute value */
	g_hash_table_insert (ht,
			     g_strdup (name),
			     g_variant_ref_sink (value));

	/* Convert back to a GVariant */
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
	g_hash_table_iter_init (&htiter, ht);
	while (g_hash_table_iter_next (&htiter,
				       (gpointer *) &dkey,
				       (gpointer *) &dvalue))
		g_variant_builder_add (&builder, "{sv}", dkey, dvalue);

	/* Write it back */
	pd_job_set_attributes (PD_JOB (job),
			       g_variant_builder_end (&builder));

	g_mutex_unlock (&job->lock);
	g_object_thaw_notify (G_OBJECT (job));
	g_hash_table_unref (ht);
}

/* ------------------------------------------------------------------ */

/* runs in main thread */
static gboolean
pd_job_impl_add_document (PdJob *_job,
			  GDBusMethodInvocation *invocation,
			  GUnixFDList *fd_list,
			  GVariant *options,
			  GVariant *file_descriptor)
{
	PdJobImpl *job = PD_JOB_IMPL (_job);
	gint32 fd_handle;
	GError *error = NULL;
	GVariant *attr_user;
	gchar *requesting_user = NULL;
	const gchar *originating_user = NULL;

	/* Check if the user is authorized to add a document */
	if (!pd_daemon_check_authorization_sync (job->daemon,
						 "org.freedesktop.printerd.job-add",
						 options,
						 N_("Authentication is required to add a job"),
						 invocation))
		return TRUE;

	/* Get the requesting user before holding the lock as it uses
	 * sync D-Bus calls */
	requesting_user = pd_get_unix_user (invocation);

	g_mutex_lock (&job->lock);
	g_object_freeze_notify (G_OBJECT (job));

	/* Check if this user owns the job */
	attr_user = get_attribute_value (PD_JOB (job),
					 "job-originating-user-name");
	if (attr_user) {
		originating_user = g_variant_get_string (attr_user, NULL);
		g_variant_unref (attr_user);
	}
	if (g_strcmp0 (originating_user, requesting_user)) {
		job_debug (PD_JOB (job), "AddDocument: denied "
			   "[originating user: %s; requesting user: %s]",
			   originating_user, requesting_user);
		g_dbus_method_invocation_return_error (invocation,
						       PD_ERROR,
						       PD_ERROR_FAILED,
						       N_("Not job owner"));
		goto out;
	}

	if (job->document_fd != -1 ||
	    job->document_filename != NULL) {
		job_debug (PD_JOB (job), "Tried to add second document");
		g_dbus_method_invocation_return_error (invocation,
						       PD_ERROR,
						       PD_ERROR_FAILED,
						       N_("No more documents allowed"));
		goto out;
	}

	/* If the document format has been specified, make a note of it. */
	g_variant_lookup (options, "document-format", "s",
			  &job->document_mimetype);

	job_debug (PD_JOB (job), "Adding document");
	if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 1) {
		job_warning (PD_JOB (job), "Bad AddDocument call");
		g_dbus_method_invocation_return_error (invocation,
						       PD_ERROR,
						       PD_ERROR_FAILED,
						       N_("Bad AddDocumentCall"));
		goto out;
	}

	fd_handle = g_variant_get_handle (file_descriptor);
	job->document_fd = g_unix_fd_list_get (fd_list,
					       fd_handle,
					       &error);
	if (job->document_fd < 0) {
		job_debug (PD_JOB (job), " failed to get file descriptor: %s",
			   error ? error->message : "(no error message)");
		g_dbus_method_invocation_return_gerror (invocation,
							error);
		if (error)
			g_error_free (error);
		goto out;
	}

	job_debug (PD_JOB (job), "Got file descriptor: %d", job->document_fd);
	g_dbus_method_invocation_return_value (invocation, NULL);

 out:
	g_mutex_unlock (&job->lock);
	g_object_thaw_notify (G_OBJECT (job));
	g_free (requesting_user);
	return TRUE; /* handled the method invocation */
}

/* runs in main thread */
static gboolean
pd_job_impl_start (PdJob *_job,
		   GDBusMethodInvocation *invocation,
		   GVariant *options)
{
	PdJobImpl *job = PD_JOB_IMPL (_job);
	gchar *name_used = NULL;
	GError *error = NULL;
	GInputStream *input = NULL;
	GOutputStream *output = NULL;
	gint spoolfd;
	GVariant *attr_user;
	gchar *requesting_user = NULL;
	const gchar *originating_user = NULL;

	/* Check if the user is authorized to start a job */
	if (!pd_daemon_check_authorization_sync (job->daemon,
						 "org.freedesktop.printerd.job-add",
						 options,
						 N_("Authentication is required to add a job"),
						 invocation))
		return TRUE;

	/* Get the requesting user before holding the lock as it uses
	 * sync D-Bus calls */
	requesting_user = pd_get_unix_user (invocation);

	g_mutex_lock (&job->lock);
	g_object_freeze_notify (G_OBJECT (job));

	/* Check if this user owns the job */
	attr_user = get_attribute_value (PD_JOB (job),
					 "job-originating-user-name");
	if (attr_user) {
		originating_user = g_variant_get_string (attr_user, NULL);
		g_variant_unref (attr_user);
	}
	if (g_strcmp0 (originating_user, requesting_user)) {
		job_debug (PD_JOB (job), "AddDocument: denied "
			   "[originating user: %s; requesting user: %s]",
			   originating_user, requesting_user);
		g_dbus_method_invocation_return_error (invocation,
						       PD_ERROR,
						       PD_ERROR_FAILED,
						       N_("Not job owner"));
		goto out;
	}

	if (job->document_fd == -1) {
		g_dbus_method_invocation_return_error (invocation,
						       PD_ERROR,
						       PD_ERROR_FAILED,
						       "No document");
		goto out;
	}

	g_assert (job->document_filename == NULL);
	spoolfd = g_file_open_tmp ("printerd-spool-XXXXXX",
				   &name_used,
				   &error);

	if (spoolfd < 0) {
		job_debug (PD_JOB (job), "Error making temporary file: %s",
			   error->message);
		g_dbus_method_invocation_return_gerror (invocation,
							error);
		g_error_free (error);
		goto out;
	}

	job->document_filename = g_strdup (name_used);

	job_debug (PD_JOB (job), "Starting job");

	job_debug (PD_JOB (job), "Spooling");
	job_debug (PD_JOB (job), "  Created temporary file %s", name_used);

	input = g_unix_input_stream_new (job->document_fd,
					 TRUE /* close_fd */);
	output = g_unix_output_stream_new (spoolfd,
					   FALSE /* close_fd */);
	job->document_fd = -1;

	if (g_output_stream_splice (output,
				    input,
				    G_OUTPUT_STREAM_SPLICE_NONE,
				    NULL, /* cancellable */
				    &error) == -1)
		goto fail;

	if (g_output_stream_close (output, NULL, &error) == -1)
		goto fail;

	if (!g_input_stream_close (input, NULL, &error))
		goto fail;

	/* If document-format unset, use the printer's document-format
	 * default */
	if (!job->document_mimetype) {
		PdPrinter *printer;
		GVariant *defaults = NULL;

		printer = pd_job_impl_get_printer (job);
		if (printer)
			defaults = pd_printer_get_defaults (printer);

		if (defaults)
			g_variant_lookup (defaults, "document-format", "s",
					  &job->document_mimetype);

		if (printer)
			g_object_unref (printer);
	}

	/* If auto-sensing is requested, work out mime type of input */
	if (!g_strcmp0 (job->document_mimetype, "application/octet-stream")) {
		guchar data[2048];
		gssize data_size;
		gchar *content_type;
		gboolean type_uncertain = FALSE;

		job_debug (PD_JOB (job), "Auto-sensing MIME type");
		lseek (spoolfd, 0, SEEK_SET);
		g_object_unref (input);
		input = g_unix_input_stream_new (spoolfd,
						 TRUE /* close_fd */);
		data_size = g_input_stream_read (input,
						 data,
						 sizeof (data),
						 NULL,
						 &error);
		if (data_size == -1)
			goto fail;

		content_type = g_content_type_guess (name_used,
						     data,
						     (gsize) data_size,
						     &type_uncertain);

		if (type_uncertain)
			job_debug (PD_JOB (job), "Content type unknown");
		else {
			job->document_mimetype = g_content_type_get_mime_type (content_type);
			g_free (content_type);
		}
	}

	if (!job->document_mimetype) {
		job_debug (PD_JOB (job), "Aborting due to unknown MIME type");
		goto unsupported_doctype;
	}

	job_debug (PD_JOB (job), "MIME type: %s", job->document_mimetype);

	/* Check we're dealing with a MIME type we can handle */
	if (strcmp (job->document_mimetype, "application/pdf")) {
		job_debug (PD_JOB (job), "Unsupported MIME type, aborting");
	unsupported_doctype:
		pd_job_impl_do_cancel_with_reason (job,
						   PD_JOB_STATE_ABORTED,
						   "job-aborted-by-system");
		g_dbus_method_invocation_return_error (invocation,
						       PD_ERROR,
						       PD_ERROR_UNSUPPORTED_DOCUMENT_TYPE,
						       N_("Unsupported document type"));
		goto out;
	}

	/* Job is no longer incoming so remove that state reason if
	   present */
	pd_job_impl_remove_state_reason (job, "job-incoming");

	/* Move the job state to pending: this is now a candidate to
	   start processing */
	job_debug (PD_JOB (job), " Set job state to pending");
	pd_job_set_state (PD_JOB (job), PD_JOB_STATE_PENDING);

	/* Return success */
	g_dbus_method_invocation_return_value (invocation,
					       g_variant_new ("()"));

 out:
	g_mutex_unlock (&job->lock);
	g_object_thaw_notify (G_OBJECT (job));

	g_free (requesting_user);
	if (input)
		g_object_unref (input);
	if (output)
		g_object_unref (output);
	g_free (name_used);
	return TRUE; /* handled the method invocation */

fail:
	/* error */
	g_dbus_method_invocation_return_error (invocation,
					       PD_ERROR,
					       PD_ERROR_FAILED,
					       "Error spooling file");
	goto out;

}

/* Cancel or abort job */
static void
pd_job_impl_do_cancel_with_reason (PdJobImpl *job,
				   gint job_state,
				   const gchar *reason)
{
	PdJob *_job = PD_JOB (job);
	GList *filter;

	pd_job_impl_add_state_reason (PD_JOB_IMPL (job), reason);

        /* RFC 2911, 3.3.3: only these jobs in these states can be
	   canceled */
	switch (pd_job_get_state (_job)) {
	case PD_JOB_STATE_PENDING:
	case PD_JOB_STATE_PENDING_HELD:
		/* These can be canceled right away. */

		/* Change job state. */
		job_debug (PD_JOB (job), "Pending job set to state %s",
			   pd_job_state_as_string (job_state));
		pd_job_set_state (_job, job_state);

		/* Nothing else to do */
		break;

	case PD_JOB_STATE_PROCESSING:
	case PD_JOB_STATE_PROCESSING_STOPPED:
		/* These need some time to tell the printer to
		   e.g. eject the current page */

		if (!pd_job_impl_check_job_transforming (job) &&
		    !job->backend->started) {
			/* Already finished */
			job_debug (PD_JOB (job),
				   "Stopped after transformation");
			pd_job_set_state (_job, job_state);
			break;
		}

		if (state_reason_is_set (job, "processing-to-stop-point"))
			break;

		job->pending_job_state = job_state;
		pd_job_impl_add_state_reason (job, "processing-to-stop-point");

		if (job->backend->channel[STDIN_FILENO] != NULL) {
			/* Stop sending data to the backend. */
			job_debug (PD_JOB (job),
				   "Stop sending data to the backend");
			if (job->backend->io_source[STDIN_FILENO]) {
				g_source_remove (job->backend->io_source[STDIN_FILENO]);
				job->backend->io_source[STDIN_FILENO] = 0;
			}
			g_io_channel_shutdown (job->backend->channel[STDIN_FILENO],
					       FALSE,
					       NULL);
			g_io_channel_unref (job->backend->channel[STDIN_FILENO]);
			job->backend->channel[STDIN_FILENO] = NULL;
		}

		/* Simple implementation for now: just kill the processes */
		for (filter = g_list_first (job->filterchain);
		     filter;
		     filter = g_list_next (filter)) {
			struct _PdJobProcess *jp = filter->data;
			if (!jp->started ||
			    jp->finished)
				continue;

			job_debug (PD_JOB (job),
				   "Sending KILL signal to %s (PID %d)",
				   jp->what, jp->pid);
			kill (jp->pid, SIGKILL);
			g_spawn_close_pid (jp->pid);
		}

		if (job->backend->started &&
		    !job->backend->finished) {
			job_debug (PD_JOB (job),
				   "Sending KILL signal to backend (PID %d)",
				   job->backend->pid);
			kill (job->backend->pid, SIGKILL);
			g_spawn_close_pid (job->backend->pid);
		}

		break;

	default: /* only completed job states remaining */
		break;
		;
	}
}

/* runs in main thread */
static gboolean
pd_job_impl_cancel (PdJob *_job,
		    GDBusMethodInvocation *invocation,
		    GVariant *options)
{
	PdJobImpl *job = PD_JOB_IMPL (_job);
	GVariant *attr_user;
	gchar *requesting_user = NULL;
	const gchar *originating_user = NULL;

	/* Check if the user is authorized to cancel this job */
	if (!pd_daemon_check_authorization_sync (job->daemon,
						 "org.freedesktop.printerd.job-cancel",
						 options,
						 N_("Authentication is required to cancel a job"),
						 invocation))
		return TRUE;

	/* Get the requesting user before holding the lock as it uses
	 * sync D-Bus calls */
	requesting_user = pd_get_unix_user (invocation);

	g_mutex_lock (&job->lock);
	g_object_freeze_notify (G_OBJECT (job));

	/* Check if this user owns the job */
	attr_user = get_attribute_value (PD_JOB (job),
					 "job-originating-user-name");
	if (attr_user) {
		originating_user = g_variant_get_string (attr_user, NULL);
		g_variant_unref (attr_user);
	}
	if (g_strcmp0 (originating_user, requesting_user)) {
		job_debug (PD_JOB (job), "AddDocument: denied "
			   "[originating user: %s; requesting user: %s]",
			   originating_user, requesting_user);
		g_dbus_method_invocation_return_error (invocation,
						       PD_ERROR,
						       PD_ERROR_FAILED,
						       N_("Not job owner"));
		goto out;
	}

	switch (pd_job_get_state (_job)) {
	case PD_JOB_STATE_PENDING:
	case PD_JOB_STATE_PENDING_HELD:
		/* These can be canceled right away. */
		job_debug (PD_JOB (job), "Canceled by user");
		goto cancel;
		break;

	case PD_JOB_STATE_PROCESSING:
	case PD_JOB_STATE_PROCESSING_STOPPED:
		/* These need some time to tell the printer to
		   e.g. eject the current page */

		if (!pd_job_impl_check_job_transforming (job) &&
		    !job->backend->started)
			goto cancel;

		if (state_reason_is_set (job, "processing-to-stop-point")) {
			g_dbus_method_invocation_return_error (invocation,
							       PD_ERROR,
							       PD_ERROR_FAILED,
							       N_("Already canceled"));
			break;
		}

	cancel:
		/* Now cancel the job. */
		pd_job_impl_do_cancel_with_reason (job,
						   PD_JOB_STATE_CANCELED,
						   "job-canceled-by-user");
		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new ("()"));
		break;

	default: /* only completed job states remaining */
		/* Send error */
		g_dbus_method_invocation_return_error (invocation,
						       PD_ERROR,
						       PD_ERROR_FAILED,
						       N_("Cannot cancel completed job"));
		break;
		;
	}

 out:
	g_mutex_unlock (&job->lock);
	g_object_thaw_notify (G_OBJECT (job));
	g_free (requesting_user);
	return TRUE; /* handled the method invocation */
}


static void
pd_job_iface_init (PdJobIface *iface)
{
	iface->handle_add_document = pd_job_impl_add_document;
	iface->handle_start = pd_job_impl_start;
	iface->handle_cancel = pd_job_impl_cancel;
}
