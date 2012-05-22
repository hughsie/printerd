/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "pd-client.h"
#include "pd-error.h"
#include "pd-generated.h"

/**
 * SECTION:printerdclient
 * @title: PdClient
 * @short_description: printerd Client
 *
 * #PdClient is used for accessing the printerd service from a
 * client program.
 */

G_LOCK_DEFINE_STATIC (init_lock);

/**
 * PdClient:
 *
 * The #PdClient structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _PdClient
{
  GObject parent_instance;

  gboolean is_initialized;
  GError *initialization_error;

  GDBusObjectManager *object_manager;

  GMainContext *context;

  GSource *changed_timeout_source;
};

typedef struct
{
  GObjectClass parent_class;
} PdClientClass;

enum
{
  PROP_0,
  PROP_OBJECT_MANAGER,
  PROP_MANAGER
};

enum
{
  CHANGED_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void initable_iface_init       (GInitableIface      *initable_iface);
static void async_initable_iface_init (GAsyncInitableIface *async_initable_iface);

static void on_object_added (GDBusObjectManager  *manager,
                             GDBusObject         *object,
                             gpointer             user_data);

static void on_object_removed (GDBusObjectManager  *manager,
                               GDBusObject         *object,
                               gpointer             user_data);

static void on_interface_added (GDBusObjectManager  *manager,
                                GDBusObject         *object,
                                GDBusInterface      *interface,
                                gpointer             user_data);

static void on_interface_removed (GDBusObjectManager  *manager,
                                  GDBusObject         *object,
                                  GDBusInterface      *interface,
                                  gpointer             user_data);

static void on_interface_proxy_properties_changed (GDBusObjectManagerClient   *manager,
                                                   GDBusObjectProxy           *object_proxy,
                                                   GDBusProxy                 *interface_proxy,
                                                   GVariant                   *changed_properties,
                                                   const gchar *const         *invalidated_properties,
                                                   gpointer                    user_data);

static void maybe_emit_changed_now (PdClient *client);

static void init_interface_proxy (PdClient *client,
                                  GDBusProxy   *proxy);

G_DEFINE_TYPE_WITH_CODE (PdClient, pd_client, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init)
                         );

static void
pd_client_finalize (GObject *object)
{
  PdClient *client = PD_CLIENT (object);

  if (client->changed_timeout_source != NULL)
    g_source_destroy (client->changed_timeout_source);

  if (client->initialization_error != NULL)
    g_error_free (client->initialization_error);

  g_signal_handlers_disconnect_by_func (client->object_manager,
                                        G_CALLBACK (on_object_added),
                                        client);
  g_signal_handlers_disconnect_by_func (client->object_manager,
                                        G_CALLBACK (on_object_removed),
                                        client);
  g_signal_handlers_disconnect_by_func (client->object_manager,
                                        G_CALLBACK (on_interface_added),
                                        client);
  g_signal_handlers_disconnect_by_func (client->object_manager,
                                        G_CALLBACK (on_interface_removed),
                                        client);
  g_signal_handlers_disconnect_by_func (client->object_manager,
                                        G_CALLBACK (on_interface_proxy_properties_changed),
                                        client);
  g_object_unref (client->object_manager);

  if (client->context != NULL)
    g_main_context_unref (client->context);

  G_OBJECT_CLASS (pd_client_parent_class)->finalize (object);
}

static void
pd_client_init (PdClient *client)
{
  static volatile GQuark pd_error_domain = 0;
  /* this will force associating errors in the PD_ERROR error
   * domain with org.freedesktop.printerd.Error.* errors via
   * g_dbus_error_register_error_domain().
   */
  pd_error_domain = PD_ERROR;
  pd_error_domain; /* shut up -Wunused-but-set-variable */
}

static void
pd_client_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PdClient *client = PD_CLIENT (object);

  switch (prop_id)
    {
    case PROP_OBJECT_MANAGER:
      g_value_set_object (value, pd_client_get_object_manager (client));
      break;

    case PROP_MANAGER:
      g_value_set_object (value, pd_client_get_manager (client));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
pd_client_class_init (PdClientClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = pd_client_finalize;
  gobject_class->get_property = pd_client_get_property;

  /**
   * PdClient:object-manager:
   *
   * The #GDBusObjectManager used by the #PdClient instance.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT_MANAGER,
                                   g_param_spec_object ("object-manager",
                                                        "Object Manager",
                                                        "The GDBusObjectManager used by the PdClient",
                                                        G_TYPE_DBUS_OBJECT_MANAGER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * PdClient:manager:
   *
   * The #PdManager interface on the well-known
   * <literal>/org/freedesktop/printerd/Manager</literal> object
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MANAGER,
                                   g_param_spec_object ("manager",
                                                        "Manager",
                                                        "The PdManager",
                                                        PD_TYPE_MANAGER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * PdClient::changed:
   * @client: A #PdClient.
   *
   * This signal is emitted either when an object or interface is
   * added or removed a when property has changed. Additionally,
   * multiple received signals are coalesced into a single signal that
   * is rate-limited to fire at most every 100ms.
   *
   * Note that calling pd_client_settle() will cause this signal
   * to fire if any changes are outstanding.
   *
   * For greater detail, connect to the
   * #GDBusObjectManager::object-added,
   * #GDBusObjectManager::object-removed,
   * #GDBusObjectManager::interface-added,
   * #GDBusObjectManager::interface-removed,
   * #GDBusObjectManagerClient::interface-proxy-properties-changed and
   * signals on the #PdClient:object-manager object.
   */
  signals[CHANGED_SIGNAL] = g_signal_new ("changed",
                                          G_OBJECT_CLASS_TYPE (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0, /* G_STRUCT_OFFSET */
                                          NULL, /* accu */
                                          NULL, /* accu data */
                                          g_cclosure_marshal_generic,
                                          G_TYPE_NONE,
                                          0);

}

/**
 * pd_client_new:
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Function that will be called when the result is ready.
 * @user_data: Data to pass to @callback.
 *
 * Asynchronously gets a #PdClient. When the operation is
 * finished, @callback will be invoked in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread you are calling this method from.
 */
void
pd_client_new (GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
  g_async_initable_new_async (PD_TYPE_CLIENT,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              callback,
                              user_data,
                              NULL);
}

/**
 * pd_client_new_finish:
 * @res: A #GAsyncResult.
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with pd_client_new().
 *
 * Returns: A #PdClient or %NULL if @error is set. Free with
 * g_object_unref() when done with it.
 */
PdClient *
pd_client_new_finish (GAsyncResult        *res,
                          GError             **error)
{
  GObject *ret;
  GObject *source_object;
  source_object = g_async_result_get_source_object (res);
  ret = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, error);
  g_object_unref (source_object);
  if (ret != NULL)
    return PD_CLIENT (ret);
  else
    return NULL;
}

/**
 * pd_client_new_sync:
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: (allow-none): Return location for error or %NULL.
 *
 * Synchronously gets a #PdClient for the local system.
 *
 * Returns: A #PdClient or %NULL if @error is set. Free with
 * g_object_unref() when done with it.
 */
PdClient *
pd_client_new_sync (GCancellable  *cancellable,
                        GError       **error)
{
  GInitable *ret;
  ret = g_initable_new (PD_TYPE_CLIENT,
                        cancellable,
                        error,
                        NULL);
  if (ret != NULL)
    return PD_CLIENT (ret);
  else
    return NULL;
}

/* ------------------------------------------------------------------ */

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  PdClient *client = PD_CLIENT (initable);
  gboolean ret;
  GList *objects, *l;
  GList *interfaces, *ll;

  ret = FALSE;

  /* This method needs to be idempotent to work with the singleton
   * pattern. See the docs for g_initable_init(). We implement this by
   * locking.
   */
  G_LOCK (init_lock);
  if (client->is_initialized)
    {
      if (client->object_manager != NULL)
        ret = TRUE;
      else
        g_assert (client->initialization_error != NULL);
      goto out;
    }
  g_assert (client->initialization_error == NULL);

  client->context = g_main_context_get_thread_default ();
  if (client->context != NULL)
    g_main_context_ref (client->context);

  client->object_manager = pd_object_manager_client_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                          G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                                          "org.freedesktop.printerd",
                                                                          "/org/freedesktop/printerd",
                                                                          cancellable,
                                                                          &client->initialization_error);
  if (client->object_manager == NULL)
    goto out;

  /* init all proxies */
  objects = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      interfaces = g_dbus_object_get_interfaces (G_DBUS_OBJECT (l->data));
      for (ll = interfaces; ll != NULL; ll = ll->next)
        {
          init_interface_proxy (client, G_DBUS_PROXY (ll->data));
        }
      g_list_foreach (interfaces, (GFunc) g_object_unref, NULL);
      g_list_free (interfaces);
    }
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);

  g_signal_connect (client->object_manager,
                    "object-added",
                    G_CALLBACK (on_object_added),
                    client);
  g_signal_connect (client->object_manager,
                    "object-removed",
                    G_CALLBACK (on_object_removed),
                    client);
  g_signal_connect (client->object_manager,
                    "interface-added",
                    G_CALLBACK (on_interface_added),
                    client);
  g_signal_connect (client->object_manager,
                    "interface-removed",
                    G_CALLBACK (on_interface_removed),
                    client);
  g_signal_connect (client->object_manager,
                    "interface-proxy-properties-changed",
                    G_CALLBACK (on_interface_proxy_properties_changed),
                    client);

  ret = TRUE;

out:
  client->is_initialized = TRUE;
  if (!ret)
    {
      g_assert (client->initialization_error != NULL);
      g_propagate_error (error, g_error_copy (client->initialization_error));
    }
  G_UNLOCK (init_lock);
  return ret;
}

static void
initable_iface_init (GInitableIface      *initable_iface)
{
  initable_iface->init = initable_init;
}

static void
async_initable_iface_init (GAsyncInitableIface *async_initable_iface)
{
  /* Use default implementation (e.g. run GInitable code in a thread) */
}

/**
 * pd_client_get_object_manager:
 * @client: A #PdClient.
 *
 * Gets the #GDBusObjectManager used by @client.
 *
 * Returns: (transfer none): A #GDBusObjectManager. Do not free, the
 * instance is owned by @client.
 */
GDBusObjectManager *
pd_client_get_object_manager (PdClient        *client)
{
  g_return_val_if_fail (PD_IS_CLIENT (client), NULL);
  return client->object_manager;
}

/**
 * pd_client_get_manager:
 * @client: A #PdClient.
 *
 * Gets the #PdManager interface on the well-known
 * <literal>/org/freedesktop/printerd/Manager</literal> object.
 *
 * Returns: (transfer none): A #PdManager or %NULL if the printerd
 * daemon is not currently running. Do not free, the instance is owned
 * by @client.
 */
PdManager *
pd_client_get_manager (PdClient *client)
{
  PdManager *ret = NULL;
  GDBusObject *obj;

  g_return_val_if_fail (PD_IS_CLIENT (client), NULL);

  obj = g_dbus_object_manager_get_object (client->object_manager, "/org/freedesktop/printerd/Manager");
  if (obj == NULL)
    goto out;

  ret = pd_object_peek_manager (PD_OBJECT (obj));
  g_object_unref (obj);

 out:
  return ret;
}

/**
 * pd_client_settle:
 * @client: A #PdClient.
 *
 * Blocks until all pending D-Bus messages have been delivered. Also
 * emits the (rate-limited) #PdClient::changed signal if changes
 * are currently pending.
 *
 * This is useful in two situations: 1. when using synchronous method
 * calls since e.g. D-Bus signals received while waiting for the reply
 * are queued up and dispatched after the synchronous call ends; and
 * 2. when using asynchronous calls where the return value references
 * a newly created object (such as the <link
 * linkend="gdbus-method-org-freedesktop-printerd-Manager.CreatePrinter">Manager.CreatePrinter()</link> method).
 */
void
pd_client_settle (PdClient *client)
{
  while (g_main_context_iteration (client->context, FALSE /* may_block */))
    ;
  /* TODO: careful if on different thread... */
  maybe_emit_changed_now (client);
}

/* ------------------------------------------------------------------ */

/**
 * pd_client_get_object:
 * @client: A #PdClient.
 * @object_path: Object path.
 *
 * Convenience function for looking up an #PdObject for @object_path.
 *
 * Returns: (transfer full): A #PdObject corresponding to
 * @object_path or %NULL if not found. The returned object must be
 * freed with g_object_unref().
 */
PdObject *
pd_client_get_object (PdClient  *client,
                          const gchar   *object_path)
{
  g_return_val_if_fail (PD_IS_CLIENT (client), NULL);
  return (PdObject *) g_dbus_object_manager_get_object (client->object_manager, object_path);
}

/**
 * pd_client_peek_object:
 * @client: A #PdClient.
 * @object_path: Object path.
 *
 * Like pd_client_get_object() but doesn't increase the reference
 * count on the returned #PdObject.
 *
 * <warning>The returned object is only valid until removed so it is only safe to use this function on the thread where @client was constructed. Use pd_client_get_object() if on another thread.</warning>
 *
 * Returns: (transfer none): A #PdObject corresponding to
 * @object_path or %NULL if not found.
 */
PdObject *
pd_client_peek_object (PdClient  *client,
                           const gchar   *object_path)
{
  PdObject *ret;
  ret = pd_client_get_object (client, object_path);
  if (ret != NULL)
    g_object_unref (ret);
  return ret;
}

/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */

static void
maybe_emit_changed_now (PdClient *client)
{
  if (client->changed_timeout_source == NULL)
    goto out;

  g_source_destroy (client->changed_timeout_source);
  client->changed_timeout_source = NULL;

  g_signal_emit (client, signals[CHANGED_SIGNAL], 0);

 out:
  ;
}


static gboolean
on_changed_timeout (gpointer user_data)
{
  PdClient *client = PD_CLIENT (user_data);
  client->changed_timeout_source = NULL;
  g_signal_emit (client, signals[CHANGED_SIGNAL], 0);
  return FALSE; /* remove source */
}

static void
queue_changed (PdClient *client)
{
  if (client->changed_timeout_source != NULL)
    goto out;

  client->changed_timeout_source = g_timeout_source_new (100);
  g_source_set_callback (client->changed_timeout_source,
                         (GSourceFunc) on_changed_timeout,
                         client,
                         NULL); /* destroy notify */
  g_source_attach (client->changed_timeout_source, client->context);
  g_source_unref (client->changed_timeout_source);

 out:
  ;
}

static void
on_object_added (GDBusObjectManager  *manager,
                 GDBusObject         *object,
                 gpointer             user_data)
{
  PdClient *client = PD_CLIENT (user_data);
  queue_changed (client);
}

static void
on_object_removed (GDBusObjectManager  *manager,
                   GDBusObject         *object,
                   gpointer             user_data)
{
  PdClient *client = PD_CLIENT (user_data);
  queue_changed (client);
}

static void
init_interface_proxy (PdClient *client,
                      GDBusProxy   *proxy)
{
  /* disable method timeouts */
  g_dbus_proxy_set_default_timeout (proxy, G_MAXINT);
}

static void
on_interface_added (GDBusObjectManager  *manager,
                    GDBusObject         *object,
                    GDBusInterface      *interface,
                    gpointer             user_data)
{
  PdClient *client = PD_CLIENT (user_data);

  init_interface_proxy (client, G_DBUS_PROXY (interface));

  queue_changed (client);
}

static void
on_interface_removed (GDBusObjectManager  *manager,
                      GDBusObject         *object,
                      GDBusInterface      *interface,
                      gpointer             user_data)
{
  PdClient *client = PD_CLIENT (user_data);
  queue_changed (client);
}

static void
on_interface_proxy_properties_changed (GDBusObjectManagerClient   *manager,
                                       GDBusObjectProxy           *object_proxy,
                                       GDBusProxy                 *interface_proxy,
                                       GVariant                   *changed_properties,
                                       const gchar *const         *invalidated_properties,
                                       gpointer                    user_data)
{
  PdClient *client = PD_CLIENT (user_data);
  queue_changed (client);
}

/* ------------------------------------------------------------------ */
