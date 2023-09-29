/***********************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Project
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

/* utility */
#include "capability.h"
#include "fc_cmdline.h"
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "registry.h"

/* common */
#include "capstr.h"
#include "connection.h"
#include "events.h"
#include "fc_cmdhelp.h"
#include "fc_interface.h"
#include "fc_types.h" /* LINE_BREAK */
#include "government.h"
#include "improvement.h"
#include "map.h"
#include "movement.h"
#include "player.h"
#include "version.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "helpdata.h"
#include "helpdlg_g.h"
#include "music.h"
#include "tilespec.h"

/* server */
#include "citytools.h"
#include "commands.h"
#include "connecthand.h"
#include "console.h"
#include "diplhand.h"
#include "gamehand.h"
#include "plrhand.h"
#include "report.h"
#include "ruleset.h"
#include "settings.h"
#include "sernet.h"
#include "srv_main.h"
#include "stdinhand.h"

/* tools/shared */
#include "tools_fc_interface.h"

/* tools/manual */
#include "fc_manual.h"


struct tag_types html_tags = {
  /* file extension */
  "html",

  /* header */
  "<html><head><link rel=\"stylesheet\" type=\"text/css\" "
  "href=\"manual.css\"/><meta http-equiv=\"Content-Type\" "
  "content=\"text/html; charset=UTF-8\"/></head><body>\n\n",

  /* title begin */
  "<h1>",

  /* title end */
  "</h1>",

  /* section title begin */
  "<h3 class='section'>",

  /* section title end */
  "</h3>",

  /* image begin */
  "<img src=\"",

  /* image end */
  ".png\">",

  /* item begin */
  "<div class='item' id='%s%d'>\n",

  /* item end */
  "</div>\n",

  /* subitem begin */
  "<pre class='%s'>",

  /* subitem end */
  "</pre>\n",

  /* tail */
  "</body></html>",

  /* horizontal line */
  "<hr/>"
};

struct tag_types wiki_tags = {
  /* file extension */
  "mediawiki",

  /* header */
  " ",

  /* title begin */
  "=",

  /* title end */
  "=",

  /* section title begin */
  "===",

  /* section title end */
  "===",

  /* image begin */
  "[[Image:",

  /* image end */
  ".png]]",

  /* item begin */
  "----\n<!-- %s %d -->\n",

  /* item end */
  "\n",

  /* subitem begin */
  "<!-- %s -->\n",

  /* subitem end */
  "\n",

  /* tail */
  " ",

  /* horizontal line */
  "----"
};


void insert_client_build_info(char *outbuf, size_t outlen);

/* Needed for "About Freeciv" help */
const char *client_string = "freeciv-manual";

static char *ruleset = NULL;

/**********************************************************************//**
  Replace html special characters ('&', '<' and '>').
**************************************************************************/
char *html_special_chars(char *str, size_t *len)
{
  char *buf;

  buf = fc_strrep_resize(str, len, "&", "&amp;");
  buf = fc_strrep_resize(buf, len, "<", "&lt;");
  buf = fc_strrep_resize(buf, len, ">", "&gt;");

  return buf;
}


/*******************************************
  Useless stubs for compiling client code.
*******************************************/

/**********************************************************************//**
  Client stub
**************************************************************************/
void popup_help_dialog_string(const char *item)
{
  /* Empty stub. */
}

/**********************************************************************//**
  Client stub
**************************************************************************/
void popdown_help_dialog(void)
{
  /* Empty stub. */
}

struct tileset *tileset;

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *tileset_name_get(struct tileset *t)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *tileset_version(struct tileset *t)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *tileset_summary(struct tileset *t)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *tileset_description(struct tileset *t)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *current_musicset_name(void)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *current_musicset_version(void)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *current_musicset_summary(void)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *current_musicset_description(void)
{
  return NULL;
}

/**********************************************************************//**
  Mostly a client stub.
**************************************************************************/
enum client_states client_state(void)
{
  return C_S_INITIAL;
}

/**********************************************************************//**
  Mostly a client stub.
**************************************************************************/
bool client_nation_is_in_current_set(const struct nation_type *pnation)
{
  /* Currently, there is no way to select a nation set for freeciv-manual.
   * Then, let's assume we want to print help for all nations. */
  return TRUE;
}

/**********************************************************************//**
  Create manual file, and do the generic header for it.

  @param tag_info       Tag set to use
  @param manual_number  Number of the manual page
  @return               Handle of the created file
**************************************************************************/
FILE *manual_start(struct tag_types *tag_info, int manual_number)
{
  char filename[40];
  FILE *doc;

  fc_snprintf(filename, sizeof(filename), "%s%d.%s",
              game.server.rulesetdir, manual_number + 1, tag_info->file_ext);

  if (!is_reg_file_for_access(filename, TRUE)
      || !(doc = fc_fopen(filename, "w"))) {
    log_error(_("Could not write manual file %s."), filename);

    return NULL;
  }

  fprintf(doc, "%s", tag_info->header);
  fprintf(doc, "<!-- Generated by freeciv-manual version %s -->\n\n",
          freeciv_datafile_version());

  return doc;
}

/**********************************************************************//**
  Generic finalizing step for a manual page. Closes the file.

  @param tag_info     Tag set to use
  @param doc          Manual handle
  @param manual_name  Name of the manual
**************************************************************************/
void manual_finalize(struct tag_types *tag_info, FILE *doc,
                     const char *manual_name)
{
  fprintf(doc, "%s", tag_info->tail);
  fclose(doc);
  log_normal(_("%s manual successfully written."), manual_name);
}

/**********************************************************************//**
  Write a server manual, then quit.
**************************************************************************/
static bool manual_command(struct tag_types *tag_info)
{
  enum manuals manuals;

  /* Reset aifill to zero */
  game.info.aifill = 0;

  if (!load_rulesets(NULL, NULL, FALSE, NULL, FALSE, FALSE, FALSE)) {
    /* Failed to load correct ruleset */
    return FALSE;
  }

  if (!manual_settings(tag_info)
      || !manual_commands(tag_info)
      || !manual_terrain(tag_info)) {
    return FALSE;
  }

  for (manuals = MANUAL_BUILDINGS; manuals < MANUAL_COUNT; manuals++) {
    char mnamebuf[20];
    FILE *doc;

    doc = manual_start(tag_info, manuals);

    if (doc == NULL) {
      return FALSE;
    }

    switch (manuals) {
    case MANUAL_SETTINGS:
    case MANUAL_COMMANDS:
    case MANUAL_TERRAIN:
      /* Should be handled in separate functions */
      fc_assert(FALSE);
      break;

    case MANUAL_BUILDINGS:
    case MANUAL_WONDERS:
      if (manuals == MANUAL_BUILDINGS) {
        /* TRANS: markup ... Freeciv version ... ruleset name ... markup */
        fprintf(doc, _("%sFreeciv %s buildings help (%s)%s\n\n"), tag_info->title_begin,
                VERSION_STRING, game.control.name, tag_info->title_end);
      } else {
        /* TRANS: markup ... Freeciv version ... ruleset name ... markup */
        fprintf(doc, _("%sFreeciv %s wonders help (%s)%s\n\n"), tag_info->title_begin,
                VERSION_STRING, game.control.name, tag_info->title_end);
      }

      fprintf(doc, "<table>\n<tr bgcolor=#9bc3d1><th colspan=2>%s</th>"
                   "<th>%s<br/>%s</th><th>%s<br/>%s</th><th>%s</th></tr>\n\n",
              _("Name"), _("Cost"), _("Upkeep"),
              _("Requirement"), _("Obsolete by"), _("More info"));

      improvement_iterate(pimprove) {
        char buf[64000];

        if (!valid_improvement(pimprove)
            || is_great_wonder(pimprove) == (manuals == MANUAL_BUILDINGS)) {
          continue;
        }

        helptext_building(buf, sizeof(buf), NULL, NULL, pimprove);

        fprintf(doc, "<tr><td>%s%s%s</td><td>%s</td>\n"
                     "<td align=\"center\"><b>%d</b><br/>%d</td>\n<td>",
                tag_info->image_begin, pimprove->graphic_str, tag_info->image_end,
                improvement_name_translation(pimprove),
                pimprove->build_cost,
                pimprove->upkeep);

        if (requirement_vector_size(&pimprove->reqs) == 0) {
          char text[512];

          strncpy(text, Q_("?req:None"), sizeof(text) - 1);
          fprintf(doc, "%s<br/>", text);
        } else {
          requirement_vector_iterate(&pimprove->reqs, req) {
            char text[512], text2[512];

            fc_snprintf(text2, sizeof(text2),
                        /* TRANS: Feature required to be absent. */
                        req->present ? "%s" : _("no %s"),
                        universal_name_translation(&req->source,
                                                   text, sizeof(text)));
            fprintf(doc, "%s<br/>", text2);
          } requirement_vector_iterate_end;
        }

        fprintf(doc, "\n%s\n", tag_info->hline);

        if (requirement_vector_size(&pimprove->obsolete_by) == 0) {
          char text[512];

          strncpy(text, Q_("?req:None"), sizeof(text) - 1);
          fprintf(doc, "<em>%s</em><br/>", text);
        } else {
          requirement_vector_iterate(&pimprove->obsolete_by, pobs) {
            char text[512], text2[512];

            fc_snprintf(text2, sizeof(text2),
                        /* TRANS: Feature required to be absent. */
                        pobs->present ? "%s" : _("no %s"),
                        universal_name_translation(&pobs->source,
                                                   text, sizeof(text)));
            fprintf(doc, "<em>%s</em><br/>", text2);
          } requirement_vector_iterate_end;
        }

        fprintf(doc,
                "</td>\n<td>%s</td>\n</tr><tr><td colspan=\"5\"><hr></td></tr>\n\n",
                buf);
      } improvement_iterate_end;

      fprintf(doc, "</table>");
      break;

    case MANUAL_GOVS:
      /* Freeciv-web uses (parts of) the government HTML output in its own
       * manual pages. */
      /* FIXME: this doesn't resemble the wiki manual at all. */
      /* TRANS: markup ... Freeciv version ... ruleset name ... markup */
      fprintf(doc, _("%sFreeciv %s governments help (%s)%s\n\n"), tag_info->title_begin,
              VERSION_STRING, game.control.name, tag_info->title_end);
      governments_iterate(pgov) {
        char buf[64000];
        fprintf(doc, tag_info->item_begin, "gov", pgov->item_number);
        fprintf(doc, "%s%s%s\n\n", tag_info->sect_title_begin,
                government_name_translation(pgov), tag_info->sect_title_end);
        fprintf(doc, tag_info->subitem_begin, "helptext");
        helptext_government(buf, sizeof(buf), NULL, NULL, pgov);
        fprintf(doc, "%s\n\n", buf);
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, "%s", tag_info->item_end);
      } governments_iterate_end;
      break;

    case MANUAL_UNITS:
      /* Freeciv-web uses (parts of) the unit type HTML output in its own
       * manual pages. */
      /* FIXME: this doesn't resemble the wiki manual at all. */
      /* TRANS: markup ... Freeciv version ... ruleset name ... markup */
      fprintf(doc, _("%sFreeciv %s unit types help (%s)%s\n\n"),
              tag_info->title_begin, VERSION_STRING, game.control.name,
              tag_info->title_end);
      unit_type_iterate(putype) {
        char buf[64000];

        fprintf(doc, tag_info->item_begin, "utype", putype->item_number);
        fprintf(doc, "%s%s%s\n\n", tag_info->sect_title_begin,
                utype_name_translation(putype), tag_info->sect_title_end);
        fprintf(doc, tag_info->subitem_begin, "cost");
        fprintf(doc,
                PL_("Cost: %d shield",
                    "Cost: %d shields",
                    utype_build_shield_cost_base(putype)),
                utype_build_shield_cost_base(putype));
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, tag_info->subitem_begin, "upkeep");
        fprintf(doc, _("Upkeep: %s"),
                helptext_unit_upkeep_str(putype));
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, tag_info->subitem_begin, "moves");
        fprintf(doc, _("Moves: %s"),
                move_points_text(putype->move_rate, TRUE));
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, tag_info->subitem_begin, "vision");
        fprintf(doc, _("Vision: %d"),
                (int)sqrt((double)putype->vision_radius_sq));
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, tag_info->subitem_begin, "attack");
        fprintf(doc, _("Attack: %d"),
                putype->attack_strength);
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, tag_info->subitem_begin, "defense");
        fprintf(doc, _("Defense: %d"),
                putype->defense_strength);
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, tag_info->subitem_begin, "firepower");
        fprintf(doc, _("Firepower: %d"),
                putype->firepower);
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, tag_info->subitem_begin, "hitpoints");
        fprintf(doc, _("Hitpoints: %d"),
                putype->hp);
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, tag_info->subitem_begin, "obsolete");
        fprintf(doc, _("Obsolete by: %s"),
                U_NOT_OBSOLETED == putype->obsoleted_by ?
                  Q_("?utype:None") :
                  utype_name_translation(putype->obsoleted_by));
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, tag_info->subitem_begin, "helptext");
        helptext_unit(buf, sizeof(buf), NULL, "", putype);
        fprintf(doc, "%s", buf);
        fprintf(doc, "%s", tag_info->subitem_end);
        fprintf(doc, "%s", tag_info->item_end);
      } unit_type_iterate_end;
      break;

    case MANUAL_TECHS:
      /* FIXME: this doesn't resemble the wiki manual at all. */
      /* TRANS: markup ... Freeciv version ... ruleset name ... markup */
      fprintf(doc, _("%sFreeciv %s tech help (%s)%s\n\n"),
              tag_info->title_begin, VERSION_STRING, game.control.name,
              tag_info->title_end);
      advance_iterate(ptech) {
        if (valid_advance(ptech)) {
          char buf[64000];

          fprintf(doc, tag_info->item_begin, "tech", ptech->item_number);
          fprintf(doc, "%s%s%s\n\n", tag_info->sect_title_begin,
                  advance_name_translation(ptech), tag_info->sect_title_end);

          fprintf(doc, tag_info->subitem_begin, "helptext");
          helptext_advance(buf, sizeof(buf), NULL, "", ptech->item_number);
          fprintf(doc, "%s", buf);
          fprintf(doc, "%s", tag_info->subitem_end);

          fprintf(doc, "%s", tag_info->item_end);
        }
      } advance_iterate_end;
      break;

    case MANUAL_COUNT:
      break;

    } /* switch */

    fc_snprintf(mnamebuf, sizeof(mnamebuf), "%d", manuals + 1);

    manual_finalize(tag_info, doc, mnamebuf);
  } /* manuals */

  return TRUE;
}

/**********************************************************************//**
  Entry point of whole freeciv-manual program
**************************************************************************/
int main(int argc, char **argv)
{
  int inx;
  bool showhelp = FALSE;
  bool showvers = FALSE;
  char *option = NULL;
  int retval = EXIT_SUCCESS;
  struct tag_types *tag_info = &html_tags;

  /* Initialize the fc_interface functions needed to generate the help
   * text.
   * fc_interface_init_tool() includes low level support like
   * guaranteeing that fc_vsnprintf() will work after it,
   * so this need to be early. */
  fc_interface_init_tool();

  registry_module_init();
  init_character_encodings(FC_DEFAULT_DATA_ENCODING, FALSE);

  /* Set the default log level. */
  srvarg.loglevel = LOG_NORMAL;

  /* parse command-line arguments... */
  inx = 1;
  while (inx < argc) {
    if ((option = get_option_malloc("--ruleset", argv, &inx, argc, TRUE))) {
      if (ruleset != NULL) {
        fc_fprintf(stderr, _("Multiple rulesets requested. Only one "
                             "ruleset at a time is supported.\n"));
      } else {
        ruleset = option;
      }
    } else if (is_option("--help", argv[inx])) {
      showhelp = TRUE;
      break;
    } else if (is_option("--version", argv[inx])) {
      showvers = TRUE;
    } else if ((option = get_option_malloc("--log", argv, &inx, argc, TRUE))) {
      srvarg.log_filename = option;
    } else if (is_option("--wiki", argv[inx])) {
      tag_info = &wiki_tags;
#ifndef FREECIV_NDEBUG
    } else if (is_option("--Fatal", argv[inx])) {
      if (inx + 1 >= argc || '-' == argv[inx + 1][0]) {
        srvarg.fatal_assertions = SIGABRT;
      } else if (str_to_int(argv[inx + 1], &srvarg.fatal_assertions)) {
        inx++;
      } else {
        fc_fprintf(stderr, _("Invalid signal number \"%s\".\n"),
                   argv[inx + 1]);
        inx++;
        showhelp = TRUE;
      }
#endif /* FREECIV_NDEBUG */
    } else if ((option = get_option_malloc("--debug", argv, &inx, argc, FALSE))) {
      if (!log_parse_level_str(option, &srvarg.loglevel)) {
        showhelp = TRUE;
        break;
      }
      free(option);
    } else {
      fc_fprintf(stderr, _("Unrecognized option: \"%s\"\n"), argv[inx]);
      exit(EXIT_FAILURE);
    }
    inx++;
  }

  init_our_capability();

  /* must be before con_log_init() */
  init_connections();
  con_log_init(srvarg.log_filename, srvarg.loglevel,
               srvarg.fatal_assertions);
  /* logging available after this point */

  /* Get common code to treat us as a tool. */
  i_am_tool();

  /* Initialize game with default values */
  game_init(FALSE);

  /* Set ruleset user requested in to use */
  if (ruleset != NULL) {
    sz_strlcpy(game.server.rulesetdir, ruleset);
  }

  settings_init(FALSE);

  if (showvers && !showhelp) {
    fc_fprintf(stderr, "%s \n", freeciv_name_version());
    exit(EXIT_SUCCESS);
  } else if (showhelp) {
    struct cmdhelp *help = cmdhelp_new(argv[0]);

#ifdef FREECIV_DEBUG
    cmdhelp_add(help, "d",
                /* TRANS: "debug" is exactly what user must type, do not translate. */
                _("debug LEVEL"),
                _("Set debug log level (one of f,e,w,n,v,d, or "
                  "d:file1,min,max:...)"));
#else  /* FREECIV_DEBUG */
    cmdhelp_add(help, "d",
                /* TRANS: "debug" is exactly what user must type, do not translate. */
                _("debug LEVEL"),
                _("Set debug log level (one of f,e,w,n,v)"));
#endif /* FREECIV_DEBUG */
#ifndef FREECIV_NDEBUG
    cmdhelp_add(help, "F",
                /* TRANS: "Fatal" is exactly what user must type, do not translate. */
                _("Fatal [SIGNAL]"),
                _("Raise a signal on failed assertion"));
#endif /* FREECIV_NDEBUG */
    cmdhelp_add(help, "h", "help",
                _("Print a summary of the options"));
    cmdhelp_add(help, "l",
                  /* TRANS: "log" is exactly what user must type, do not translate. */
                _("log FILE"),
                _("Use FILE as logfile"));
    cmdhelp_add(help, "r",
                  /* TRANS: "ruleset" is exactly what user must type, do not translate. */
                _("ruleset RULESET"),
                _("Make manual for RULESET"));
    cmdhelp_add(help, "v", "version",
                _("Print the version number"));
    cmdhelp_add(help, "w", "wiki",
                _("Write manual in wiki format"));

    /* The function below prints a header and footer for the options.
     * Furthermore, the options are sorted. */
    cmdhelp_display(help, TRUE, FALSE, TRUE);
    cmdhelp_destroy(help);

    exit(EXIT_SUCCESS);
  }

  if (!manual_command(tag_info)) {
    retval = EXIT_FAILURE;
  }

  con_log_close();
  registry_module_close();
  libfreeciv_free();
  cmdline_option_values_free();

  return retval;
}

/**********************************************************************//**
  Empty function required by helpdata
**************************************************************************/
void insert_client_build_info(char *outbuf, size_t outlen)
{
  /* Nothing here */
}