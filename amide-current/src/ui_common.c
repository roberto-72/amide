/* ui_common.c
 *
 * Part of amide - Amide's a Medical Image Dataset Examiner
 * Copyright (C) 2001-2003 Andy Loening
 *
 * Author: Andy Loening <loening@alum.mit.edu>
 */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
  02111-1307, USA.
*/

#include "amide_config.h"
#include <sys/stat.h>
#include <string.h>
#include "amide.h"
#include "ui_common.h"
#include "amitk_space.h"
#include "amitk_xif_sel.h"
#include "pixmaps.h"
#include "amitk_color_table.h"
#include "amitk_preferences.h"
#include "amitk_threshold.h"
#ifdef AMIDE_LIBGSL_SUPPORT
#include <gsl/gsl_version.h>
#endif
#ifdef AMIDE_LIBVOLPACK_SUPPORT
#include <volpack.h>
#endif
#ifdef AMIDE_LIBMDC_SUPPORT
#include <medcon.h>
#endif
#ifdef AMIDE_LIBFAME_SUPPORT
#include <fame_version.h>
#endif

#define AXIS_WIDTH 120
#define AXIS_HEADER 20
#define AXIS_MARGIN 10
#define ORTHOGONAL_AXIS_HEIGHT 100
#define LINEAR_AXIS_HEIGHT 140
#define AXIS_TEXT_MARGIN 10
#define AXIS_ARROW_LENGTH 8
#define AXIS_ARROW_EDGE 7
#define AXIS_ARROW_WIDTH 6

/* our help menu... */
GnomeUIInfo ui_common_help_menu[]= {
  GNOMEUIINFO_HELP(PACKAGE),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_MENU_ABOUT_ITEM(ui_common_about_cb, NULL), 
  GNOMEUIINFO_END
};


/* an array to hold the preloaded cursors */
GdkCursor * ui_common_cursor[NUM_CURSORS];

/* internal variables */
static gboolean ui_common_cursors_initialized = FALSE;
static gchar * last_path_used=NULL;
static ui_common_cursor_t current_cursor;

static gchar * line_style_names[] = {
  N_("Solid"),
  N_("On/Off"),
  N_("Double Dash")
};




/* this function's use is a bit of a cludge 
   GTK typically uses %f for changing a float to text to display in a table
   Here we overwrite the typical conversion with a %g conversion
 */
void amitk_real_cell_data_func(GtkTreeViewColumn *tree_column,
			       GtkCellRenderer *cell,
			       GtkTreeModel *tree_model,
			       GtkTreeIter *iter,
			       gpointer data) {

  gdouble value;
  gchar *text;
  gint column = GPOINTER_TO_INT(data);

  /* Get the double value from the model. */
  gtk_tree_model_get (tree_model, iter, column, &value, -1);

  /* Now we can format the value ourselves. */
  text = g_strdup_printf ("%g", value);
  g_object_set (cell, "text", text, NULL);
  g_free (text);

  return;
}




/* returns TRUE for OK */
gboolean ui_common_check_filename(const gchar * filename) {

  if ((strcmp(filename, ".") == 0) ||
      (strcmp(filename, "..") == 0) ||
      (strcmp(filename, "") == 0) ||
      (strcmp(filename, "\\") == 0) ||
      (strcmp(filename, "/") == 0)) {
    return FALSE;
  } else
    return TRUE;
}



static gchar * save_name_common(GtkWidget * file_selection, const gchar * filename) {

  struct stat file_info;
  GtkWidget * question;
  gint return_val;

  /* sanity checks */
  if (!ui_common_check_filename(filename)) {
    g_warning(_("Inappropriate filename: %s"),filename);
    return NULL;
  }

  if (last_path_used != NULL) g_free(last_path_used);
  last_path_used = g_strdup(filename);


  /* check with user if filename already exists */
  if (stat(filename, &file_info) == 0) {
    /* check if it's okay to writeover the file */
    question = gtk_message_dialog_new(GTK_WINDOW(file_selection),
				      GTK_DIALOG_DESTROY_WITH_PARENT,
				      GTK_MESSAGE_QUESTION,
				      GTK_BUTTONS_OK_CANCEL,
				      _("Overwrite file: %s"), filename);

    /* and wait for the question to return */
    return_val = gtk_dialog_run(GTK_DIALOG(question));

    gtk_widget_destroy(question);
    if (return_val != GTK_RESPONSE_OK) {
      return NULL; /* we don't want to overwrite the file.... */
    }
    
    /* unlinking the file doesn't occur here */
  }

  return g_strdup(filename);
}

gchar * ui_common_file_selection_get_save_name(GtkWidget * file_selection) {
  const gchar * filename;
  filename = gtk_file_selection_get_filename(GTK_FILE_SELECTION(file_selection));
  return save_name_common(file_selection, filename);
}

gchar * ui_common_xif_selection_get_save_name(GtkWidget * xif_selection) {
  const gchar * filename;
  gchar * save_filename;
  gchar * prev_filename;
  gchar ** frags1=NULL;
  gchar ** frags2=NULL;

  filename = amitk_xif_selection_get_filename(AMITK_XIF_SELECTION(xif_selection));

  /* make sure the filename ends with .xif */
  save_filename = g_strdup(filename);
  g_strreverse(save_filename);
  frags1 = g_strsplit(save_filename, ".", 2);
  g_strreverse(save_filename);
  g_strreverse(frags1[0]);
  frags2 = g_strsplit(frags1[0], G_DIR_SEPARATOR_S, -1);
  if (g_ascii_strcasecmp(frags2[0], "xif") != 0) {
    prev_filename = save_filename;
    save_filename = g_strdup_printf("%s%s",prev_filename, ".xif");
    g_free(prev_filename);
  }    
  g_strfreev(frags2);
  g_strfreev(frags1);

  prev_filename = save_filename;
  save_filename = save_name_common(xif_selection, prev_filename);
  g_free(prev_filename);

  return save_filename;
}







static gchar * load_name_common(const gchar * filename) {

  /* sanity checks */
  if (!ui_common_check_filename(filename)) {
    g_warning(_("Inappropriate filename: %s"),filename);
    return NULL;
  }

  if (last_path_used != NULL) g_free(last_path_used);
  last_path_used = g_strdup(filename);

  return g_strdup(filename);
}

gchar * ui_common_file_selection_get_load_name(GtkWidget * file_selection) {
  const gchar * filename;
  filename = gtk_file_selection_get_filename(GTK_FILE_SELECTION(file_selection));
  return load_name_common(filename);
}

gchar * ui_common_xif_selection_get_load_name(GtkWidget * xif_selection) {
  const gchar * filename;
  filename = amitk_xif_selection_get_filename(AMITK_XIF_SELECTION(xif_selection));
  return load_name_common(filename);
}





static gchar * set_filename_common(gchar * suggested_name) {

  gchar * dir_string;
  gchar * base_string;
  gchar * return_string;

  if (last_path_used != NULL)
    dir_string = g_path_get_dirname(last_path_used);
  else
    dir_string = NULL;

  if (suggested_name != NULL) {
    base_string = g_path_get_basename(suggested_name);
    return_string = g_strdup_printf("%s%s%s", dir_string, G_DIR_SEPARATOR_S, base_string);
    g_free(base_string);
  } else {
    return_string = g_strdup_printf("%s%s",dir_string, G_DIR_SEPARATOR_S);
  }

  if (dir_string != NULL) g_free(dir_string);

  return return_string;
}


void ui_common_file_selection_set_filename(GtkWidget * file_selection, gchar * suggested_name) {
  gchar * temp_string;
  temp_string = set_filename_common(suggested_name);

  if (temp_string != NULL) {
    gtk_file_selection_set_filename(GTK_FILE_SELECTION(file_selection), temp_string);
    g_free(temp_string); 
  }

  return;
}


void ui_common_xif_selection_set_filename(GtkWidget * xif_selection, gchar * suggested_name) {
  gchar * temp_string;
  temp_string = set_filename_common(suggested_name);
    
  if (temp_string != NULL) {
    amitk_xif_selection_set_filename(AMITK_XIF_SELECTION(xif_selection), temp_string);
    g_free(temp_string); 
  }

  return;
}



/* function to close a file selection widget */
void ui_common_file_selection_cancel_cb(GtkWidget* widget, gpointer data) {

  GtkFileSelection * file_selection = data;

  gtk_widget_destroy(GTK_WIDGET(file_selection));

  return;
}

/* function which brings up an about box */
void ui_common_about_cb(GtkWidget * button, gpointer data) {

  GtkWidget *about;
  GdkPixbuf * amide_logo;

  const gchar *authors[] = {
    "Andy Loening <loening@alum.mit.edu>",
    NULL
  };

  gchar * contents;


  contents = g_strjoin("", 
		       _("AMIDE's a Medical Image Data Examiner\n"),
		       "\n",
		       _("Email bug reports to: "), PACKAGE_BUGREPORT,"\n",
		       "\n",
#if (AMIDE_LIBECAT_SUPPORT || AMIDE_LIBGSL_SUPPORT || AMIDE_LIBMDC_SUPPORT || AMIDE_LIBVOLPACK_SUPPORT || AMIDE_LIBFAME_SUPPORT)
		       _("Compiled with support for the following libraries:\n"),
#endif
#ifdef AMIDE_LIBECAT_SUPPORT
		       _("libecat: CTI File library by Merence Sibomona\n"),
#endif
#ifdef AMIDE_LIBGSL_SUPPORT
		       _("libgsl: GNU Scientific Library by the GSL Team (version "),GSL_VERSION,")\n",
#endif
#ifdef AMIDE_LIBMDC_SUPPORT
		       _("libmdc: Medical Imaging File library by Erik Nolf (version "),MDC_VERSION,")\n",
#endif
#ifdef AMIDE_LIBVOLPACK_SUPPORT
		       _("libvolpack: Volume Rendering library by Philippe Lacroute (version "),VP_VERSION,")\n",
#endif
#ifdef AMIDE_LIBFAME_SUPPORT
		       _("libfame: Fast Assembly Mpeg Encoding library by the FAME Team (version "), LIBFAME_VERSION, ")\n",
#endif
		       NULL);

  amide_logo = gdk_pixbuf_new_from_xpm_data(amide_logo_xpm);

  about = gnome_about_new(PACKAGE, VERSION, 
			  _("Copyright (c) 2000-2003 Andy Loening"),
			  contents,
			  authors, NULL, NULL, amide_logo);
  g_object_unref(amide_logo);

  gtk_window_set_modal(GTK_WINDOW(about), FALSE);

  gtk_widget_show(about);

  g_free(contents);

  return;
}



void ui_common_draw_view_axis(GnomeCanvas * canvas, gint row, gint column, 
			      AmitkView view, AmitkLayout layout, 
			      gint axis_width, gint axis_height) {

  const gchar * x_axis_label;
  gdouble x_axis_label_x_location;
  gdouble x_axis_label_y_location;
  GtkAnchorType x_axis_label_anchor;
  GnomeCanvasPoints * x_axis_line_points;

  const gchar * y_axis_label;
  gdouble y_axis_label_x_location;
  gdouble y_axis_label_y_location;
  GtkAnchorType y_axis_label_anchor;
  GnomeCanvasPoints * y_axis_line_points;

  x_axis_line_points = gnome_canvas_points_new(2);
  x_axis_line_points->coords[0] = column*axis_width + AXIS_MARGIN; /* x1 */

  y_axis_line_points = gnome_canvas_points_new(2);
  y_axis_line_points->coords[0] = column*axis_width + AXIS_MARGIN; /* x1 */

  switch(view) {
  case AMITK_VIEW_CORONAL:
    /* the x axis */
    x_axis_line_points->coords[1] = row*axis_height + AXIS_HEADER; /* y1 */
    x_axis_line_points->coords[2] = column*axis_width + axis_width-AXIS_MARGIN; /* x2 */
    x_axis_line_points->coords[3] = row*axis_height + AXIS_HEADER; /* y2 */

    /* the x label */
    x_axis_label = amitk_axis_get_name(AMITK_AXIS_X);
    x_axis_label_x_location = column*axis_width + axis_width-AXIS_MARGIN-AXIS_TEXT_MARGIN;
    x_axis_label_y_location = row*axis_height + AXIS_HEADER+AXIS_TEXT_MARGIN;
    x_axis_label_anchor = GTK_ANCHOR_NORTH_EAST;
    
    /* the z axis */
    y_axis_line_points->coords[1] = row*axis_height + AXIS_HEADER; /* y1 */
    y_axis_line_points->coords[2] = column*axis_width + AXIS_MARGIN; /* x2 */
    y_axis_line_points->coords[3] = row*axis_height + axis_height-AXIS_MARGIN; /* y2 */
    
    /* the z label */
    y_axis_label = amitk_axis_get_name(AMITK_AXIS_Z);
    y_axis_label_x_location = column*axis_width + AXIS_MARGIN+AXIS_TEXT_MARGIN;
    y_axis_label_y_location = row*axis_height + axis_height-AXIS_MARGIN-AXIS_TEXT_MARGIN;
    y_axis_label_anchor = GTK_ANCHOR_NORTH_WEST;
    break;
  case AMITK_VIEW_SAGITTAL:
    /* the y axis */
      x_axis_line_points->coords[3] = row*axis_height + AXIS_HEADER; /* y2 */
    if (layout == AMITK_LAYOUT_ORTHOGONAL) {
      x_axis_line_points->coords[1] = row*axis_height + axis_height-AXIS_MARGIN; /* y1 */
      x_axis_line_points->coords[2] = column*axis_width + AXIS_MARGIN; /* x2 */
    } else { /* AMITK_LAYOUT_LINEAR */ 
      x_axis_line_points->coords[1] = row*axis_height + AXIS_HEADER; /* y1 */
      x_axis_line_points->coords[2] = column*axis_width + axis_width-AXIS_MARGIN; /* x2 */
    }
    
    /* the y label */
    x_axis_label = amitk_axis_get_name(AMITK_AXIS_Y);
    x_axis_label_y_location = row*axis_height + AXIS_HEADER+AXIS_TEXT_MARGIN;
    if (layout == AMITK_LAYOUT_ORTHOGONAL) {
      x_axis_label_x_location = column*axis_width + AXIS_MARGIN+AXIS_TEXT_MARGIN;
      x_axis_label_anchor = GTK_ANCHOR_NORTH_WEST;
    } else {/* AMITK_LAYOUT_LINEAR */
      x_axis_label_x_location = column*axis_width + axis_width-AXIS_MARGIN-AXIS_TEXT_MARGIN;
      x_axis_label_anchor = GTK_ANCHOR_NORTH_EAST;
    }
    
    /* the z axis */
    y_axis_line_points->coords[3] = row*axis_height + axis_height-AXIS_MARGIN; /* y2 */
    if (layout == AMITK_LAYOUT_ORTHOGONAL) {
      y_axis_line_points->coords[1] = row*axis_height + axis_height-AXIS_MARGIN; /* y1 */
      y_axis_line_points->coords[2] = column*axis_width + axis_width-AXIS_MARGIN; /* x2 */
    } else { /* AMITK_LAYOUT_LINEAR */ 
      y_axis_line_points->coords[1] = row*axis_height + AXIS_HEADER; /* y1 */
      y_axis_line_points->coords[2] = column*axis_width + AXIS_MARGIN; /* x2 */
    }
    
    /* the z label */
    y_axis_label = amitk_axis_get_name(AMITK_AXIS_Z);
    y_axis_label_y_location = row*axis_height + axis_height-AXIS_MARGIN-AXIS_TEXT_MARGIN;
    if (layout == AMITK_LAYOUT_ORTHOGONAL) {
      y_axis_label_x_location = column*axis_width + axis_width-AXIS_MARGIN-AXIS_TEXT_MARGIN;
      y_axis_label_anchor = GTK_ANCHOR_SOUTH_EAST;
    } else {
      y_axis_label_x_location = column*axis_width + AXIS_MARGIN+AXIS_TEXT_MARGIN;
      y_axis_label_anchor = GTK_ANCHOR_NORTH_WEST;
    }
    break;
  case AMITK_VIEW_TRANSVERSE:
  default:
    /* the x axis */
    x_axis_line_points->coords[1] = row*axis_height + axis_height-AXIS_MARGIN; /* y1 */
    x_axis_line_points->coords[2] = column*axis_width + axis_width-AXIS_MARGIN; /* x2 */
    x_axis_line_points->coords[3] = row*axis_height + axis_height-AXIS_MARGIN; /* y2 */
  
    /* the x label */
    x_axis_label = amitk_axis_get_name(AMITK_AXIS_X);
    x_axis_label_x_location = column*axis_width + axis_width-AXIS_MARGIN-AXIS_TEXT_MARGIN;
    x_axis_label_y_location = row*axis_height + axis_height-AXIS_MARGIN-AXIS_TEXT_MARGIN;
    x_axis_label_anchor = GTK_ANCHOR_SOUTH_EAST;

    /* the y axis */
    y_axis_line_points->coords[1] = row*axis_height + axis_height-AXIS_MARGIN; /* y1 */
    y_axis_line_points->coords[2] = column*axis_width + AXIS_MARGIN; /* x2 */
    y_axis_line_points->coords[3] = row*axis_height + AXIS_HEADER; /* y2 */

    /* the y label */
    y_axis_label = amitk_axis_get_name(AMITK_AXIS_Y);
    y_axis_label_x_location = column*axis_width + AXIS_MARGIN+AXIS_TEXT_MARGIN;
    y_axis_label_y_location = row*axis_height + AXIS_HEADER+AXIS_TEXT_MARGIN;
    y_axis_label_anchor = GTK_ANCHOR_NORTH_WEST;

    break;
  }

  /* the view label */
  gnome_canvas_item_new(gnome_canvas_root(canvas), gnome_canvas_text_get_type(),
			"anchor", GTK_ANCHOR_NORTH, "text", view_names[view],
			"x", (gdouble) (column+0.5)*axis_width,
			"y", (gdouble) (row+0.5)*axis_height, 
			"fill_color", "black", 
			"font_desc", amitk_fixed_font_desc, NULL);

  /* the x axis */
  gnome_canvas_item_new(gnome_canvas_root(canvas), gnome_canvas_line_get_type(),
			"points", x_axis_line_points, "fill_color", "black",
			"width_pixels", 3, "last_arrowhead", TRUE, 
			"arrow_shape_a", (gdouble) AXIS_ARROW_LENGTH,
			"arrow_shape_b", (gdouble) AXIS_ARROW_EDGE,
			"arrow_shape_c", (gdouble) AXIS_ARROW_WIDTH,
			NULL);

  /* the x label */
  gnome_canvas_item_new(gnome_canvas_root(canvas), gnome_canvas_text_get_type(),
			"anchor", x_axis_label_anchor,"text", x_axis_label,
			"x", x_axis_label_x_location, "y", x_axis_label_y_location,
			"fill_color", "black", "font_desc", amitk_fixed_font_desc, NULL); 

  /* the y axis */
  gnome_canvas_item_new(gnome_canvas_root(canvas), gnome_canvas_line_get_type(),
			"points", y_axis_line_points, "fill_color", "black",
			"width_pixels", 3, "last_arrowhead", TRUE, 
			"arrow_shape_a", (gdouble) AXIS_ARROW_LENGTH,
			"arrow_shape_b", (gdouble) AXIS_ARROW_EDGE,
			"arrow_shape_c", (gdouble) AXIS_ARROW_WIDTH,
			NULL);

  gnome_canvas_points_unref(x_axis_line_points);
  gnome_canvas_points_unref(y_axis_line_points);

  /* the y label */
  gnome_canvas_item_new(gnome_canvas_root(canvas),gnome_canvas_text_get_type(),
			"anchor", y_axis_label_anchor, "text", y_axis_label,
			"x", y_axis_label_x_location,"y", y_axis_label_y_location,
			"fill_color", "black", "font_desc", amitk_fixed_font_desc, NULL); 

  return;
}


void ui_common_data_set_preferences_widgets(GtkWidget * packing_table,
					    gint table_row,
					    GtkWidget * window_spins[AMITK_WINDOW_NUM][AMITK_LIMIT_NUM]) {

  AmitkWindow i_window;
  AmitkLimit i_limit;
  GtkWidget * label;

  for (i_limit = 0; i_limit < AMITK_LIMIT_NUM; i_limit++) {
    label  = gtk_label_new(limit_names[i_limit]);
    gtk_table_attach(GTK_TABLE(packing_table), label, 1+i_limit,2+i_limit, 
		     table_row, table_row+1, GTK_FILL, 0, X_PADDING, Y_PADDING);
    gtk_widget_show(label);
  }
  table_row++;

  for (i_window = 0; i_window < AMITK_WINDOW_NUM; i_window++) {
    label = gtk_label_new(window_names[i_window]);
    gtk_table_attach(GTK_TABLE(packing_table), label, 0,1, 
		     table_row, table_row+1, GTK_FILL, 0, X_PADDING, Y_PADDING);
    gtk_widget_show(label);
      
    for (i_limit = 0; i_limit < AMITK_LIMIT_NUM; i_limit++) {
      
      window_spins[i_window][i_limit] = gtk_spin_button_new_with_range(-G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
      gtk_spin_button_set_digits(GTK_SPIN_BUTTON(window_spins[i_window][i_limit]), AMITK_THRESHOLD_SPIN_BUTTON_DIGITS);
      gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(window_spins[i_window][i_limit]), FALSE);
      g_object_set_data(G_OBJECT(window_spins[i_window][i_limit]), "which_window", GINT_TO_POINTER(i_window));
      g_object_set_data(G_OBJECT(window_spins[i_window][i_limit]), "which_limit", GINT_TO_POINTER(i_limit));
      gtk_table_attach(GTK_TABLE(packing_table), window_spins[i_window][i_limit], 1+i_limit,2+i_limit, 
		       table_row, table_row+1, GTK_FILL, 0, X_PADDING, Y_PADDING);
      gtk_widget_show(window_spins[i_window][i_limit]);
    }
    table_row++;
  }


  return;
}


void ui_common_study_preferences_widgets(GtkWidget * packing_table,
					 gint table_row,
					 GtkWidget ** pspin_button,
					 GnomeCanvasItem ** proi_item,
					 GtkWidget ** pline_style_menu,
					 GtkWidget ** playout_button1,
					 GtkWidget ** playout_button2,
					 GtkWidget ** pmaintain_size_button,
					 GtkWidget ** ptarget_size_spin) {

  GtkWidget * label;
  GtkObject * adjustment;
  GtkWidget * roi_canvas;
  GnomeCanvasPoints * roi_line_points;
  rgba_t outline_color;
  GdkPixbuf * pixbuf;
  GtkWidget * image;
  GtkWidget * hseparator;
  GtkWidget * menu;
#ifndef AMIDE_LIBGNOMECANVAS_AA
  GtkWidget * menuitem;
  GdkLineStyle i_line_style;
#endif


  /* widgets to change the roi's size */
  label = gtk_label_new(_("ROI Width (pixels)"));
  gtk_table_attach(GTK_TABLE(packing_table), label, 
		   0,1, table_row, table_row+1,
		   0, 0, X_PADDING, Y_PADDING);
  gtk_widget_show(label);

  adjustment = gtk_adjustment_new(AMITK_PREFERENCES_MIN_ROI_WIDTH,
				  AMITK_PREFERENCES_MIN_ROI_WIDTH,
				  AMITK_PREFERENCES_MAX_ROI_WIDTH,1.0, 1.0, 1.0);
  *pspin_button = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1.0, 0);
  gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(*pspin_button),FALSE);
  gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(*pspin_button), TRUE);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(*pspin_button), TRUE);
  gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(*pspin_button), GTK_UPDATE_ALWAYS);
  gtk_table_attach(GTK_TABLE(packing_table), *pspin_button, 1,2, 
		   table_row, table_row+1, GTK_FILL, 0, X_PADDING, Y_PADDING);
  gtk_widget_show(*pspin_button);

  /* a little canvas indicator thingie to show the user who the new preferences will look */
#ifdef AMIDE_LIBGNOMECANVAS_AA
  roi_canvas = gnome_canvas_new_aa();
#else
  roi_canvas = gnome_canvas_new();
#endif
  gtk_widget_set_size_request(roi_canvas, 100, 100);
  gnome_canvas_set_scroll_region(GNOME_CANVAS(roi_canvas), 0.0, 0.0, 100.0, 100.0);
  gtk_table_attach(GTK_TABLE(packing_table),  roi_canvas, 2,3,table_row,table_row+2,
		   GTK_FILL, 0,  X_PADDING, Y_PADDING);
  gtk_widget_show(roi_canvas);

  /* the box */
  roi_line_points = gnome_canvas_points_new(5);
  roi_line_points->coords[0] = 25.0; /* x1 */
  roi_line_points->coords[1] = 25.0; /* y1 */
  roi_line_points->coords[2] = 75.0; /* x2 */
  roi_line_points->coords[3] = 25.0; /* y2 */
  roi_line_points->coords[4] = 75.0; /* x3 */
  roi_line_points->coords[5] = 75.0; /* y3 */
  roi_line_points->coords[6] = 25.0; /* x4 */
  roi_line_points->coords[7] = 75.0; /* y4 */
  roi_line_points->coords[8] = 25.0; /* x4 */
  roi_line_points->coords[9] = 25.0; /* y4 */

  outline_color = amitk_color_table_outline_color(AMITK_COLOR_TABLE_BW_LINEAR, TRUE);
  *proi_item = gnome_canvas_item_new(gnome_canvas_root(GNOME_CANVAS(roi_canvas)), 
				     gnome_canvas_line_get_type(),
				     "points", roi_line_points, 
				     "fill_color_rgba", amitk_color_table_rgba_to_uint32(outline_color), 
				     NULL);
  gnome_canvas_points_unref(roi_line_points);
  table_row++;


#ifndef AMIDE_LIBGNOMECANVAS_AA
  /* widgets to change the roi's line style */
  /* Anti-aliased canvas doesn't yet support this */
  /* also need to remove #ifndef for relevant lines in amitk_canvas_object.c */
  label = gtk_label_new(_("ROI Line Style:"));
  gtk_table_attach(GTK_TABLE(packing_table), label, 0,1,
  		   table_row, table_row+1, 0, 0, X_PADDING, Y_PADDING);
  gtk_widget_show(label);
  
  menu = gtk_menu_new();
  for (i_line_style=0; i_line_style<=GDK_LINE_DOUBLE_DASH; i_line_style++) {
    menuitem = gtk_menu_item_new_with_label(line_style_names[i_line_style]);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    g_object_set_data(G_OBJECT(menuitem), "line_style", GINT_TO_POINTER(i_line_style)); 
    gtk_widget_show(menuitem);
  }
  
  *pline_style_menu = gtk_option_menu_new();
  gtk_option_menu_set_menu(GTK_OPTION_MENU(*pline_style_menu), menu);
  gtk_widget_show(menu);

  gtk_widget_set_size_request (*pline_style_menu, 125, -1);
  gtk_table_attach(GTK_TABLE(packing_table),  *pline_style_menu, 1,2, 
  		   table_row,table_row+1, GTK_FILL, 0,  X_PADDING, Y_PADDING);
  gtk_widget_show(*pline_style_menu);
  table_row++;
#endif


  hseparator = gtk_hseparator_new();
  gtk_table_attach(GTK_TABLE(packing_table), hseparator, 
		   0, 3, table_row, table_row+1,
		   GTK_FILL, 0, X_PADDING, Y_PADDING);
  table_row++;
  gtk_widget_show(hseparator);


  label = gtk_label_new(_("Canvas Layout:"));
  gtk_table_attach(GTK_TABLE(packing_table), label, 
		   0,1, table_row, table_row+1,
		   0, 0, X_PADDING, Y_PADDING);
  gtk_widget_show(label);

  /* the radio buttons */
  *playout_button1 = gtk_radio_button_new(NULL);
  pixbuf = gdk_pixbuf_new_from_xpm_data(linear_layout_xpm);
  image = gtk_image_new_from_pixbuf(pixbuf);
  g_object_unref(pixbuf);
  gtk_container_add(GTK_CONTAINER(*playout_button1), image);
  gtk_widget_show(image);
  gtk_table_attach(GTK_TABLE(packing_table), *playout_button1,
  		   1,2, table_row, table_row+1,
  		   0, 0, X_PADDING, Y_PADDING);
  g_object_set_data(G_OBJECT(*playout_button1), "layout", GINT_TO_POINTER(AMITK_LAYOUT_LINEAR));
  gtk_widget_show(*playout_button1);

  *playout_button2 = gtk_radio_button_new(NULL);
  gtk_radio_button_set_group(GTK_RADIO_BUTTON(*playout_button2), 
			     gtk_radio_button_get_group(GTK_RADIO_BUTTON(*playout_button1)));
  pixbuf = gdk_pixbuf_new_from_xpm_data(orthogonal_layout_xpm);
  image = gtk_image_new_from_pixbuf(pixbuf);
  g_object_unref(pixbuf);
  gtk_container_add(GTK_CONTAINER(*playout_button2), image);
  gtk_widget_show(image);
  gtk_table_attach(GTK_TABLE(packing_table), *playout_button2, 2,3, table_row, table_row+1,
  		   0, 0, X_PADDING, Y_PADDING);
  g_object_set_data(G_OBJECT(*playout_button2), "layout", GINT_TO_POINTER(AMITK_LAYOUT_ORTHOGONAL));
  gtk_widget_show(*playout_button2);

  table_row++;


  /* do we want the size of the canvas to not resize */
  label = gtk_label_new(_("Maintain view size constant:"));
  gtk_table_attach(GTK_TABLE(packing_table), label, 
		   0,1, table_row, table_row+1, 0, 0, X_PADDING, Y_PADDING);
  gtk_widget_show(label);

  *pmaintain_size_button = gtk_check_button_new();
  gtk_table_attach(GTK_TABLE(packing_table), *pmaintain_size_button, 
		   1,2, table_row, table_row+1, 0, 0, X_PADDING, Y_PADDING);
  gtk_widget_show(*pmaintain_size_button);
  table_row++;


  /* widgets to change the amount of empty space in the center of the target */
  label = gtk_label_new(_("Target Empty Area (pixels)"));
  gtk_table_attach(GTK_TABLE(packing_table), label, 
		   0,1, table_row, table_row+1, 0, 0, X_PADDING, Y_PADDING);
  gtk_widget_show(label);

  adjustment = gtk_adjustment_new(AMITK_PREFERENCES_MIN_TARGET_EMPTY_AREA, 
				  AMITK_PREFERENCES_MIN_TARGET_EMPTY_AREA, 
				  AMITK_PREFERENCES_MAX_TARGET_EMPTY_AREA,1.0, 1.0, 1.0);
  *ptarget_size_spin = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1.0, 0);
  gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(*ptarget_size_spin),FALSE);
  gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(*ptarget_size_spin), TRUE);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(*ptarget_size_spin), TRUE);
  gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(*ptarget_size_spin), GTK_UPDATE_ALWAYS);

  gtk_table_attach(GTK_TABLE(packing_table), *ptarget_size_spin, 1,2, 
		   table_row, table_row+1, GTK_FILL, 0, X_PADDING, Y_PADDING);
  gtk_widget_show(*ptarget_size_spin);


  return;
}

GtkWidget * ui_common_create_view_axis_indicator(AmitkLayout layout) {

  GtkWidget * axis_indicator;
  AmitkView i_view;

#ifdef AMIDE_LIBGNOMECANVAS_AA
  axis_indicator = gnome_canvas_new_aa();
#else
  axis_indicator = gnome_canvas_new();
#endif
  switch(layout) {
  case AMITK_LAYOUT_ORTHOGONAL:
    gtk_widget_set_size_request(axis_indicator, 2.0*AXIS_WIDTH, 2.0*ORTHOGONAL_AXIS_HEIGHT);
    gnome_canvas_set_scroll_region(GNOME_CANVAS(axis_indicator), 0.0, 0.0, 
				   2.0*AXIS_WIDTH, 2.0*ORTHOGONAL_AXIS_HEIGHT);
    ui_common_draw_view_axis(GNOME_CANVAS(axis_indicator), 0, 0, AMITK_VIEW_TRANSVERSE, 
			     layout, AXIS_WIDTH, ORTHOGONAL_AXIS_HEIGHT);
    ui_common_draw_view_axis(GNOME_CANVAS(axis_indicator), 1, 0, AMITK_VIEW_CORONAL, 
			     layout, AXIS_WIDTH, ORTHOGONAL_AXIS_HEIGHT);
    ui_common_draw_view_axis(GNOME_CANVAS(axis_indicator), 0, 1, AMITK_VIEW_SAGITTAL, 
			     layout, AXIS_WIDTH, ORTHOGONAL_AXIS_HEIGHT);

    break;
  case AMITK_LAYOUT_LINEAR:
  default:
    gtk_widget_set_size_request(axis_indicator, 3.0*AXIS_WIDTH, LINEAR_AXIS_HEIGHT);
    gnome_canvas_set_scroll_region(GNOME_CANVAS(axis_indicator), 0.0, 0.0, 
				   3.0*AXIS_WIDTH, LINEAR_AXIS_HEIGHT);
    for (i_view=0;i_view< AMITK_VIEW_NUM;i_view++)
      ui_common_draw_view_axis(GNOME_CANVAS(axis_indicator), 0, i_view, i_view,
			       layout, AXIS_WIDTH, LINEAR_AXIS_HEIGHT);
    break;
  }


  return GTK_WIDGET(axis_indicator);
}



/* This data is in X bitmap format, and can be created with the 'bitmap' utility. */
#define small_dot_width 3
#define small_dot_height 3
static unsigned char small_dot_bits[] = {0x00, 0x02, 0x00};
 

/* load in the cursors */
static void ui_common_cursor_init(void) {

  GdkPixmap *source, *mask;
  GdkColor fg = { 0, 0, 0, 0 }; /* black. */
  GdkColor bg = { 0, 0, 0, 0 }; /* black */
  GdkCursor * small_dot;
 
  source = gdk_bitmap_create_from_data(NULL, small_dot_bits, small_dot_width, small_dot_height);
  mask = gdk_bitmap_create_from_data(NULL, small_dot_bits, small_dot_width, small_dot_height);
  small_dot = gdk_cursor_new_from_pixmap (source, mask, &fg, &bg, 2,2);
  gdk_pixmap_unref (source);
  gdk_pixmap_unref (mask);
                                     
  /* load in the cursors */

  ui_common_cursor[UI_CURSOR_DEFAULT] = NULL;
  ui_common_cursor[UI_CURSOR_ROI_MODE] =  gdk_cursor_new(GDK_DRAFT_SMALL);
  ui_common_cursor[UI_CURSOR_ROI_RESIZE] = small_dot; /* was GDK_SIZING */
  ui_common_cursor[UI_CURSOR_ROI_ROTATE] = small_dot; /* was GDK_EXCHANGE */
  ui_common_cursor[UI_CURSOR_OBJECT_SHIFT] = small_dot; /* was GDK_FLEUR */
  ui_common_cursor[UI_CURSOR_ROI_ISOCONTOUR] = gdk_cursor_new(GDK_DRAFT_SMALL);
  ui_common_cursor[UI_CURSOR_ROI_ERASE] = gdk_cursor_new(GDK_DRAFT_SMALL);
  ui_common_cursor[UI_CURSOR_DATA_SET_MODE] = gdk_cursor_new(GDK_CROSSHAIR);
  ui_common_cursor[UI_CURSOR_FIDUCIAL_MARK_MODE] = gdk_cursor_new(GDK_DRAFT_SMALL);
  ui_common_cursor[UI_CURSOR_RENDERING_ROTATE_XY] = gdk_cursor_new(GDK_FLEUR);
  ui_common_cursor[UI_CURSOR_RENDERING_ROTATE_Z] = gdk_cursor_new(GDK_EXCHANGE);
  ui_common_cursor[UI_CURSOR_WAIT] = gdk_cursor_new(GDK_WATCH);
  

 
  ui_common_cursors_initialized = TRUE;
  return;
}


/* callback to add the window's icon when the window gets realized */
void ui_common_window_realize_cb(GtkWidget * widget, gpointer data) {

  GdkPixbuf * pixbuf;

  g_return_if_fail(GTK_IS_WINDOW(widget));

  pixbuf = gdk_pixbuf_new_from_xpm_data(amide_logo_xpm);
  gtk_window_set_icon(GTK_WINDOW(widget), pixbuf);
  g_object_unref(pixbuf);

  return;
}


/* replaces the current cursor with the specified cursor */
void ui_common_place_cursor_no_wait(ui_common_cursor_t which_cursor, GtkWidget * widget) {

  GdkCursor * cursor;

  /* make sure we have cursors */
  if (!ui_common_cursors_initialized) ui_common_cursor_init();

  /* sanity checks */
  if (widget == NULL) return;
  if (!GTK_WIDGET_REALIZED(widget)) return;

  if (which_cursor != UI_CURSOR_WAIT)
    current_cursor = which_cursor;
  
  cursor = ui_common_cursor[which_cursor];

  gdk_window_set_cursor(gtk_widget_get_parent_window(widget), cursor);

  return;
}

void ui_common_remove_wait_cursor(GtkWidget * widget) {

  ui_common_place_cursor_no_wait(current_cursor, widget);
}

/* replaces the current cursor with the specified cursor */
void ui_common_place_cursor(ui_common_cursor_t which_cursor, GtkWidget * widget) {

  /* call the actual function */
  ui_common_place_cursor_no_wait(which_cursor, widget);

  /* do any events pending, this allows the cursor to get displayed */
  while (gtk_events_pending()) gtk_main_iteration();

  return;
}


static void entry_activate(GtkEntry * entry, gpointer data) {

  GtkWidget * dialog = data;
  gchar ** return_str_ptr;

  return_str_ptr = g_object_get_data(G_OBJECT(dialog), "return_str_ptr");
  if(*return_str_ptr != NULL) g_free(*return_str_ptr);
  *return_str_ptr= g_strdup(gtk_entry_get_text(entry));

  gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK,
				    strlen(*return_str_ptr));
  return;
}

static void init_response_cb (GtkDialog * dialog, gint response_id, gpointer data) {
  
  gint return_val;

  switch(response_id) {
  case GTK_RESPONSE_OK:
  case GTK_RESPONSE_CLOSE:
    g_signal_emit_by_name(G_OBJECT(dialog), "delete_event", NULL, &return_val);
    if (!return_val) gtk_widget_destroy(GTK_WIDGET(dialog));
    break;

  default:
    break;
  }

  return;
}


/* function to setup a dialog to allow us to choice options for rendering */
GtkWidget * ui_common_entry_dialog(GtkWindow * parent, gchar * prompt, gchar **return_str_ptr) {
  
  GtkWidget * dialog;
  GtkWidget * table;
  guint table_row;
  GtkWidget * entry;
  GtkWidget * label;
  GtkWidget * image;
  

  dialog = gtk_dialog_new_with_buttons (_("Request Dialog"),  parent,
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CLOSE, 
					GTK_STOCK_OK, GTK_RESPONSE_OK,
					NULL);
  g_object_set_data(G_OBJECT(dialog), "return_str_ptr", return_str_ptr);
  g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(init_response_cb), NULL);
  gtk_container_set_border_width(GTK_CONTAINER(dialog), 10);
  gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK,FALSE);


  table = gtk_table_new(3,2,FALSE);
  table_row=0;
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);


  image = gtk_image_new_from_stock(GTK_STOCK_DIALOG_QUESTION,GTK_ICON_SIZE_DIALOG);
  gtk_table_attach(GTK_TABLE(table), image, 
		   0,1, table_row, table_row+1, X_PACKING_OPTIONS, 0, X_PADDING, Y_PADDING);

  label = gtk_label_new(prompt);
  gtk_table_attach(GTK_TABLE(table), label, 
		   1,2, table_row, table_row+1, X_PACKING_OPTIONS, 0, X_PADDING, Y_PADDING);
  table_row++;


  entry = gtk_entry_new();
  gtk_table_attach(GTK_TABLE(table), entry, 
		   1,2, table_row, table_row+1, GTK_FILL, 0, X_PADDING, Y_PADDING);
  g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(entry_activate), dialog);
  table_row++;


  gtk_widget_show_all(dialog);

  return dialog;
}





