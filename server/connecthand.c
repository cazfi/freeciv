/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <string.h>

/* utility */
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "support.h"

/* common */
#include "capstr.h"
#include "events.h"
#include "game.h"
#include "packets.h"
#include "player.h"
#include "version.h"

/* server */
#include "aiiface.h"
#include "auth.h"
#include "diplhand.h"
#include "edithand.h"
#include "gamehand.h"
#include "maphand.h"
#include "meta.h"
#include "notify.h"
#include "plrhand.h"
#include "report.h"
#include "ruleload.h"
#include "sernet.h"
#include "settings.h"
#include "srv_main.h"
#include "stdinhand.h"
#include "voting.h"

/* server/scripting */
#include "script_fcdb.h"

#include "connecthand.h"


static bool connection_attach_real(struct connection *pconn,
                                   struct player *pplayer,
                                   bool observing, bool connecting);

/**********************************************************************//**
  Set the access level of a connection, and re-send some needed info.  If
  granted is TRUE, then it will overwrite the granted_access_level too.
  Else, it will affect only the current access level.

  NB: This function does not send updated connection information to other
  clients, you need to do that yourself afterwards.
**************************************************************************/
void conn_set_access(struct connection *pconn, enum cmdlevel new_level,
                     bool granted)
{
  enum cmdlevel old_level = conn_get_access(pconn);

  pconn->access_level = new_level;
  if (granted) {
    pconn->server.granted_access_level = new_level;
  }

  /* Send updated settings only if settings control already sent.
   * Otherwise client is not ready to receive them, AND we will send
   * them later anyway. */
  if (old_level != new_level && pconn->server.settings_sent) {
    send_server_access_level_settings(pconn->self, old_level, new_level);
  }
}

/**********************************************************************//**
  Restore access level for the given connection (user). Used when taking
  a player, observing, or detaching.

  NB: This function does not send updated connection information to other
  clients, you need to do that yourself afterwards.
**************************************************************************/
static void restore_access_level(struct connection *pconn)
{
  /* Restore previous privileges. */
  enum cmdlevel level = pconn->server.granted_access_level;

  /* Detached connections must have at most the same privileges
   * as observers, unless they were granted something higher than
   * ALLOW_BASIC in the first place. */
  if ((pconn->observer || !pconn->playing) && level == ALLOW_BASIC) {
    level = ALLOW_INFO;
  }

  conn_set_access(pconn, level, FALSE);
}

/**********************************************************************//**
  This is used when a new player joins a server, before the game
  has started. If pconn is nullptr, is an AI, else a client.

  N.B. this only attaches a connection to a player if
       pconn->username == player->username

  Here we send initial packets:
  - ruleset datas.
  - server settings.
  - scenario info.
  - game info.
  - players infos (note it's resent in srv_main.c::send_all_info(),
      see comment there).
  - connections infos.
  - running vote infos.
  ... and additional packets if the game already started.
**************************************************************************/
void establish_new_connection(struct connection *pconn)
{
  struct conn_list *dest = pconn->self;
  struct player *pplayer;
  struct packet_server_join_reply packet;
  struct packet_chat_msg connect_info;
  char hostname[512];
  bool delegation_error = FALSE;
  struct packet_set_topology topo_packet;

  /* Zero out the password */
  memset(pconn->server.password, 0, sizeof(pconn->server.password));

  /* Send join_reply packet */
  packet.you_can_join = TRUE;
  sz_strlcpy(packet.capability, our_capability);
  fc_snprintf(packet.message, sizeof(packet.message), _("%s Welcome"),
              pconn->username);
  sz_strlcpy(packet.challenge_file, new_challenge_filename(pconn));
  packet.conn_id = pconn->id;
  send_packet_server_join_reply(pconn, &packet);

  /* "establish" the connection */
  pconn->established = TRUE;
  pconn->server.status = AS_ESTABLISHED;

  pconn->server.delegation.status = FALSE;
  pconn->server.delegation.playing = nullptr;
  pconn->server.delegation.observer = FALSE;
  pconn->server.settings_sent = FALSE;

  conn_list_append(game.est_connections, pconn);
  if (conn_list_size(game.est_connections) == 1) {
    /* First connection
     * Replace "restarting in x seconds" meta message */
    maybe_automatic_meta_message(default_meta_message_string());
    (void) send_server_info_to_metaserver(META_INFO);
  }

  /* Introduce the server to the connection */
  if (fc_gethostname(hostname, sizeof(hostname)) == 0) {
    notify_conn(dest, nullptr, E_CONNECTION, ftc_any,
                _("Welcome to the %s Server running at %s port %d."),
                freeciv_name_version(), hostname, srvarg.port);
  } else {
    notify_conn(dest, nullptr, E_CONNECTION, ftc_any,
                _("Welcome to the %s Server at port %d."),
                freeciv_name_version(), srvarg.port);
  }

  /* FIXME: this (getting messages about others logging on) should be
   * a message option for the client with event */

  /* Notify the console that you're here. */
  log_normal(_("%s has connected from %s."), pconn->username, pconn->addr);

  if (srvarg.fcdb_enabled) {
    script_fcdb_call("conn_established", pconn);
  }

  conn_compression_freeze(pconn);
  send_rulesets(dest);
  send_server_setting_control(pconn);
  send_server_settings(dest);
  send_scenario_info(dest);
  send_scenario_description(dest);
  send_game_info(dest);
  topo_packet.topology_id = wld.map.topology_id;
  topo_packet.wrap_id = wld.map.wrap_id;
  send_packet_set_topology(pconn, &topo_packet);

  /* Do we have a player that a delegate is currently controlling? */
  if ((pplayer = player_by_user_delegated(pconn->username))) {
    /* Reassert our control over the player. */
    struct connection *pdelegate;

    fc_assert_ret(player_delegation_get(pplayer) != nullptr);
    pdelegate = conn_by_user(player_delegation_get(pplayer));

    if (pdelegate && connection_delegate_restore(pdelegate)) {
      /* Delegate now detached from our player. We will restore control
       * over them as normal below. */
      notify_conn(pconn->self, nullptr, E_CONNECTION, ftc_server,
                  _("Your delegate %s was controlling your player '%s'; "
                    "now detached."), pdelegate->username,
                  player_name(pplayer));
      notify_conn(pdelegate->self, nullptr, E_CONNECTION, ftc_server,
                  _("%s reconnected, ending your delegated control of "
                    "player '%s'."), pconn->username, player_name(pplayer));
    } else {
      fc_assert(pdelegate);
      /* This really shouldn't happen. */
      log_error("Failed to revoke delegate %s's control of %s, so owner %s "
                "can't regain control.", pdelegate->username,
                player_name(pplayer), pconn->username);
      notify_conn(dest, nullptr, E_CONNECTION, ftc_server,
                  _("Couldn't get control of '%s' from delegation to %s."),
                  player_name(pplayer), pdelegate->username);
      delegation_error = TRUE;
      pplayer = nullptr;
    }
  }

  if (!delegation_error) {
    if ((pplayer = player_by_user(pconn->username))
        && connection_attach_real(pconn, pplayer, FALSE, TRUE)) {
      /* A player has already been created for this user, reconnect */

      if (S_S_INITIAL == server_state()) {
        send_player_info_c(nullptr, dest);
      }
    } else {
      int total_free_weight = 0;

      players_iterate(wplayer) {
        if (wplayer->autoselect_weight > 0
            && !wplayer->is_connected
            && !player_has_flag(wplayer, PLRF_SCENARIO_RESERVED)) {
          total_free_weight += wplayer->autoselect_weight;
        }
      } players_iterate_end;

      if (total_free_weight > 0) {
        int weight_rand = fc_rand(total_free_weight);

        players_iterate(wplayer) {
          if (wplayer->autoselect_weight > 0
              && !wplayer->is_connected
              && !player_has_flag(wplayer, PLRF_SCENARIO_RESERVED)) {
            weight_rand -= wplayer->autoselect_weight;
            if (weight_rand < 0) {
              log_normal("Trying to attach %s to %s",
                         pconn->username, wplayer->name);
              if (!connection_attach_real(pconn, wplayer, FALSE, TRUE)) {
                notify_conn(dest, nullptr, E_CONNECTION, ftc_server,
                            _("Couldn't attach your connection to a scenario player."));
                log_verbose("%s is not attached to a player", pconn->username);
              }
              break;
            }
          }
        } players_iterate_end;
      } else if (!game_was_started()) {
        if (connection_attach_real(pconn, nullptr, FALSE, TRUE)) {
          pplayer = conn_get_player(pconn);
          fc_assert(pplayer != nullptr);
        } else {
          notify_conn(dest, nullptr, E_CONNECTION, ftc_server,
                      _("Couldn't attach your connection to new player."));
          log_verbose("%s is not attached to a player", pconn->username);
        }
      }
      send_player_info_c(nullptr, dest);
    }
  }

  send_conn_info(game.est_connections, dest);

  if (pplayer == nullptr) {
    /* Else this has already been done in connection_attach_real(). */
    send_pending_events(pconn, TRUE);
    send_running_votes(pconn, FALSE);
    restore_access_level(pconn);
    send_conn_info(dest, game.est_connections);

    notify_conn(dest, nullptr, E_CONNECTION, ftc_server,
                _("You are logged in as '%s' connected to no player."),
                pconn->username);
  } else {
    notify_conn(dest, nullptr, E_CONNECTION, ftc_server,
                _("You are logged in as '%s' connected to %s."),
                pconn->username,
                player_name(pconn->playing));
  }

  /* Send information about delegation(s). */
  send_delegation_info(pconn);

  /* Notify the *other* established connections that you are connected, and
   * add the info for all in event cache. Note we must to do it after we
   * sent the pending events to pconn (from this function and also
   * connection_attach()), otherwise pconn will receive it too. */
  if (conn_controls_player(pconn)) {
    if (game.server.ip_hide) {
      package_event(&connect_info, nullptr, E_CONNECTION, ftc_server,
                    _("%s has connected (player %s)."),
                    pconn->username,
                    player_name(conn_get_player(pconn)));
    } else {
      package_event(&connect_info, nullptr, E_CONNECTION, ftc_server,
                    _("%s has connected from %s (player %s)."),
                    pconn->username, pconn->addr,
                    player_name(conn_get_player(pconn)));
    }
  } else {
    if (game.server.ip_hide) {
      package_event(&connect_info, nullptr, E_CONNECTION, ftc_server,
                    _("%s has connected."),
                    pconn->username);
    } else {
      package_event(&connect_info, nullptr, E_CONNECTION, ftc_server,
                    _("%s has connected from %s."),
                    pconn->username, pconn->addr);
    }
  }
  conn_list_iterate(game.est_connections, aconn) {
    if (aconn != pconn) {
      send_packet_chat_msg(aconn, &connect_info);
    }
  } conn_list_iterate_end;
  event_cache_add_for_all(&connect_info);

  /* If need be, tell who we're waiting on to end the game.info.turn */
  if (S_S_RUNNING == server_state() && game.server.turnblock) {
    players_iterate_alive(cplayer) {
      if (is_human(cplayer)
          && !cplayer->phase_done
          && cplayer != pconn->playing) {  /* skip current player */
        notify_conn(dest, nullptr, E_CONNECTION, ftc_any,
                    _("Turn-blocking game play: "
                      "waiting on %s to finish turn..."),
                    player_name(cplayer));
      }
    } players_iterate_alive_end;
  }

  if (game.info.is_edit_mode) {
    notify_conn(dest, nullptr, E_SETTING, ftc_editor,
                _(" *** Server is in edit mode. *** "));
  }

  if (pplayer != nullptr) {
    /* Else, no need to do anything. */
    reset_all_start_commands(TRUE);
    (void) send_server_info_to_metaserver(META_INFO);
  }

  send_current_history_report(pconn->self);

  conn_compression_thaw(pconn);
}

/**********************************************************************//**
  send the rejection packet to the client.
**************************************************************************/
void reject_new_connection(const char *msg, struct connection *pconn)
{
  struct packet_server_join_reply packet;

  /* Zero out the password */
  memset(pconn->server.password, 0, sizeof(pconn->server.password));

  packet.you_can_join = FALSE;
  sz_strlcpy(packet.capability, our_capability);
  sz_strlcpy(packet.message, msg);
  packet.challenge_file[0] = '\0';
  packet.conn_id = -1;
  send_packet_server_join_reply(pconn, &packet);
  log_normal(_("Client rejected: %s."), conn_description(pconn));
  flush_connection_send_buffer_all(pconn);
}

/**********************************************************************//**
  Returns FALSE if the clients gets rejected and the connection should be
  closed. Returns TRUE if the client get accepted.
**************************************************************************/
bool handle_login_request(struct connection *pconn,
                          struct packet_server_join_req *req)
{
  char msg[MAX_LEN_MSG];
  int kick_time_remaining;

  if (pconn->established || pconn->server.status != AS_NOT_ESTABLISHED) {
    /* We read the PACKET_SERVER_JOIN_REQ twice from this connection,
     * this is probably not a Freeciv client. */
    return FALSE;
  }

  log_normal(_("Connection request from %s from %s"),
             req->username, pconn->addr);

  /* Print server and client capabilities to console */
  log_normal(_("%s has client version %d.%d.%d%s"),
             pconn->username, req->major_version, req->minor_version,
             req->patch_version, req->version_label);
  log_verbose("Client caps: %s", req->capability);
  log_verbose("Server caps: %s", our_capability);
  conn_set_capability(pconn, req->capability);

  /* Make sure the server has every capability the client needs */
  if (!has_capabilities(our_capability, req->capability)) {
    fc_snprintf(msg, sizeof(msg),
                _("The client is missing a capability that this server needs.\n"
                   "Server version: %d.%d.%d%s Client version: %d.%d.%d%s."
                   "  Upgrading may help!"),
                MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION, VERSION_LABEL,
                req->major_version, req->minor_version,
                req->patch_version, req->version_label);
    reject_new_connection(msg, pconn);
    log_normal(_("%s was rejected: Mismatched capabilities."),
               req->username);
    return FALSE;
  }

  /* Make sure the client has every capability the server needs */
  if (!has_capabilities(req->capability, our_capability)) {
    fc_snprintf(msg, sizeof(msg),
                _("The server is missing a capability that the client needs.\n"
                   "Server version: %d.%d.%d%s Client version: %d.%d.%d%s."
                   "  Upgrading may help!"),
                MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION, VERSION_LABEL,
                req->major_version, req->minor_version,
                req->patch_version, req->version_label);
    reject_new_connection(msg, pconn);
    log_normal(_("%s was rejected: Mismatched capabilities."),
               req->username);
    return FALSE;
  }

  {
    /* Client is compatible. That includes ability to receive server info.
     * Send it. */
    struct packet_server_info info;

    info.major_version = MAJOR_VERSION;
    info.minor_version = MINOR_VERSION;
    info.patch_version = PATCH_VERSION;
#ifdef EMERGENCY_VERSION
    info.emerg_version = EMERGENCY_VERSION;
#else
    info.emerg_version = 0;
#endif
    sz_strlcpy(info.version_label, VERSION_LABEL);
    send_packet_server_info(pconn, &info);
  }

  remove_leading_trailing_spaces(req->username);

  /* Name-sanity check: could add more checks? */
  if (!is_valid_username(req->username)) {
    fc_snprintf(msg, sizeof(msg), _("Invalid username '%s'"), req->username);
    reject_new_connection(msg, pconn);
    log_normal(_("%s was rejected: Invalid name [%s]."),
               req->username, pconn->addr);
    return FALSE;
  }

  if (conn_is_kicked(pconn, &kick_time_remaining)) {
    fc_snprintf(msg, sizeof(msg), _("You have been kicked from this server "
                                    "and cannot reconnect for %d seconds."),
                kick_time_remaining);
    reject_new_connection(msg, pconn);
    log_normal(_("%s was rejected: Connection kicked "
                 "(%d seconds remaining)."),
               req->username, kick_time_remaining);
    return FALSE;
  }

  /* Don't allow duplicate logins */
  conn_list_iterate(game.all_connections, aconn) {
    if (fc_strcasecmp(req->username, aconn->username) == 0) {
      fc_snprintf(msg, sizeof(msg), _("'%s' already connected."),
                  req->username);
      reject_new_connection(msg, pconn);
      log_normal(_("%s was rejected: Duplicate login name [%s]."),
                 req->username, pconn->addr);
      return FALSE;
    }
  } conn_list_iterate_end;

  /* Remove the ping timeout given in sernet.c:server_make_connection(). */
  fc_assert_msg(1 == timer_list_size(pconn->server.ping_timers),
                "Ping timer list size %d, should be 1. Have we sent "
                "a ping to unestablished connection %s?",
                timer_list_size(pconn->server.ping_timers),
                conn_description(pconn));
  timer_list_pop_front(pconn->server.ping_timers);

  if (game.server.connectmsg[0] != '\0') {
    log_debug("Sending connectmsg: %s", game.server.connectmsg);
    dsend_packet_connect_msg(pconn, game.server.connectmsg);
  }

  if (srvarg.auth_enabled) {
    return auth_user(pconn, req->username);
  } else {
    sz_strlcpy(pconn->username, req->username);
    establish_new_connection(pconn);
    return TRUE;
  }
}

/**********************************************************************//**
  High-level server stuff when connection to client is closed or lost.
  Reports loss to log, and to other players if the connection was a
  player. Also removes player in pregame, applies auto_toggle, and
  does check for turn done (since can depend on connection/ai status).
  Note you shouldn't this function directly. You should use
  server_break_connection() if you want to close the connection.
**************************************************************************/
void lost_connection_to_client(struct connection *pconn)
{
  const char *desc = conn_description(pconn);

  fc_assert_ret(pconn->server.is_closing);

  log_normal(_("Lost connection: %s."), desc);

  /* Special color (white on black) for player loss */
  notify_conn(game.est_connections, nullptr, E_CONNECTION,
              conn_controls_player(pconn) ? ftc_player_lost : ftc_server,
              _("Lost connection: %s."), desc);

  connection_detach(pconn, TRUE);
  send_conn_info_remove(pconn->self, game.est_connections);
  notify_if_first_access_level_is_available();

  check_for_full_turn_done();
}

/**********************************************************************//**
  Fill in packet_conn_info from full connection struct.
**************************************************************************/
static void package_conn_info(struct connection *pconn,
                              struct packet_conn_info *packet)
{
  packet->id           = pconn->id;
  packet->used         = pconn->used;
  packet->established  = pconn->established;
  packet->player_num   = (pconn->playing != nullptr)
                         ? player_number(pconn->playing)
                         : player_slot_count();
  packet->observer     = pconn->observer;
  packet->access_level = pconn->access_level;

  sz_strlcpy(packet->username, pconn->username);
  if (game.server.ip_hide) {
    sz_strlcpy(packet->addr, Q_("?IP:Hidden"));
  } else {
    sz_strlcpy(packet->addr, pconn->addr);
  }
  sz_strlcpy(packet->capability, pconn->capability);
}

/**********************************************************************//**
  Handle both send_conn_info() and send_conn_info_removed(), depending
  on 'remove' arg.  Sends conn_info packets for 'src' to 'dest', turning
  off 'used' if 'remove' is specified.
**************************************************************************/
static void send_conn_info_arg(struct conn_list *src,
                               struct conn_list *dest, bool remove_conn)
{
  struct packet_conn_info packet;

  if (!dest) {
    dest = game.est_connections;
  }

  conn_list_iterate(src, psrc) {
    package_conn_info(psrc, &packet);
    if (remove_conn) {
      packet.used = FALSE;
    }
    lsend_packet_conn_info(dest, &packet);
  } conn_list_iterate_end;
}

/**********************************************************************//**
  Send conn_info packets to tell 'dest' connections all about
  'src' connections.
**************************************************************************/
void send_conn_info(struct conn_list *src, struct conn_list *dest)
{
  send_conn_info_arg(src, dest, FALSE);
}

/**********************************************************************//**
  Like send_conn_info(), but turn off the 'used' bits to tell clients
  to remove info about these connections instead of adding it.
**************************************************************************/
void send_conn_info_remove(struct conn_list *src, struct conn_list *dest)
{
  send_conn_info_arg(src, dest, TRUE);
}

/**********************************************************************//**
  Search for first uncontrolled player
**************************************************************************/
struct player *find_uncontrolled_player(void)
{
  players_iterate(played) {
    if (!played->is_connected && !played->was_created) {
      return played;
    }
  } players_iterate_end;

  return nullptr;
}

/**********************************************************************//**
  Setup pconn as a client connected to pplayer or observer:
  Updates pconn->playing, pplayer->connections, pplayer->is_connected
  and pconn->observer.

  - If pplayer is nullptr and observing is FALSE: take the next available
    player that is not connected.
  - If pplayer is nullptr and observing is TRUE: attach this connection to
    the game as global observer.
  - If pplayer is not nullptr and observing is FALSE: take this player.
  - If pplayer is not nullptr and observing is TRUE: observe this player.

  Note take_command() needs to know if this function will success before
       it's time to call this. Keep take_command() checks in sync when
       modifying this.
**************************************************************************/
static bool connection_attach_real(struct connection *pconn,
                                   struct player *pplayer,
                                   bool observing, bool connecting)
{
  fc_assert_ret_val(pconn != nullptr, FALSE);
  fc_assert_ret_val_msg(!pconn->observer && pconn->playing == nullptr, FALSE,
                        "connections must be detached with "
                        "connection_detach() before calling this!");

  if (!observing) {
    if (pplayer == nullptr) {
      /* Search for uncontrolled player */
      pplayer = find_uncontrolled_player();

      if (pplayer == nullptr) {
        /* No uncontrolled player found */
        if (player_count() >= game.server.max_players
            || normal_player_count() >= server.playable_nations) {
          return FALSE;
        }
        /* Add new player, or not */
        /* Should only be called in such a way as to create a new player
         * in the pregame */
        fc_assert_ret_val(!game_was_started(), FALSE);
        pplayer = server_create_player(-1, default_ai_type_name(),
                                       nullptr, FALSE);
        /* Pregame => no need to assign_player_colors() */
        if (!pplayer) {
          return FALSE;
        }
      } else {
        team_remove_player(pplayer);
      }
      server_player_init(pplayer, FALSE, TRUE);

      /* Make it human! */
      set_as_human(pplayer);
    }

    sz_strlcpy(pplayer->username, pconn->username);
    pplayer->unassigned_user = FALSE;
    pplayer->user_turns = 0; /* Reset for a new user */
    pplayer->is_connected = TRUE;

    if (!game_was_started()) {
      if (!pplayer->was_created && pplayer->nation == nullptr) {
        /* Temporarily set player_name() to username. */
        server_player_set_name(pplayer, pconn->username);
      }
      (void) aifill(game.info.aifill);
    }

    if (game.server.auto_ai_toggle && !is_human(pplayer)) {
      toggle_ai_player_direct(nullptr, pplayer);
    }

    send_player_info_c(pplayer, game.est_connections);

    /* Remove from global observers list, if was there */
    conn_list_remove(game.glob_observers, pconn);
  } else if (pplayer == nullptr) {
    /* Global observer */
    bool already = FALSE;

    fc_assert(observing);

    conn_list_iterate(game.glob_observers, pconn2) {
      if (pconn2 == pconn) {
        already = TRUE;
        break;
      }
    } conn_list_iterate_end;

    if (!already) {
      conn_list_append(game.glob_observers, pconn);
    }
  }

  /* We don't want the connection's username on another player. */
  players_iterate(aplayer) {
    if (aplayer != pplayer
        && !fc_strncmp(aplayer->username, pconn->username, MAX_LEN_NAME)) {
      sz_strlcpy(aplayer->username, _(ANON_USER_NAME));
      aplayer->unassigned_user = TRUE;
      send_player_info_c(aplayer, nullptr);
    }
  } players_iterate_end;

  pconn->observer = observing;
  pconn->playing = pplayer;
  if (pplayer) {
    conn_list_append(pplayer->connections, pconn);
  }

  restore_access_level(pconn);

  /* Reset the delta-state. */
  send_conn_info(pconn->self, game.est_connections);    /* Client side. */
  conn_reset_delta_state(pconn);                        /* Server side. */

  /* Initial packets don't need to be resent.  See comment for
   * connecthand.c::establish_new_connection(). */
  switch (server_state()) {
  case S_S_INITIAL:
    send_pending_events(pconn, connecting);
    send_running_votes(pconn, !connecting);
    break;

  case S_S_RUNNING:
    conn_compression_freeze(pconn);
    send_all_info(pconn->self);
    if (game.info.is_edit_mode && can_conn_edit(pconn)) {
      edithand_send_initial_packets(pconn->self);
    }
    conn_compression_thaw(pconn);
    /* Enter C_S_RUNNING client state. */
    dsend_packet_start_phase(pconn, game.info.phase);
    /* Must be after C_S_RUNNING client state to be effective. */
    send_diplomatic_meetings(pconn);
    send_pending_events(pconn, connecting);
    send_running_votes(pconn, !connecting);
    break;

  case S_S_OVER:
    conn_compression_freeze(pconn);
    send_all_info(pconn->self);
    if (game.info.is_edit_mode && can_conn_edit(pconn)) {
      edithand_send_initial_packets(pconn->self);
    }
    conn_compression_thaw(pconn);
    report_final_scores(pconn->self);
    send_pending_events(pconn, connecting);
    send_running_votes(pconn, !connecting);
    if (!connecting) {
      /* Send information about delegation(s). */
      send_delegation_info(pconn);
    }
    break;
  }

  send_updated_vote_totals(nullptr);

  return TRUE;
}

/**********************************************************************//**
  Setup pconn as a client connected to pplayer or observer.
**************************************************************************/
bool connection_attach(struct connection *pconn, struct player *pplayer,
                       bool observing)
{
  return connection_attach_real(pconn, pplayer, observing, FALSE);
}

/**********************************************************************//**
  Remove pconn as a client connected to pplayer:
  Updates pconn->playing, pconn->playing->connections,
  pconn->playing->is_connected and pconn->observer.

  pconn remains a member of game.est_connections.

  If remove_unused_player is TRUE, may remove a player left with no
  controlling connection (only in pregame, and not if explicitly /created).
**************************************************************************/
void connection_detach(struct connection *pconn, bool remove_unused_player)
{
  struct player *pplayer;

  fc_assert_ret(pconn != nullptr);

  if ((pplayer = pconn->playing) != nullptr) {
    bool was_connected = pplayer->is_connected;

    send_remove_team_votes(pconn);
    conn_list_remove(pplayer->connections, pconn);
    pconn->playing = nullptr;
    pconn->observer = FALSE;
    restore_access_level(pconn);
    cancel_connection_votes(pconn);
    send_updated_vote_totals(nullptr);
    send_conn_info(pconn->self, game.est_connections);

    /* If any other (non-observing) conn is attached to this player, the
     * player is still connected. */
    pplayer->is_connected = FALSE;
    conn_list_iterate(pplayer->connections, aconn) {
      if (!aconn->observer) {
        pplayer->is_connected = TRUE;
        break;
      }
    } conn_list_iterate_end;

    if (was_connected && !pplayer->is_connected) {
      /* Player just lost its controlling connection. */
      if (remove_unused_player
          && !pplayer->was_created && !game_was_started()) {
        /* Remove player. */
        conn_list_iterate(pplayer->connections, aconn) {
          /* Detach all. */
          fc_assert_action(aconn != pconn, continue);
          notify_conn(aconn->self, nullptr, E_CONNECTION, ftc_server,
                      _("Detaching from %s."), player_name(pplayer));
          /* Recursive... but shouldn't be a problem, as this can only
           * be a non-controlling connection so can't get back here. */
          connection_detach(aconn, TRUE);
        } conn_list_iterate_end;

        /* Actually do the removal. */
        server_remove_player(pplayer);
        (void) aifill(game.info.aifill);
        reset_all_start_commands(TRUE);
      } else {
        /* Aitoggle the player if no longer connected. */
        if (game.server.auto_ai_toggle && is_human(pplayer)) {
          toggle_ai_player_direct(nullptr, pplayer);
          /* send_player_info_c() was formerly updated by
           * toggle_ai_player_direct(), so it must be safe to send here now?
           *
           * At other times, data from send_conn_info() is used by the
           * client to display player information.
           * See establish_new_connection().
           */
          log_verbose("connection_detach() calls send_player_info_c()");
          send_player_info_c(pplayer, nullptr);

          reset_all_start_commands(TRUE);
        }
      }
    }
  } else {
    pconn->observer = FALSE;
    restore_access_level(pconn);
    send_conn_info(pconn->self, game.est_connections);
  }
}

/**********************************************************************//**
  Use a delegation to get control over another player.
**************************************************************************/
bool connection_delegate_take(struct connection *pconn,
                              struct player *dplayer)
{
  fc_assert_ret_val(!pconn->server.delegation.status, FALSE);

  /* Save the original player of this connection and the original username of
   * the player. */
  pconn->server.delegation.status = TRUE;
  pconn->server.delegation.playing = conn_get_player(pconn);
  pconn->server.delegation.observer = pconn->observer;
  if (conn_controls_player(pconn)) {
    /* Setting orig_username in the player we're about to put aside is
     * a flag that no-one should be allowed to mess with it (e.g. /take). */
    struct player *oplayer = conn_get_player(pconn);

    fc_assert_ret_val(oplayer != dplayer, FALSE);
    fc_assert_ret_val(strlen(oplayer->server.orig_username) == 0, FALSE);
    sz_strlcpy(oplayer->server.orig_username, oplayer->username);
  }
  fc_assert_ret_val(strlen(dplayer->server.orig_username) == 0, FALSE);
  sz_strlcpy(dplayer->server.orig_username, dplayer->username);

  /* Detach the current connection. */
  if (pconn->playing != nullptr || pconn->observer) {
    connection_detach(pconn, FALSE);
  }

  /* Try to attach to the new player */
  if (!connection_attach(pconn, dplayer, FALSE)) {

    /* Restore original connection. */
#ifndef FREECIV_NDEBUG
    bool success =
#endif
      connection_attach(pconn,
                        pconn->server.delegation.playing,
                        pconn->server.delegation.observer);
    fc_assert_ret_val(success, FALSE);

    /* Reset all changes done above. */
    pconn->server.delegation.status = FALSE;
    pconn->server.delegation.playing = nullptr;
    pconn->server.delegation.observer = FALSE;
    if (conn_controls_player(pconn)) {
      struct player *oplayer = conn_get_player(pconn);
      oplayer->server.orig_username[0] = '\0';
    }
    dplayer->server.orig_username[0] = '\0';

    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Restore the original status of a delegate connection pconn after potentially
  using a delegation. pconn is detached from the delegated player, and
  reattached to its previous view (e.g. observer), if any.
  (Reattaching the original user to the delegated player is not handled here.)
**************************************************************************/
bool connection_delegate_restore(struct connection *pconn)
{
  struct player *dplayer;

  if (!pconn->server.delegation.status) {
    return FALSE;
  }

  if (pconn->server.delegation.playing
      && !pconn->server.delegation.observer) {
    /* If restoring to controlling another player, and we're not the
     * original controller of that player, something's gone wrong. */
    fc_assert_ret_val(
        strcmp(pconn->server.delegation.playing->server.orig_username,
               pconn->username) == 0, FALSE);
  }

  /* Save the current (delegated) player. */
  dplayer = conn_get_player(pconn);

  /* There should be a delegated player connected to pconn. */
  fc_assert_ret_val(dplayer, FALSE);

  /* Detach the current (delegate) connection from the delegated player. */
  if (pconn->playing != nullptr || pconn->observer) {
    connection_detach(pconn, FALSE);
  }

  /* Try to attach to the delegate's original player */
  if ((pconn->server.delegation.playing != nullptr
      || pconn->server.delegation.observer)
      && !connection_attach(pconn, pconn->server.delegation.playing,
                            pconn->server.delegation.observer)) {
    return FALSE;
  }

  /* Reset data. */
  pconn->server.delegation.status = FALSE;
  pconn->server.delegation.playing = nullptr;
  pconn->server.delegation.observer = FALSE;
  if (conn_controls_player(pconn) && conn_get_player(pconn) != nullptr) {
    /* Remove flag that we had 'put aside' our original player. */
    struct player *oplayer = conn_get_player(pconn);

    fc_assert_ret_val(oplayer != dplayer, FALSE);
    oplayer->server.orig_username[0] = '\0';
  }

  /* Restore the username of the original controller in the previously-
   * delegated player. */
  sz_strlcpy(dplayer->username, dplayer->server.orig_username);
  dplayer->server.orig_username[0] = '\0';
  /* Send updated username to all connections. */
  send_player_info_c(dplayer, nullptr);

  return TRUE;
}

/**********************************************************************//**
  Close a connection. Use this in the server to take care of delegation stuff
  (reset the username of the controlled connection).
**************************************************************************/
void connection_close_server(struct connection *pconn, const char *reason)
{
  /* Restore possible delegations before the connection is closed. */
  connection_delegate_restore(pconn);
  connection_close(pconn, reason);
}

/**********************************************************************//**
  Handle client's request to sync via packet serial number.
**************************************************************************/
void handle_sync_serial(struct connection *conn, int serial)
{
  dsend_packet_sync_serial_reply(conn, serial);
}
