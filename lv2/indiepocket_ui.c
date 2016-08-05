/* Copyright (C) 2016 Henrik Hedelund.

   This file is part of IndiePocket.

   IndiePocket is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   IndiePocket is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with IndiePocket.  If not, see <http://www.gnu.org/licenses/>. */

/* Standard headers.  */
#include <stdlib.h>
#include <unistd.h>

/* LV2 headers.  */
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>

/* GTK+ headers.  */
#include <gtk/gtk.h>

/* IndiePocket headers.  */
#include "indiepocket_io.h"
#include "../pckt/gtk2/dial.h"

typedef struct
{
  LV2_Atom_Forge forge;
  LV2_URID_Map *map;
  IPIOURIs uris;
  LV2UI_Write_Function write;
  LV2UI_Controller controller;
  GtkWidget *root;
  GtkWidget *notebook;
  GtkWidget *name_box;
  GtkWidget *tune_box;
  GtkWidget *damp_box;
  GtkWidget *expr_box;
  GtkWidget *button;
  GtkWidget *statusbar;
} IndiePocketUI;

typedef struct
{
  LV2_URID uri;
  int8_t drum;
} DrumProperty;

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

#define DRUM_PROPERTY_KEY "drum_property"

static void clear_drum_controls (IndiePocketUI *ui);

/* Load kit button click callback.  */
static void
on_file_selected (GtkWidget *widget, void *handle)
{
  IndiePocketUI *ui = (IndiePocketUI *) handle;
  char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));

  gtk_widget_set_sensitive (widget, FALSE);

  uint8_t forgebuf[IPIO_FORGE_BUFFER_SIZE];
  lv2_atom_forge_set_buffer (&ui->forge, forgebuf, IPIO_FORGE_BUFFER_SIZE);
  LV2_Atom *kitmsg = ipio_forge_kit_file_atom (&ui->forge, &ui->uris,
                                               filename);

  ui->write (ui->controller, IPIO_CONTROL, lv2_atom_total_size (kitmsg),
             ui->uris.atom_eventTransfer, kitmsg);

  g_free (filename);
}

/* Drum control change callback.  */
static void
on_drum_prop_changed (GtkRange *range, void *handle)
{
  IndiePocketUI *ui = (IndiePocketUI *) handle;
  float value = (float) gtk_range_get_value (range);
  DrumProperty *prop = g_object_get_data (G_OBJECT (range), DRUM_PROPERTY_KEY);
  if (!prop)
    return;

  uint8_t forgebuf[IPIO_FORGE_BUFFER_SIZE];
  lv2_atom_forge_set_buffer (&ui->forge, forgebuf, IPIO_FORGE_BUFFER_SIZE);
  LV2_Atom *msg = ipio_write_drum_property (&ui->forge, &ui->uris, prop->drum,
                                            prop->uri, value);

  ui->write (ui->controller, IPIO_CONTROL, lv2_atom_total_size (msg),
             ui->uris.atom_eventTransfer, msg);
}

/* Set up UI.  */
static LV2UI_Handle
instantiate (const LV2UI_Descriptor *descriptor, const char *plugin_uri,
             const char *bundle_path, LV2UI_Write_Function write_function,
             LV2UI_Controller controller, LV2UI_Widget *widget,
             const LV2_Feature * const *features)
{
  (void) descriptor;
  (void) plugin_uri;

  IndiePocketUI *ui = (IndiePocketUI *) malloc (sizeof (IndiePocketUI));
  if (!ui)
    return NULL;

  ui->map = NULL;
  ui->write = write_function;
  ui->controller = controller;
  ui->root = NULL;
  ui->notebook = NULL;
  ui->name_box = NULL;
  ui->tune_box = NULL;
  ui->damp_box = NULL;
  ui->expr_box = NULL;
  ui->button = NULL;

  *widget = NULL;

  for (uint32_t i = 0; features[i]; ++i)
    {
      if (!strcmp (features[i]->URI, LV2_URID__map))
        ui->map = (LV2_URID_Map *) features[i]->data;
    }

  if (!ui->map)
    {
      fprintf (stderr, "indiepocket_ui.c: Host does not support urid:Map\n");
      free (ui);
      return NULL;
    }

  ipio_map_uris (&ui->uris, ui->map);
  lv2_atom_forge_init (&ui->forge, ui->map);

  char cwdbuf[PATH_MAX + 1];
  char *cwd = getcwd (cwdbuf, PATH_MAX + 1);
  if (chdir (bundle_path) || chdir ("assets"))
    fprintf (stderr, "indiepocket_ui.c: Could not cd to %sassets\n",
             bundle_path);

  GtkBuilder *builder = gtk_builder_new ();
  GError *error = NULL;
  if (!gtk_builder_add_from_file (builder, "ui.xml", &error))
    {
      fprintf (stderr, "indiepocket_ui.c: %s\n", error->message);
      g_error_free (error);
      free (ui);
      return NULL;
    }

  gtk_rc_parse ("gtkrc");

  if (cwd)
    chdir (cwd);

  void *widget_map[8][2] = {
    {&ui->root, "root"},
    {&ui->notebook, "notebook"},
    {&ui->name_box, "drum-name-box"},
    {&ui->tune_box, "drum-tune-box"},
    {&ui->damp_box, "drum-damp-box"},
    {&ui->expr_box, "drum-expr-box"},
    {&ui->statusbar, "statusbar"},
    {&ui->button, "file-chooser-button"}
  };

  for (uint8_t i = 0; i < 8; ++i)
    {
      GtkWidget **ref = (GtkWidget **) widget_map[i][0];
      const char *id = (const char *) widget_map[i][1];
      *ref = GTK_WIDGET (gtk_builder_get_object (builder, id));
      if (!*ref)
        {
          fprintf (stderr, "indiepocket_ui.c: Widget \"%s\" not found\n", id);
          g_object_unref (G_OBJECT (builder));
          free (ui);
          return NULL;
        }
    }

  g_object_ref (G_OBJECT (ui->root));
  g_object_unref (G_OBJECT (builder));

  g_signal_connect (ui->button, "file-set", G_CALLBACK (on_file_selected), ui);

  *widget = ui->root;

  return ui;
}

/* Free any resources allocated in `instantiate'.  */
static void
cleanup (LV2UI_Handle handle)
{
  IndiePocketUI *ui = (IndiePocketUI *) handle;
  clear_drum_controls (ui);
  g_object_unref (G_OBJECT (ui->root));
  gtk_widget_destroy (ui->root);
  free (ui);
}

/* Kit file notification callback.  */
static void
on_kit_loaded (IndiePocketUI *ui, const LV2_Atom_Object *obj)
{
  const LV2_Atom *kit_path = ipio_atom_get_kit_file (&ui->uris, obj);
  if (!kit_path)
    {
      fprintf (stderr, "Unknown message sent to UI.\n");
      return;
    }

  const char *filename = (const char *) LV2_ATOM_BODY_CONST (kit_path);
  if (strlen (filename))
    {
      GFile *kit_file = g_file_new_for_path (filename);
      gtk_file_chooser_select_file (GTK_FILE_CHOOSER (ui->button), kit_file,
                                    NULL);
      g_object_unref (kit_file);
      clear_drum_controls (ui);
    }
  else
    gtk_file_chooser_unselect_all (GTK_FILE_CHOOSER (ui->button));

  gtk_widget_set_sensitive (ui->button, TRUE);
}

/* Remove all drum control widgets.  */
static void
clear_drum_controls (IndiePocketUI *ui)
{
  GList *children, *it;
  GtkContainer *containers[4] = {
    GTK_CONTAINER (ui->tune_box),
    GTK_CONTAINER (ui->name_box),
    GTK_CONTAINER (ui->damp_box),
    GTK_CONTAINER (ui->expr_box)
  };
  for (uint8_t i = 0; i < 4; ++i)
    {
      children = gtk_container_get_children (containers[i]);
      for (it = children; it != NULL; it = g_list_next (it))
        gtk_widget_destroy (GTK_WIDGET (it->data));
      g_list_free (children);
    }
}

/* Create widget to control given drum property.  */
static GtkWidget *
create_drum_control (IndiePocketUI *ui, DrumProperty *prop,
                     float value, float from, float to, float step)
{
  GtkAdjustment *adj = (GtkAdjustment *) gtk_adjustment_new (value, from, to,
                                                             step, 0.5, 0);
  GtkWidget *dial = pckt_gtk_dial_new_with_adjustment (adj);
  DrumProperty *prop_data = malloc (sizeof (DrumProperty));
  memcpy (prop_data, prop, sizeof (DrumProperty));

  g_signal_connect (dial, "value-changed",
                    G_CALLBACK (on_drum_prop_changed), ui);
  g_object_set_data_full (G_OBJECT (dial),
                          DRUM_PROPERTY_KEY, prop_data,
                          (GDestroyNotify) free);
  return dial;
}

/* Drum meta notification callback.  */
static void
on_drum_loaded (IndiePocketUI *ui, const LV2_Atom_Object *obj)
{
  const LV2_Atom *index_atom = NULL, *name_atom = NULL;
  lv2_atom_object_get (obj, ui->uris.pckt_index, &index_atom,
                       ui->uris.doap_name, &name_atom, 0);
  if (!index_atom || (index_atom->type != ui->forge.Int)
      || !name_atom || (name_atom->type != ui->forge.String))
    {
      fprintf (stderr, "Could not read index and name from Drum message\n");
      return;
    }

  int8_t index = *(const int *) LV2_ATOM_BODY_CONST (index_atom);
  const char *name = (const char *) LV2_ATOM_BODY_CONST (name_atom);

  /* Create drum label.  */
  GtkWidget *name_label;
  if (strlen (name))
    name_label = gtk_label_new (name);
  else
    {
      char *anon = g_strdup_printf ("Drum #%d", index);
      name_label = gtk_label_new (anon);
      g_free (anon);
    }
  gtk_misc_set_alignment (GTK_MISC (name_label), 0.5, 0.5);
  gtk_box_pack_start (GTK_BOX (ui->name_box), GTK_WIDGET (name_label),
                      TRUE, TRUE, 0);
  gtk_widget_show (name_label);

  /* Create tuning control.  */
  DrumProperty tune_prop = {ui->uris.pckt_tuning, index};
  GtkWidget *tune_ctrl = create_drum_control (ui, &tune_prop, 0, -12, 12, 0.1);
  gtk_box_pack_start (GTK_BOX (ui->tune_box), GTK_WIDGET (tune_ctrl),
                      TRUE, TRUE, 0);
  gtk_widget_show (tune_ctrl);

  /* Create dampening control.  */
  DrumProperty damp_prop = {ui->uris.pckt_dampening, index};
  GtkWidget *damp_ctrl = create_drum_control (ui, &damp_prop, 0, 0, 1, 0.01);
  gtk_box_pack_start (GTK_BOX (ui->damp_box), GTK_WIDGET (damp_ctrl),
                      TRUE, TRUE, 0);
  gtk_widget_show (damp_ctrl);

  /* Create expression control.  */
  DrumProperty expr_prop = {ui->uris.pckt_expression, index};
  GtkWidget *expr_ctrl = create_drum_control (ui, &expr_prop, 0, -1, 1, 0.01);
  gtk_box_pack_start (GTK_BOX (ui->expr_box), GTK_WIDGET (expr_ctrl),
                      TRUE, TRUE, 0);
  gtk_widget_show (expr_ctrl);
}

/* Port event listener.  */
static void
port_event (LV2UI_Handle handle, uint32_t port_index, uint32_t buffer_size,
            uint32_t format, const void *buffer)
{
  IndiePocketUI *ui = (IndiePocketUI *) handle;
  (void) port_index;
  (void) buffer_size;

  if (format != ui->uris.atom_eventTransfer)
    {
      fprintf (stderr, "Unknown format\n");
      return;
    }

  const LV2_Atom *atom = (const LV2_Atom *) buffer;
  if (!ipio_atom_type_is_object (&ui->forge, atom->type))
    {
      fprintf (stderr, "Unknown message type.\n");
      return;
    }

  const LV2_Atom_Object *obj = (const LV2_Atom_Object *) atom;
  if (obj->body.otype == ui->uris.patch_Set)
    on_kit_loaded (ui, obj);
  else if (obj->body.otype == ui->uris.pckt_Drum)
    on_drum_loaded (ui, obj);
}

/* Return any extension data supported by this UI.  */
static const void *
extension_data (const char *uri)
{
  (void) uri;
  return NULL;
}

/* IndiePocket UI descriptor.  */
static const LV2UI_Descriptor descriptor = {
  IPCKT_UI_URI,
  instantiate,
  cleanup,
  port_event,
  extension_data
};

/* Export UI to lv2 host.  */
LV2_SYMBOL_EXPORT
const LV2UI_Descriptor *
lv2ui_descriptor (uint32_t index)
{
  if (index == 0)
    return &descriptor;
  return NULL;
}
