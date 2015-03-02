/* ui_series_cb.c
 *
 * Part of amide - Amide's a Medical Image Dataset Examiner
 * Copyright (C) 2001 Andy Loening
 *
 * Author: Andy Loening <loening@ucla.edu>
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

#include "config.h"
#include <gnome.h>
#include "study.h"
#include "ui_common.h"
#include "amitk_threshold.h"
#include "ui_series.h"
#include "ui_series_cb.h"

/* function called by the adjustment in charge for scrolling */
void ui_series_cb_scroll_change(GtkAdjustment* adjustment, gpointer data) {

  ui_series_t * ui_series = data;

  if (ui_series->type == PLANES)
    ui_series->view_point.z = adjustment->value;
  else
    ui_series->view_frame = adjustment->value;

  ui_common_place_cursor(UI_CURSOR_WAIT, GTK_WIDGET(ui_series->canvas));
  ui_series_update_canvas(ui_series);
  ui_common_remove_cursor(GTK_WIDGET(ui_series->canvas));

  return;
}

/* function to save the series as an external data format */
void ui_series_cb_export(GtkWidget * widget, gpointer data) {
  
  //  ui_series_t * ui_series = data;

  /* this function would require being able to transform a canvas back into
     a single image/pixbuf/etc.... don't know how to do this yet */

  return;
}


/* function called when the threshold is changed */
static void ui_series_cb_threshold_changed(GtkWidget * widget, gpointer data) {
  ui_series_t * ui_series=data;
  ui_series_update_canvas(ui_series);
  return;
}

/* function called when the volume's color is changed */
static void ui_series_cb_color_changed(GtkWidget * widget, gpointer data) {
  ui_series_t * ui_series=data;
  ui_series_update_canvas(ui_series);
  return;
}

static gboolean ui_series_cb_thresholds_close(GtkWidget* widget, gpointer data) {
  ui_series_t * ui_series = data;

  /* just keeping track on whether or not the threshold widget is up */
  ui_series->thresholds_dialog = NULL;

  return FALSE;
}

/* function called when we hit the threshold button */
void ui_series_cb_threshold(GtkWidget * widget, gpointer data) {

  ui_series_t * ui_series = data;

  if (ui_series->thresholds_dialog != NULL)
    return;


  ui_common_place_cursor(UI_CURSOR_WAIT, GTK_WIDGET(ui_series->canvas));

  ui_series->thresholds_dialog = amitk_thresholds_dialog_new(ui_series->volumes);
  gtk_signal_connect(GTK_OBJECT(ui_series->thresholds_dialog), "threshold_changed",
		     GTK_SIGNAL_FUNC(ui_series_cb_threshold_changed), ui_series);
  gtk_signal_connect(GTK_OBJECT(ui_series->thresholds_dialog), "color_changed",
		     GTK_SIGNAL_FUNC(ui_series_cb_color_changed), ui_series);
  gtk_signal_connect(GTK_OBJECT(ui_series->thresholds_dialog), "close",
		     GTK_SIGNAL_FUNC(ui_series_cb_thresholds_close), ui_series);
  gtk_widget_show(ui_series->thresholds_dialog);

  ui_common_remove_cursor(GTK_WIDGET(ui_series->canvas));

  return;
}



/* function ran when closing a series window */
void ui_series_cb_close(GtkWidget* widget, gpointer data) {

  ui_series_t * ui_series = data;
  GtkWidget * app = GTK_WIDGET(ui_series->app);

  /* delete the widget */
  ui_series_cb_delete_event(app, NULL, ui_series);
  gtk_widget_destroy(app);

  return;
}

/* function to run for a delete_event */
gboolean ui_series_cb_delete_event(GtkWidget* widget, GdkEvent * event, gpointer data) {

  ui_series_t * ui_series = data;
  GtkWidget * app = GTK_WIDGET(ui_series->app);

  /* make sure our threshold dialog is gone */
  if (ui_series->thresholds_dialog != NULL) {
    gtk_widget_destroy(ui_series->thresholds_dialog);
    ui_series->thresholds_dialog = NULL;
  }

  /* free the associated data structure */
  ui_series = ui_series_free(ui_series);

  /* tell the rest of the program this window is no longer here */
  amide_unregister_window((gpointer) app);

  return FALSE;
}






