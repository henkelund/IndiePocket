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
#include <math.h>

/* LV2 headers.  */
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>

/* GTK+ headers.  */
#include <gtk/gtk.h>

/* IndiePocket headers.  */
#include "indiepocket_io.h"
#include "../pckt/gtk2/dial.h"

typedef struct DrumPropertyImpl DrumProperty;

typedef struct
{
  LV2_Atom_Forge forge;
  LV2_URID_Map *map;
  IPIOURIs uris;
  LV2UI_Write_Function write;
  LV2UI_Controller controller;
  GtkWidget *root;
  GtkWidget *drum_controls;
  GtkWidget *button;
  GtkWidget *statusbar;
  gchar *drum_template;
  DrumProperty *drum_props;
} IndiePocketUI;

struct DrumPropertyImpl
{
  const char *widget_id;
  LV2_URID uri;
  int8_t drum;
  GtkLabel *label;
  PcktGtkDial *dial;
  DrumProperty *next;
};

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

#define DRUM_PROPERTY_KEY "drum_property"

/* Convenience macro for writing message to control port.  */
#define WRITE_CONTROL_MESSAGE(ui, message, _C_)            \
  do {                                                     \
    LV2_Atom *message = NULL;                              \
    uint8_t buffer[IPIO_FORGE_BUFFER_SIZE];                \
    lv2_atom_forge_set_buffer (&(ui)->forge, buffer,       \
                               IPIO_FORGE_BUFFER_SIZE);    \
    {_C_;}                                                 \
    (ui)->write ((ui)->controller, IPIO_CONTROL,           \
                 lv2_atom_total_size (message),            \
                 (ui)->uris.atom_eventTransfer, message);  \
  } while (0)

/* Convenience macro for writing object to control port.  */
#define WRITE_CONTROL_OBJECT(ui, uri, _C_)                          \
  WRITE_CONTROL_MESSAGE ((ui), message,                             \
    LV2_Atom_Forge_Frame frame;                                     \
    message = (LV2_Atom *) ipio_forge_object (&(ui)->forge, &frame, \
                                              (ui)->uris.uri);      \
    {_C_;}                                                          \
    lv2_atom_forge_pop (&(ui)->forge, &frame);                      \
  )

static void clear_drum_controls (IndiePocketUI *ui);

/* Load kit button click callback.  */
static void
on_file_selected (GtkWidget *widget, void *handle)
{
  IndiePocketUI *ui = (IndiePocketUI *) handle;
  char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));

  gtk_widget_set_sensitive (widget, FALSE);
  clear_drum_controls (ui);

  WRITE_CONTROL_MESSAGE (ui, msg,
    msg = ipio_forge_kit_file_atom (&ui->forge, &ui->uris, filename)
  );

  g_free (filename);
}

/* Show state of given drum control.  */
static void
show_drum_prop_status (const DrumProperty *prop, IndiePocketUI *ui)
{
  GtkRange *range = GTK_RANGE (prop->dial);
  float value = (float) gtk_range_get_value (range);
  GtkStatusbar *statusbar = GTK_STATUSBAR (ui->statusbar);
  guint context_id = gtk_statusbar_get_context_id (statusbar, prop->widget_id);
  const char *name = gtk_label_get_text (prop->label);
  char *message = NULL;

  if (!strcmp (prop->widget_id, "tune-dial"))
    {
      message = g_strdup_printf ("%s tuning: %+.1f semitones", name, value);
    }
  else if (!strcmp (prop->widget_id, "damp-dial"))
    {
      message = g_strdup_printf ("%s damping: %d%%", name,
                                 (int) (value * 100.f));
    }
  else if (!strcmp (prop->widget_id, "expr-dial"))
    {
      if (value == 0.f)
        message = g_strdup_printf ("%s expression: linear", name);
      else
        message = g_strdup_printf ("%s expression: %d%% %s", name,
                                   (int) (fabsf (value) * 100.f),
                                   (value < 0.f) ? "laid-back" : "aggressive");
    }
  else if (!strcmp (prop->widget_id, "olap-dial"))
    {
      message = g_strdup_printf ("%s sample overlap: %d%%", name,
                                 (int) (value * 100.f));
    }
  else
    fprintf (stderr, "Unknown widget ID: %s\n", prop->widget_id);

  if (message)
    {
      gtk_statusbar_remove_all (statusbar, context_id);
      gtk_statusbar_push (statusbar, context_id, message);
      g_free (message);
    }
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

  WRITE_CONTROL_MESSAGE (ui, msg,
    msg = ipio_write_drum_property (&ui->forge, &ui->uris, prop->drum,
                                    prop->uri, value)
  );

  show_drum_prop_status (prop, ui);
}

/* Drum control mouseover callback.  */
static gboolean
on_drum_prop_mouseover (GtkWidget *widget, GdkEvent *event, void *handle)
{
  (void) event;

  if (gtk_widget_has_grab (widget))
    return FALSE;

  DrumProperty *prop = g_object_get_data (G_OBJECT (widget), DRUM_PROPERTY_KEY);
  if (prop)
    show_drum_prop_status (prop, (IndiePocketUI *) handle);

  return FALSE;
}

/* Drum control mouseout callback.  */
static gboolean
on_drum_prop_mouseout (GtkWidget *widget, GdkEvent *event, void *handle)
{
  (void) event;

  if (gtk_widget_has_grab (widget))
    return FALSE;

  IndiePocketUI *ui = (IndiePocketUI *) handle;
  DrumProperty *prop = g_object_get_data (G_OBJECT (widget), DRUM_PROPERTY_KEY);
  if (prop && ui)
    {
      GtkStatusbar *statusbar = GTK_STATUSBAR (ui->statusbar);
      guint context_id = gtk_statusbar_get_context_id (statusbar,
                                                       prop->widget_id);
      gtk_statusbar_remove_all (statusbar, context_id);
    }

  return FALSE;
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
  ui->drum_controls = NULL;
  ui->button = NULL;
  ui->statusbar = NULL;
  ui->drum_template = NULL;
  ui->drum_props = NULL;

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
    fprintf (stderr, __FILE__ ": Could not cd to %sassets\n", bundle_path);

  /* Touch custom GTK type to register it's widget class.  */
  (void) PCKT_GTK_TYPE_DIAL;

  GError *error = NULL;
  GtkBuilder *builder = gtk_builder_new ();
  if (!gtk_builder_add_from_file (builder, "layout.ui", &error))
    {
      fprintf (stderr, __FILE__ ": %s\n", error->message);
      g_error_free (error);
      free (ui);
      return NULL;
    }

  error = NULL;
  if (!g_file_get_contents ("drum.ui", &ui->drum_template, NULL, &error))
    {
      fprintf (stderr, __FILE__ ": %s\n", error->message);
      g_error_free (error);
      g_object_unref (G_OBJECT (builder));
      free (ui);
      return NULL;
    }

  gtk_rc_parse ("gtkrc");

  if (cwd)
    chdir (cwd);

  void *widget_map[4][2] = {
    {&ui->root, "root"},
    {&ui->drum_controls, "drum-controls"},
    {&ui->button, "file-chooser-button"},
    {&ui->statusbar, "statusbar"}
  };

  for (uint8_t i = 0; i < 4; ++i)
    {
      GtkWidget **ref = (GtkWidget **) widget_map[i][0];
      const char *id = (const char *) widget_map[i][1];
      *ref = GTK_WIDGET (gtk_builder_get_object (builder, id));
      if (!*ref)
        {
          fprintf (stderr, __FILE__ ": Widget \"%s\" not found\n", id);
          g_object_unref (G_OBJECT (builder));
          free (ui->drum_template);
          free (ui);
          return NULL;
        }
    }

  GtkFileFilter *file_filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (file_filter, "Kits (*.pckt *.bfk)");
  gtk_file_filter_add_pattern (file_filter, "*.pckt");
  gtk_file_filter_add_pattern (file_filter, "*.bfk");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (ui->button), file_filter);
  g_signal_connect (ui->button, "file-set", G_CALLBACK (on_file_selected), ui);

#ifdef PCKT_VERSION
  GtkLabel *version_label;
  version_label = GTK_LABEL (gtk_builder_get_object (builder, "version-label"));
  if (version_label)
    gtk_label_set_text (version_label, "Ver. " PCKT_VERSION);
#endif

  g_object_ref (G_OBJECT (ui->root));
  g_object_unref (G_OBJECT (builder));

  /* Ask plugin if there's a kit loaded already.  */
  WRITE_CONTROL_OBJECT (ui, patch_Get,
    ipio_forge_key (&ui->forge, ui->uris.patch_property);
    lv2_atom_forge_urid (&ui->forge, ui->uris.pckt_Kit);
  );

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
  free (ui->drum_template);
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

  GtkStatusbar *statusbar = GTK_STATUSBAR (ui->statusbar);
  guint context_id = gtk_statusbar_get_context_id (statusbar, "kit");
  const char *filename = (const char *) LV2_ATOM_BODY_CONST (kit_path);

  if (strlen (filename))
    {
      GFileInfo *info;
      GFile *kit_file = g_file_new_for_path (filename);
      info = g_file_query_info (kit_file,
                                G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                G_FILE_QUERY_INFO_NONE,
                                NULL,
                                NULL);

      gtk_file_chooser_select_file (GTK_FILE_CHOOSER (ui->button), kit_file,
                                    NULL);

      if (info)
        {
          gtk_statusbar_remove_all (statusbar, context_id);
          gtk_statusbar_push (statusbar, context_id,
                              g_file_info_get_display_name (info));
          g_object_unref (info);
        }

      g_object_unref (kit_file);
    }
  else
    {
      clear_drum_controls (ui);
      gtk_file_chooser_unselect_all (GTK_FILE_CHOOSER (ui->button));
    }

  gtk_widget_set_sensitive (ui->button, TRUE);
}

/* Remove all drum control widgets.  */
static void
clear_drum_controls (IndiePocketUI *ui)
{
  GList *children, *it;
  ui->drum_props = NULL;
  children = gtk_container_get_children (GTK_CONTAINER (ui->drum_controls));
  for (it = children; it != NULL; it = g_list_next (it))
    gtk_widget_destroy (GTK_WIDGET (it->data));
  g_list_free (children);
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

  GError *error = NULL;
  GtkBuilder *builder = gtk_builder_new ();
  if (!gtk_builder_add_from_string (builder, ui->drum_template, -1, &error))
    {
      fprintf (stderr, __FILE__ ": %s\n", error->message);
      g_error_free (error);
      return;
    }

  GtkLabel *name_label;
  name_label = GTK_LABEL (gtk_builder_get_object (builder, "name-label"));
  if (strlen (name))
    gtk_label_set_label (name_label, name);
  else
    {
      char *anon = g_strdup_printf ("Drum #%d", index);
      gtk_label_set_label (name_label, anon);
      g_free (anon);
    }

  DrumProperty props[] = {
    {"tune-dial", ui->uris.pckt_tuning, index, name_label, NULL, NULL},
    {"damp-dial", ui->uris.pckt_dampening, index, name_label, NULL, NULL},
    {"expr-dial", ui->uris.pckt_expression, index, name_label, NULL, NULL},
    {"olap-dial", ui->uris.pckt_overlap, index, name_label, NULL, NULL}
  };

  for (uint8_t i = 0; i < 4; ++i)
    {
      GObject *dial = gtk_builder_get_object (builder, props[i].widget_id);
      if (!PCKT_GTK_IS_DIAL (dial))
        continue;

      DrumProperty *prop = malloc (sizeof (DrumProperty));
      memcpy (prop, props + i, sizeof (DrumProperty));
      prop->dial = PCKT_GTK_DIAL (dial);
      prop->next = ui->drum_props;
      ui->drum_props = prop;
      g_object_set_data_full (G_OBJECT (dial),
                              DRUM_PROPERTY_KEY, prop,
                              (GDestroyNotify) free);

      g_signal_connect (dial, "value-changed",
                        G_CALLBACK (on_drum_prop_changed), ui);
      g_signal_connect (dial, "enter-notify-event",
                        G_CALLBACK (on_drum_prop_mouseover), ui);
      g_signal_connect (dial, "leave-notify-event",
                        G_CALLBACK (on_drum_prop_mouseout), ui);

      /* Request property value from plugin.  */
      WRITE_CONTROL_OBJECT (ui, patch_Get,
        ipio_forge_key (&ui->forge, ui->uris.patch_subject);
        lv2_atom_forge_int (&ui->forge, prop->drum);
        ipio_forge_key (&ui->forge, ui->uris.patch_property);
        lv2_atom_forge_urid (&ui->forge, prop->uri);
      );
    }

  GtkWidget *root = GTK_WIDGET (gtk_builder_get_object (builder, "root"));
  gtk_container_add (GTK_CONTAINER (ui->drum_controls), root);

  g_object_unref (G_OBJECT (builder));
}

/* Find drum property by ID and URI.  */
static DrumProperty *
find_drum_prop (IndiePocketUI *ui, int8_t drum, LV2_URID uri)
{
  for (DrumProperty **item = &ui->drum_props; *item; item = &(*item)->next)
    {
      if (((*item)->drum == drum) && ((*item)->uri == uri))
        return *item;
    }
  return NULL;
}

/* Drum property notification callback.  */
static void
on_drum_prop_value_set (IndiePocketUI *ui, const LV2_Atom_Object *obj)
{
  const LV2_Atom *id_atom = NULL, *prop_atom = NULL, *val_atom = NULL;
  lv2_atom_object_get (obj,
                       ui->uris.patch_subject, &id_atom,
                       ui->uris.patch_property, &prop_atom,
                       ui->uris.patch_value, &val_atom,
                       0);

  if (id_atom && (id_atom->type == ui->forge.Int)
      && val_atom && (val_atom->type == ui->forge.Float))
    {
      int8_t id = *(const int *) LV2_ATOM_BODY_CONST (id_atom);
      LV2_URID uri = ((const LV2_Atom_URID *) prop_atom)->body;
      float value = *(const float *) LV2_ATOM_BODY_CONST (val_atom);
      DrumProperty *drum_prop = find_drum_prop (ui, id, uri);
      if (drum_prop)
        gtk_range_set_value (GTK_RANGE (drum_prop->dial), (gdouble) value);
      else
        fprintf (stderr, "No property found for drum %d and uri %u\n", id, uri);
    }
  else
    fprintf (stderr, "Invalid set property message\n");
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
    {
      const LV2_Atom *property = NULL;
      lv2_atom_object_get (obj, ui->uris.patch_property, &property, 0);
      if (!property && (property->type != ui->uris.atom_URID))
        {
          fprintf (stderr, "Got patch message with no property.\n");
          return;
        }

      LV2_URID uri = ((const LV2_Atom_URID *) property)->body;
      if (uri == ui->uris.pckt_Kit)
        on_kit_loaded (ui, obj);
      else
        on_drum_prop_value_set (ui, obj);
    }
  else if (obj->body.otype == ui->uris.pckt_DrumMeta)
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
