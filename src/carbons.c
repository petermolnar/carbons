/*
carbons - XEP-0280 plugin for libpurple
Copyright (C) 2017, Richard Bayerle <riba@firemail.cc>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <glib.h>

#include <stdlib.h>
#include <string.h>

#include <purple.h>

#include "iq.h"

#include "carbons.h"

#define JABBER_PROTOCOL_ID "prpl-jabber"

#define CARBONS_SETTING_NAME "carbons-enabled"
#define CARBONS_LOG_CATEGORY "carbons"

#define CARBONS_XMLNS   "urn:xmpp:carbons:2"
#define DISCO_XMLNS     "http://jabber.org/protocol/disco#info" // see XEP-0030: Service Discovery (https://xmpp.org/extensions/xep-0030.html)

static int carbons_is_valid(PurpleAccount * acc_p, xmlnode * outer_msg_stanza_p) {
  char ** split;

  split = g_strsplit(purple_account_get_username(acc_p), "/", 2);

  if (g_strcmp0(split[0], xmlnode_get_attrib(outer_msg_stanza_p, "from"))) {
    purple_debug_warning(CARBONS_LOG_CATEGORY, "Invalid sender: %s (should be: %s)\n", xmlnode_get_attrib(outer_msg_stanza_p, "from"), split[0]);
    g_strfreev(split);
    return 0;
  } else {
    g_strfreev(split);
    return 1;
  }
}

static void carbons_xml_received_cb(PurpleConnection * gc_p, xmlnode ** stanza_pp) {
  xmlnode * carbons_node_p    = (void *) 0;
  xmlnode * forwarded_node_p  = (void *) 0;
  xmlnode * msg_node_p        = (void *) 0;

  carbons_node_p = xmlnode_get_child_with_namespace(*stanza_pp, "received", CARBONS_XMLNS);
  if (carbons_node_p) {
    purple_debug_info(CARBONS_LOG_CATEGORY, "Received carbon copy of a received message.\n");

    if (!carbons_is_valid(purple_connection_get_account(gc_p), *stanza_pp)) {
      purple_debug_warning(CARBONS_LOG_CATEGORY, "Ignoring carbon copy of received message with invalid sender.\n");
      return;
    }

    forwarded_node_p = xmlnode_get_child(carbons_node_p, "forwarded");
    if (!forwarded_node_p) {
      purple_debug_error(CARBONS_LOG_CATEGORY, "Ignoring carbon copy of received message that does not contain a 'forwarded' node.\n");
      return;
    }

    msg_node_p = xmlnode_get_child(forwarded_node_p, "message");
    if (!msg_node_p) {
      purple_debug_error(CARBONS_LOG_CATEGORY, "Ignoring carbon copy of received message that does not contain a 'message' node.\n");
      return;
    }

    msg_node_p = xmlnode_copy(msg_node_p);
    xmlnode_free(*stanza_pp);
    *stanza_pp = msg_node_p;
    return;
  }

  carbons_node_p = xmlnode_get_child_with_namespace(*stanza_pp, "sent", CARBONS_XMLNS);
  if (carbons_node_p) {
    purple_debug_info(CARBONS_LOG_CATEGORY, "Received carbon copy of a sent message.\n");

    if (!carbons_is_valid(purple_connection_get_account(gc_p), *stanza_pp)) {
      purple_debug_warning(CARBONS_LOG_CATEGORY, "Ignoring carbon copy of sent message with invalid sender.\n");
      return;
    }

    forwarded_node_p = xmlnode_get_child(carbons_node_p, "forwarded");
    if (!forwarded_node_p) {
      purple_debug_error(CARBONS_LOG_CATEGORY, "Ignoring carbon copy of sent message that does not contain a 'forwarded' node.\n");
      return;
    }

    msg_node_p = xmlnode_get_child(forwarded_node_p, "message");
    if (!msg_node_p) {
      purple_debug_error(CARBONS_LOG_CATEGORY, "Ignoring carbon copy of sent message that does not contain a 'message' node.\n");
      return;
    }

    // add an empty node inside the message node for detection in later callback
    carbons_node_p = xmlnode_new_child(msg_node_p, "sent");
    xmlnode_set_namespace(carbons_node_p, CARBONS_XMLNS);

    purple_debug_info(CARBONS_LOG_CATEGORY, "Stripped carbons envelope of a sent message and passing through the message stanza.\n");
    msg_node_p = xmlnode_copy(msg_node_p);
    xmlnode_free(*stanza_pp);
    *stanza_pp = msg_node_p;
  }
}

// libpurple doesn't know what to do with incoming messages addressed to someone else, so they need to be written to the conversation manually
// checks for presence of a <sent /> node that was inserted in the initial handler
static void carbons_xml_stripped_cb(PurpleConnection * gc_p, xmlnode ** stanza_pp) {
  xmlnode * carbons_node_p    = (void *) 0;
  xmlnode * body_node_p       = (void *) 0;
  char * buddy_name_bare      = (void *) 0;
  PurpleConversation * conv_p = (void *) 0;

  if (!(*stanza_pp) || g_strcmp0((*stanza_pp)->name, "message")) {
    return;
  }

  carbons_node_p = xmlnode_get_child_with_namespace(*stanza_pp, "sent", CARBONS_XMLNS);
  if (!carbons_node_p) {
    return;
  }

  body_node_p = xmlnode_get_child(*stanza_pp, "body");
  if (body_node_p) {
    buddy_name_bare = jabber_get_bare_jid(xmlnode_get_attrib(*stanza_pp, "to"));
    conv_p = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, buddy_name_bare, purple_connection_get_account(gc_p));
    if (!conv_p) {
      conv_p = purple_conversation_new(PURPLE_CONV_TYPE_IM, purple_connection_get_account(gc_p), buddy_name_bare);
    }

    purple_debug_info(CARBONS_LOG_CATEGORY, "Writing body of the carbon copy of a sent message to the conversation window with %s.\n", buddy_name_bare);
    purple_conversation_write(conv_p, xmlnode_get_attrib(*stanza_pp, "from"), xmlnode_get_data(body_node_p), PURPLE_MESSAGE_SEND, time((void *) 0));

    xmlnode_free(*stanza_pp);
    *stanza_pp = (void *) 0;
    g_free(buddy_name_bare);
  }
}

static void carbons_autoenable_cb(JabberStream * js_p, const char * from,
                                  JabberIqType type,   const char * id,
                                  xmlnode * packet_p,  gpointer data_p) {
  const char * accname = purple_account_get_username(purple_connection_get_account(js_p->gc));

  if (type == JABBER_IQ_ERROR) {
    purple_debug_error(CARBONS_LOG_CATEGORY, "Server returned an error when trying to activate carbons for %s.\n", accname);
  } else {
    purple_debug_info(CARBONS_LOG_CATEGORY, "Successfully activated carbons for %s.\n", accname);
  }
}

static void carbons_autoenable(PurpleAccount * acc_p) {
  JabberIq * jiq_p = (void *) 0;
  xmlnode * req_node_p = (void *) 0;
  JabberStream * js_p = purple_connection_get_protocol_data(purple_account_get_connection(acc_p));

  jiq_p = jabber_iq_new(js_p, JABBER_IQ_SET);
  req_node_p = xmlnode_new_child(jiq_p->node, "enable");
  xmlnode_set_namespace(req_node_p, CARBONS_XMLNS);

  jabber_iq_set_callback(jiq_p, carbons_autoenable_cb, (void *) 0);
  jabber_iq_send(jiq_p);

  purple_debug_info(CARBONS_LOG_CATEGORY, "Sent enable request for %s.\n", purple_account_get_username(acc_p));
}

static void carbons_discover_cb(JabberStream * js_p, const char * from,
                                JabberIqType type,   const char * id,
                                xmlnode * packet_p,  gpointer data_p) {

  xmlnode * query_node_p   = (void *) 0;
  const char * accname      = purple_account_get_username(purple_connection_get_account(js_p->gc));

  if (type == JABBER_IQ_ERROR) {
    purple_debug_error(CARBONS_LOG_CATEGORY, "Server returned an error when trying to discover carbons for %s.\n", accname);
    return;
  }

  query_node_p = xmlnode_get_child_with_namespace(packet_p, "query", DISCO_XMLNS);
  if (!query_node_p) {
    purple_debug_error(CARBONS_LOG_CATEGORY, "No 'query' node in feature discovery reply for %s.\n", accname);
    return;
  }
  purple_debug_info(CARBONS_LOG_CATEGORY, "Trying to enable carbons %s.\n", accname);
  carbons_autoenable(purple_connection_get_account(js_p->gc));
}

static void carbons_discover(PurpleAccount * acc_p) {
  JabberIq * jiq_p = (void *) 0;
  xmlnode * query_node_p = (void *) 0;
  JabberStream * js_p = purple_connection_get_protocol_data(purple_account_get_connection(acc_p));
  const char * username = purple_account_get_username(acc_p);

  jiq_p = jabber_iq_new(js_p, JABBER_IQ_GET);
  xmlnode_set_attrib(jiq_p->node, "to", jabber_get_domain(username));
  query_node_p = xmlnode_new_child(jiq_p->node, "query");
  xmlnode_set_namespace(query_node_p, DISCO_XMLNS);

  jabber_iq_set_callback(jiq_p, carbons_discover_cb, (void *) 0);
  jabber_iq_send(jiq_p);

  purple_debug_info(CARBONS_LOG_CATEGORY, "Sent feature discovery request for %s.\n", purple_account_get_username(acc_p));
}

static void carbons_account_connect_cb(PurpleAccount * acc_p) {
  if (strcmp(purple_account_get_protocol_id(acc_p), "prpl-jabber")) {
    return;
  }

  // "migration code" - remove obsolete setting
  purple_account_remove_setting(acc_p, CARBONS_SETTING_NAME);

  carbons_discover(acc_p);
}

static gboolean
carbons_plugin_load(PurplePlugin * plugin_p) {

  GList * accs_l_p      = (void *) 0;
  GList * curr_p        = (void *) 0;
  PurpleAccount * acc_p = (void *) 0;

  (void) jabber_add_feature(CARBONS_XMLNS, (void *) 0);

  (void) purple_signal_connect(purple_accounts_get_handle(), "account-signed-on", plugin_p, PURPLE_CALLBACK(carbons_account_connect_cb), NULL);
  (void) purple_signal_connect_priority(purple_plugins_find_with_id("prpl-jabber"), "jabber-receiving-xmlnode", plugin_p, PURPLE_CALLBACK(carbons_xml_received_cb), NULL, PURPLE_PRIORITY_LOWEST + 100);
  (void) purple_signal_connect_priority(purple_plugins_find_with_id("prpl-jabber"), "jabber-receiving-xmlnode", plugin_p, PURPLE_CALLBACK(carbons_xml_stripped_cb), NULL, PURPLE_PRIORITY_HIGHEST - 50);

  // manually call init code if there are already accounts connected, e.g. when plugin is loaded manually
  accs_l_p = purple_accounts_get_all_active();
  for (curr_p = accs_l_p; curr_p; curr_p = curr_p->next) {
    acc_p = (PurpleAccount *) curr_p->data;
    if (purple_account_is_connected(acc_p)) {
      if (!g_strcmp0(purple_account_get_protocol_id(acc_p), JABBER_PROTOCOL_ID)) {
        carbons_account_connect_cb(acc_p);
      }
    }
  }

  g_list_free(accs_l_p);

  return TRUE;
}

static PurplePluginInfo info = {
    PURPLE_PLUGIN_MAGIC,
    PURPLE_MAJOR_VERSION,
    PURPLE_MINOR_VERSION,
    PURPLE_PLUGIN_STANDARD,
    NULL,
    0,
    NULL,
    PURPLE_PRIORITY_DEFAULT,

    "core-riba-carbons",
    "XMPP Message Carbons",
    CARBONS_VERSION,

    "Implements XEP-0280: Message Carbons as a plugin.",
    "This plugin enables a consistent history view across multiple devices which are online at the same time.",
    CARBONS_AUTHOR,
    "https://github.com/gkdr/carbons",

    carbons_plugin_load,
    NULL,
    NULL,

    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static void
carbons_plugin_init(PurplePlugin * plugin_p)
{
  PurplePluginInfo * info_p = plugin_p->info;

  info_p->dependencies = g_list_prepend(info_p->dependencies, "prpl-jabber");
}

PURPLE_INIT_PLUGIN(carbons, carbons_plugin_init, info)
