/*
 * gtk GUI functions
 *
 * This file is part of ANT (Ant is Not a Telephone)
 *
 * Copyright 2002, 2003 Roland Stigge
 * Copyright 2007 Ivan Schreter
 *
 * ANT is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * ANT is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ANT; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* own header files */
#include "session.h"

/*!
 * @brief Create a dialog window with a (big) label and an ok button to close.
 *
 * This is primarily good for displaying a note to the user.
 *
 * @note Caller has to show the window himself with gtk_widget_show()
 *       and maybe want to make it modal with
 *       gtk_window_set_modal(GTK_WINDOW(window), TRUE).
 *
 * @param title dialog title.
 * @param contents message to display.
 * @param justification justification of label (e.g. GTK_JUSTIFY_LEFT).
 * @return newly-created dialog (caller has to free it).
 */
GtkWidget *ok_dialog_get(char *title, char *contents,
			 GtkJustification justification);

/*!
 * @brief Display a note about audio devices not available.
 */
void show_audio_error_dialog(void);

/*!
 * @brief Main function for gtk GUI.
 *
 * @param session session to run.
 * @return int to be returned from main().
 */
int main_gtk(session_t *session);
