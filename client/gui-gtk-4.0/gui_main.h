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
#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

#include <gtk/gtk.h>

/* client */
#include "gui_main_g.h"
#include "options.h"


/* Mac uses "command"/"meta" key for menu accelerator modifier key */
#ifdef __APPLE__
#define ACCL_MOD_KEY "<Meta>"
#define ACCL_MOD_MASK GDK_META_MASK
#else
#define ACCL_MOD_KEY "<ctrl>"
#define ACCL_MOD_MASK GDK_CONTROL_MASK
#endif

#define GUI_NAME_FULL "gui-gtk-4.0"
#define GUI_NAME_SHORT "gtk4"

#define GUI_GTK_OPTION(optname) gui_options.gui_gtk4_##optname
#define GUI_GTK_OPTION_STR(optname) "gui_gtk4_" #optname
#define GUI_GTK_DEFAULT_THEME_NAME FC_GTK4_DEFAULT_THEME_NAME

void main_message_area_resize(void *data);
void animation_idle_cb(void *data);

/* Network string charset conversion */
gchar *ntoh_str(const gchar *netstr);

extern PangoFontDescription *city_names_style;
extern PangoFontDescription *city_productions_style;
extern PangoFontDescription *reqtree_text_style;

#define single_tile_pixmap (mapview.single_tile->pixmap)

extern GtkTextView *    main_message_area;
extern GtkWidget *      text_scrollbar;
extern GtkWidget *      toplevel;
extern GtkWidget *      top_vbox;
extern GtkWidget *      main_frame_civ_name;
extern GtkWidget *      main_label_info;
extern GtkWidget *      econ_label[10];
extern GtkWidget *      bulb_label;
extern GtkWidget *      sun_label;
extern GtkWidget *      flake_label;
extern GtkWidget *      government_label;
extern GtkWidget *      econ_widget;
extern GtkWidget *      map_canvas;             /* GtkDrawingArea */
extern GtkWidget *      overview_canvas;        /* GtkDrawingArea */
extern GtkWidget *      overview_scrolled_window;        /* GtkScrolledWindow */
extern GtkWidget *      timeout_label;
extern GtkWidget *      turn_done_button;
extern GtkWidget *      unit_info_box;
extern GtkWidget *      unit_info_label;
extern GtkWidget *      unit_info_frame;
extern GtkWidget *      map_horizontal_scrollbar;
extern GtkWidget *      map_vertical_scrollbar;

extern GtkWidget *      toplevel_tabs;
extern GtkWidget *      top_notebook;
extern GtkWidget *      map_widget;
extern GtkWidget *      bottom_notebook;
extern GtkWidget *      right_notebook;
extern GtkTextBuffer *  message_buffer;

extern int overview_canvas_store_width;
extern int overview_canvas_store_height;

gboolean map_canvas_focus(void);

void reset_unit_table(void);
void popup_quit_dialog(void);
void quit_gtk_main(void);
void refresh_chat_buttons(void);

int screen_width(void);
int screen_height(void);
struct video_mode *resolution_request_get(void);

void fullscreen_opt_refresh(struct option *poption);

GtkApplication *gui_app(void);
bool is_gui_up(void);

gboolean terminate_signal_processing(GtkEventControllerFocus *controller,
                                     gpointer data);

gboolean fc_lost_focus(GtkEventControllerFocus *controller,
                       gpointer data);
gboolean fc_gained_focus(GtkEventControllerFocus *controller,
                         gpointer data);

void update_turn_done_tooltip(void);

#endif /* FC__GUI_MAIN_H */
