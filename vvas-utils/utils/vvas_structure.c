/*
 * Copyright (C) 2022 Xilinx, Inc.  All rights reserved.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
 */

/*
 * This file is using some of the work from David A. Schleef
 * and the original license for that work has been
 * preserved in this file below
 */

/* GStreamer
 * Copyright (C) 2003 David A. Schleef <ds@schleef.org>
 *
 * gststructure.c: lists of { GQuark, GValue } tuples
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <glib-object.h>
#include <gobject/gvaluecollector.h>

#include <vvas/vvas_structure.h>

#define VVAS_STRUCTURE_LEN(s) (s->fields_len)

#define VVAS_STRUCTURE_IS_USING_DYNAMIC_ARRAY(s) \
  (s->fields != &(s->arr[0]))

#define VVAS_STRUCTURE_FIELD(structure, index) \
  (&(structure)->fields[(index)])

#define ALIGN_BY_8(num)  (((num)+7)&~7)

/* Replacement for g_array_append_val */
static void
_structure_append_val (VvasStructure * s, VvasStructureField * val)
{
  /* resize if needed */
  if (G_UNLIKELY (s->fields_len == s->fields_alloc)) {
    guint want_alloc;

    if (G_UNLIKELY (s->fields_alloc > (G_MAXUINT / 2)))
      g_error ("Growing structure would result in overflow");

    want_alloc = MAX (ALIGN_BY_8 (s->fields_len + 1), s->fields_alloc * 2);
    if (VVAS_STRUCTURE_IS_USING_DYNAMIC_ARRAY (s)) {
      s->fields = g_renew (VvasStructureField, s->fields, want_alloc);
    } else {
      s->fields = g_new0 (VvasStructureField, want_alloc);
      memcpy (s->fields, &s->arr[0],
          s->fields_len * sizeof (VvasStructureField));
    }
    s->fields_alloc = want_alloc;
  }

  /* Finally set value */
  s->fields[s->fields_len++] = *val;
}

/* Replacement for g_array_remove_index */
static inline void
_structure_remove_index (VvasStructure * s, guint idx)
{
  /* We never "reduce" the memory footprint. */
  if (idx >= s->fields_len)
    return;

  /* Shift everything if it's not the last item */
  if (idx != s->fields_len)
    memmove (&s->fields[idx],
        &s->fields[idx + 1],
        (s->fields_len - idx - 1) * sizeof (VvasStructureField));
  s->fields_len--;
}

static void vvas_structure_set_field (VvasStructure * structure,
    VvasStructureField * field);
static VvasStructureField *vvas_structure_get_field (const VvasStructure *
    structure, const gchar * fieldname);
static VvasStructureField *vvas_structure_id_get_field (const VvasStructure *
    structure, GQuark field);
static VvasStructure *vvas_structure_new_valist (const gchar * name,
    const gchar * firstfield, va_list varargs);

#ifndef G_DISABLE_CHECKS
static gboolean
vvas_structure_validate_name (const gchar * name)
{
  const gchar *s;

  g_return_val_if_fail (name != NULL, FALSE);

  if (G_UNLIKELY (!g_ascii_isalpha (*name))) {
    g_print ("Invalid character '%c' at offset 0 in structure name: %s",
        *name, name);
    return FALSE;
  }

  /* FIXME: test name string more */
  s = &name[1];
  while (*s && (g_ascii_isalnum (*s) || strchr ("/-_.:+", *s) != NULL))
    s++;
  if (G_UNLIKELY (*s != '\0')) {
    g_print ("Invalid character '%c' at offset %" G_GUINTPTR_FORMAT " in"
        " structure name: %s", *s, ((guintptr) s - (guintptr) name), name);
    return FALSE;
  }

  if (strncmp (name, "video/x-raw-", 12) == 0) {
    g_warning ("0.10-style raw video caps are being created. Should be "
        "video/x-raw,format=(string).. now.");
  } else if (strncmp (name, "audio/x-raw-", 12) == 0) {
    g_warning ("0.10-style raw audio caps are being created. Should be "
        "audio/x-raw,format=(string).. now.");
  }

  return TRUE;
}
#endif

static VvasStructure *
vvas_structure_new_id_empty_with_size (GQuark quark, guint prealloc)
{
  guint n_alloc;
  VvasStructure *structure;

  if (prealloc == 0)
    prealloc = 1;

  n_alloc = ALIGN_BY_8 (prealloc);
  structure =
      g_malloc0 (sizeof (VvasStructure) + (n_alloc -
          1) * sizeof (VvasStructureField));

  structure->name = quark;

  structure->fields_len = 0;
  structure->fields_alloc = n_alloc;
  structure->fields = &structure->arr[0];

  return structure;
}

static void
vvas_structure_set_valist_internal (VvasStructure * structure,
    const gchar * fieldname, va_list varargs)
{
  gchar *err = NULL;
  GType type;

  while (fieldname) {
    VvasStructureField field = { 0 };

    field.name = g_quark_from_string (fieldname);

    type = va_arg (varargs, GType);

    G_VALUE_COLLECT_INIT (&field.value, type, varargs, 0, &err);
    if (G_UNLIKELY (err)) {
      g_critical ("%s", err);
      g_free (err);
      return;
    }
    vvas_structure_set_field (structure, &field);

    fieldname = va_arg (varargs, gchar *);
  }
}

/**
 * vvas_structure_id_has_field_typed:
 * @structure: a #VvasStructure
 * @field: #GQuark of the field name
 * @type: the type of a value
 *
 * Check if @structure contains a field named @field and with GType @type.
 *
 * Returns: %TRUE if the structure contains a field with the given name and type
 */
static gboolean
vvas_structure_id_has_field_typed (const VvasStructure * structure,
    GQuark field, GType type)
{
  VvasStructureField *f;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (field != 0, FALSE);

  f = vvas_structure_id_get_field (structure, field);
  if (f == NULL)
    return FALSE;

  return (G_VALUE_TYPE (&f->value) == type);
}

/**
 * vvas_structure_id_has_field:
 * @structure: a #VvasStructure
 * @field: #GQuark of the field name
 *
 * Check if @structure contains a field named @field.
 *
 * Returns: %TRUE if the structure contains a field with the given name
 */
static gboolean
vvas_structure_id_has_field (const VvasStructure * structure, GQuark field)
{
  VvasStructureField *f;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (field != 0, FALSE);

  f = vvas_structure_id_get_field (structure, field);

  return (f != NULL);
}


/* If there is no field with the given ID, NULL is returned.
 */
static VvasStructureField *
vvas_structure_id_get_field (const VvasStructure * structure, GQuark field_id)
{
  VvasStructureField *field;
  guint i, len;

  len = VVAS_STRUCTURE_LEN (structure);

  for (i = 0; i < len; i++) {
    field = VVAS_STRUCTURE_FIELD (structure, i);

    if (G_UNLIKELY (field->name == field_id))
      return field;
  }

  return NULL;
}

/**
 * vvas_structure_remove_fields_valist:
 * @structure: a #VvasStructure
 * @fieldname: the name of the field to remove
 * @varargs: %NULL-terminated list of more fieldnames to remove
 *
 * va_list form of vvas_structure_remove_fields().
 */
static void
vvas_structure_remove_fields_valist (VvasStructure * structure,
    const gchar * fieldname, va_list varargs)
{
  gchar *field = (gchar *) fieldname;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);
  /* mutability checked in remove_field */

  while (field) {
    vvas_structure_remove_field (structure, field);
    field = va_arg (varargs, char *);
  }
}

/**
 * vvas_structure_new_empty:
 * @name: name of new structure
 *
 * Creates a new, empty #VvasStructure with the given @name.
 *
 * See vvas_structure_set_name() for constraints on the @name parameter.
 *
 * Free-function: vvas_structure_free
 *
 * Returns: (transfer full): a new, empty #VvasStructure
 */
VvasStructure *
vvas_structure_new_empty (const gchar * name)
{
  g_return_val_if_fail (vvas_structure_validate_name (name), NULL);

  return vvas_structure_new_id_empty_with_size (g_quark_from_string (name), 0);
}

/**
 * vvas_structure_new:
 * @name: name of new structure
 * @firstfield: name of first field to set
 * @...: additional arguments
 *
 * Creates a new #VvasStructure with the given name.  Parses the
 * list of variable arguments and sets fields to the values listed.
 * Variable arguments should be passed as field name, field type,
 * and value.  Last variable argument should be %NULL.
 *
 * Free-function: vvas_structure_free
 *
 * Returns: (transfer full): a new #VvasStructure
 */
VvasStructure *
vvas_structure_new (const gchar * name, const gchar * firstfield, ...)
{
  VvasStructure *structure;
  va_list varargs;

  va_start (varargs, firstfield);
  structure = vvas_structure_new_valist (name, firstfield, varargs);
  va_end (varargs);

  return structure;
}

/**
 * vvas_structure_set_valist:
 * @structure: a #VvasStructure
 * @fieldname: the name of the field to set
 * @varargs: variable arguments
 *
 * va_list form of vvas_structure_set().
 */
static void
vvas_structure_set_valist (VvasStructure * structure,
    const gchar * fieldname, va_list varargs)
{
  g_return_if_fail (structure != NULL);

  vvas_structure_set_valist_internal (structure, fieldname, varargs);
}

/**
 * vvas_structure_new_valist:
 * @name: name of new structure
 * @firstfield: name of first field to set
 * @varargs: variable argument list
 *
 * Creates a new #VvasStructure with the given @name.  Structure fields
 * are set according to the varargs in a manner similar to
 * vvas_structure_new().
 *
 * See vvas_structure_set_name() for constraints on the @name parameter.
 *
 * Free-function: vvas_structure_free
 *
 * Returns: (transfer full): a new #VvasStructure
 */
static VvasStructure *
vvas_structure_new_valist (const gchar * name,
    const gchar * firstfield, va_list varargs)
{
  VvasStructure *structure;
  va_list copy;
  guint len = 0;
  const gchar *field_copy = firstfield;
  GType type_copy;

  g_return_val_if_fail (vvas_structure_validate_name (name), NULL);

  /* Calculate size of varargs */
  va_copy (copy, varargs);
  while (field_copy) {
    type_copy = va_arg (copy, GType);
    G_VALUE_COLLECT_SKIP (type_copy, copy);
    field_copy = va_arg (copy, gchar *);
    len++;
  }
  va_end (copy);

  structure =
      vvas_structure_new_id_empty_with_size (g_quark_from_string (name), len);

  if (structure)
    vvas_structure_set_valist (structure, firstfield, varargs);

  return structure;
}

static void
vvas_value_init_and_copy (GValue * dest, const GValue * src)
{
  GType type;

  g_return_if_fail (G_IS_VALUE (src));
  g_return_if_fail (dest != NULL);

  type = G_VALUE_TYPE (src);

  g_value_init (dest, type);
  g_value_copy (src, dest);
}


/**
 * vvas_structure_copy:
 * @structure: a #VvasStructure to duplicate
 *
 * Duplicates a #VvasStructure and all its fields and values.
 *
 * Free-function: vvas_structure_free
 *
 * Returns: (transfer full): a new #VvasStructure.
 */
VvasStructure *
vvas_structure_copy (const VvasStructure * structure)
{
  VvasStructure *new_structure;
  VvasStructureField *field;
  guint i, len;

  g_return_val_if_fail (structure != NULL, NULL);

  len = VVAS_STRUCTURE_LEN (structure);
  new_structure = vvas_structure_new_id_empty_with_size (structure->name, len);

  for (i = 0; i < len; i++) {
    VvasStructureField new_field = { 0 };

    field = VVAS_STRUCTURE_FIELD (structure, i);

    new_field.name = field->name;
    vvas_value_init_and_copy (&new_field.value, &field->value);
    _structure_append_val (new_structure, &new_field);
  }
  return new_structure;
}

/**
 * vvas_structure_free:
 * @structure: (in) (transfer full): the #VvasStructure to free
 *
 * Frees a #VvasStructure and all its fields and values. The structure must not
 * have a parent when this function is called.
 */
void
vvas_structure_free (VvasStructure * structure)
{
  VvasStructureField *field;
  guint i, len;

  g_return_if_fail (structure != NULL);

  len = VVAS_STRUCTURE_LEN (structure);
  for (i = 0; i < len; i++) {
    field = VVAS_STRUCTURE_FIELD (structure, i);

    if (G_IS_VALUE (&field->value)) {
      g_value_unset (&field->value);
    }
  }
  if (VVAS_STRUCTURE_IS_USING_DYNAMIC_ARRAY (structure))
    g_free (structure->fields);

#ifdef USE_POISONING
  memset (structure, 0xff, sizeof (VvasStructure));
#endif

  g_free (structure);
}

/**
 * vvas_structure_get_name:
 * @structure: a #VvasStructure
 *
 * Get the name of @structure as a string.
 *
 * Returns: the name of the structure.
 */
const gchar *
vvas_structure_get_name (const VvasStructure * structure)
{
  g_return_val_if_fail (structure != NULL, NULL);

  return g_quark_to_string (structure->name);
}

/**
 * vvas_structure_has_name:
 * @structure: a #VvasStructure
 * @name: structure name to check for
 *
 * Checks if the structure has the given name
 *
 * Returns: %TRUE if @name matches the name of the structure.
 */
gboolean
vvas_structure_has_name (const VvasStructure * structure, const gchar * name)
{
  const gchar *structure_name;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  /* getting the string is cheap and comparing short strings is too
   * should be faster than getting the quark for name and comparing the quarks
   */
  structure_name = g_quark_to_string (structure->name);

  return (structure_name && strcmp (structure_name, name) == 0);
}

/**
 * vvas_structure_set_name:
 * @structure: a #VvasStructure
 * @name: the new name of the structure
 *
 * Sets the name of the structure to the given @name.  The string
 * provided is copied before being used. It must not be empty, start with a
 * letter and can be followed by letters, numbers and any of "/-_.:".
 */
void
vvas_structure_set_name (VvasStructure * structure, const gchar * name)
{
  g_return_if_fail (structure != NULL);
  g_return_if_fail (vvas_structure_validate_name (name));

  structure->name = g_quark_from_string (name);
}

static inline void
vvas_structure_id_set_value_internal (VvasStructure * structure, GQuark field,
    const GValue * value)
{
  VvasStructureField gsfield = { 0, {0,} };

  gsfield.name = field;
  vvas_value_init_and_copy (&gsfield.value, value);

  vvas_structure_set_field (structure, &gsfield);
}

/**
 * vvas_structure_set_value:
 * @structure: a #VvasStructure
 * @fieldname: the name of the field to set
 * @value: the new value of the field
 *
 * Sets the field with the given name @field to @value.  If the field
 * does not exist, it is created.  If the field exists, the previous
 * value is replaced and freed.
 */
void
vvas_structure_set_value (VvasStructure * structure,
    const gchar * fieldname, const GValue * value)
{
  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);
  g_return_if_fail (G_IS_VALUE (value));

  vvas_structure_id_set_value_internal (structure,
      g_quark_from_string (fieldname), value);
}

/**
 * vvas_structure_set:
 * @structure: a #VvasStructure
 * @fieldname: the name of the field to set
 * @...: variable arguments
 *
 * Parses the variable arguments and sets fields accordingly. Fields that
 * weren't already part of the structure are added as needed.
 * Variable arguments should be in the form field name, field type
 * (as a GType), value(s).  The last variable argument should be %NULL.
 */
void
vvas_structure_set (VvasStructure * structure, const gchar * field, ...)
{
  va_list varargs;

  g_return_if_fail (structure != NULL);

  va_start (varargs, field);
  vvas_structure_set_valist_internal (structure, field, varargs);
  va_end (varargs);
}

/* If the structure currently contains a field with the same name, it is
 * replaced with the provided field. Otherwise, the field is added to the
 * structure. The field's value is not deeply copied.
 */
static void
vvas_structure_set_field (VvasStructure * structure, VvasStructureField * field)
{
  VvasStructureField *f;
  GType field_value_type;
  guint i, len;

  len = VVAS_STRUCTURE_LEN (structure);

  field_value_type = G_VALUE_TYPE (&field->value);
  if (field_value_type == G_TYPE_STRING) {
    const gchar *s;

    s = g_value_get_string (&field->value);
    /* only check for NULL strings in taglists, as they are allowed in message
     * structs, e.g. error message debug strings */
    if (G_UNLIKELY (s != NULL && !g_utf8_validate (s, -1, NULL))) {
      g_warning
          ("Trying to set string on structure field '%s', but string is not "
          "valid UTF-8. Please file a bug.", g_quark_to_string (field->name));
      g_value_unset (&field->value);
      return;
    }
  } else if (G_UNLIKELY (field_value_type == G_TYPE_DATE)) {
    const GDate *d;

    d = g_value_get_boxed (&field->value);
    if (G_UNLIKELY (d != NULL && !g_date_valid (d))) {
      g_warning
          ("Trying to set invalid GDate on strucutre field '%s'. Please file a bug.",
          g_quark_to_string (field->name));
      g_value_unset (&field->value);
      return;
    }
  }

  for (i = 0; i < len; i++) {
    f = VVAS_STRUCTURE_FIELD (structure, i);

    if (G_UNLIKELY (f->name == field->name)) {
      g_value_unset (&f->value);
      memcpy (f, field, sizeof (VvasStructureField));
      return;
    }
  }

  _structure_append_val (structure, field);
}

/* If there is no field with the given ID, NULL is returned.
 */
static VvasStructureField *
vvas_structure_get_field (const VvasStructure * structure,
    const gchar * fieldname)
{
  g_return_val_if_fail (structure != NULL, NULL);
  g_return_val_if_fail (fieldname != NULL, NULL);

  return vvas_structure_id_get_field (structure,
      g_quark_from_string (fieldname));
}

/**
 * vvas_structure_get_value:
 * @structure: a #VvasStructure
 * @fieldname: the name of the field to get
 *
 * Get the value of the field with name @fieldname.
 *
 * Returns: (nullable): the #GValue corresponding to the field with the given
 * name.
 */
const GValue *
vvas_structure_get_value (const VvasStructure * structure,
    const gchar * fieldname)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, NULL);
  g_return_val_if_fail (fieldname != NULL, NULL);

  field = vvas_structure_get_field (structure, fieldname);
  if (field == NULL)
    return NULL;

  return &field->value;
}

/**
 * vvas_structure_remove_field:
 * @structure: a #VvasStructure
 * @fieldname: the name of the field to remove
 *
 * Removes the field with the given name.  If the field with the given
 * name does not exist, the structure is unchanged.
 */
void
vvas_structure_remove_field (VvasStructure * structure, const gchar * fieldname)
{
  VvasStructureField *field;
  GQuark id;
  guint i, len;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);

  id = g_quark_from_string (fieldname);
  len = VVAS_STRUCTURE_LEN (structure);

  for (i = 0; i < len; i++) {
    field = VVAS_STRUCTURE_FIELD (structure, i);

    if (field->name == id) {
      if (G_IS_VALUE (&field->value)) {
        g_value_unset (&field->value);
      }
      _structure_remove_index (structure, i);
      return;
    }
  }
}

/**
 * vvas_structure_remove_fields:
 * @structure: a #VvasStructure
 * @fieldname: the name of the field to remove
 * @...: %NULL-terminated list of more fieldnames to remove
 *
 * Removes the fields with the given names. If a field does not exist, the
 * argument is ignored.
 */
void
vvas_structure_remove_fields (VvasStructure * structure,
    const gchar * fieldname, ...)
{
  va_list varargs;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);
  /* mutability checked in remove_field */

  va_start (varargs, fieldname);
  vvas_structure_remove_fields_valist (structure, fieldname, varargs);
  va_end (varargs);
}

/**
 * vvas_structure_remove_all_fields:
 * @structure: a #VvasStructure
 *
 * Removes all fields in a VvasStructure.
 */
void
vvas_structure_remove_all_fields (VvasStructure * structure)
{
  VvasStructureField *field;
  int i;

  g_return_if_fail (structure != NULL);

  for (i = VVAS_STRUCTURE_LEN (structure) - 1; i >= 0; i--) {
    field = VVAS_STRUCTURE_FIELD (structure, i);

    if (G_IS_VALUE (&field->value)) {
      g_value_unset (&field->value);
    }
    _structure_remove_index (structure, i);
  }
}

/**
 * vvas_structure_get_field_type:
 * @structure: a #VvasStructure
 * @fieldname: the name of the field
 *
 * Finds the field with the given name, and returns the type of the
 * value it contains.  If the field is not found, G_TYPE_INVALID is
 * returned.
 *
 * Returns: the #GValue of the field
 */
GType
vvas_structure_get_field_type (const VvasStructure * structure,
    const gchar * fieldname)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, G_TYPE_INVALID);
  g_return_val_if_fail (fieldname != NULL, G_TYPE_INVALID);

  field = vvas_structure_get_field (structure, fieldname);
  if (field == NULL)
    return G_TYPE_INVALID;

  return G_VALUE_TYPE (&field->value);
}

/**
 * vvas_structure_n_fields:
 * @structure: a #VvasStructure
 *
 * Get the number of fields in the structure.
 *
 * Returns: the number of fields in the structure
 */
gint
vvas_structure_n_fields (const VvasStructure * structure)
{
  g_return_val_if_fail (structure != NULL, 0);

  return VVAS_STRUCTURE_LEN (structure);
}

/**
 * vvas_structure_nth_field_name:
 * @structure: a #VvasStructure
 * @index: the index to get the name of
 *
 * Get the name of the given field number, counting from 0 onwards.
 *
 * Returns: the name of the given field number
 */
const gchar *
vvas_structure_nth_field_name (const VvasStructure * structure, guint index)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, NULL);
  g_return_val_if_fail (index < VVAS_STRUCTURE_LEN (structure), NULL);

  field = VVAS_STRUCTURE_FIELD (structure, index);

  return g_quark_to_string (field->name);
}

/**
 * vvas_structure_has_field:
 * @structure: a #VvasStructure
 * @fieldname: the name of a field
 *
 * Check if @structure contains a field named @fieldname.
 *
 * Returns: %TRUE if the structure contains a field with the given name
 */
gboolean
vvas_structure_has_field (const VvasStructure * structure,
    const gchar * fieldname)
{
  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);

  return vvas_structure_id_has_field (structure,
      g_quark_from_string (fieldname));
}

/**
 * vvas_structure_has_field_typed:
 * @structure: a #VvasStructure
 * @fieldname: the name of a field
 * @type: the type of a value
 *
 * Check if @structure contains a field named @fieldname and with GType @type.
 *
 * Returns: %TRUE if the structure contains a field with the given name and type
 */
gboolean
vvas_structure_has_field_typed (const VvasStructure * structure,
    const gchar * fieldname, GType type)
{
  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);

  return vvas_structure_id_has_field_typed (structure,
      g_quark_from_string (fieldname), type);
}

/* utility functions */

/**
 * vvas_structure_get_boolean:
 * @structure: a #VvasStructure
 * @fieldname: the name of a field
 * @value: (out): a pointer to a #gboolean to set
 *
 * Sets the boolean pointed to by @value corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: %TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a boolean, this
 * function returns %FALSE.
 */
gboolean
vvas_structure_get_boolean (const VvasStructure * structure,
    const gchar * fieldname, gboolean * value)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);

  field = vvas_structure_get_field (structure, fieldname);

  if (field == NULL || G_VALUE_TYPE (&field->value) != G_TYPE_BOOLEAN)
    return FALSE;

  *value = g_value_get_boolean (&field->value);

  return TRUE;
}

/**
 * vvas_structure_get_int:
 * @structure: a #VvasStructure
 * @fieldname: the name of a field
 * @value: (out): a pointer to an int to set
 *
 * Sets the int pointed to by @value corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: %TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain an int, this function
 * returns %FALSE.
 */
gboolean
vvas_structure_get_int (const VvasStructure * structure,
    const gchar * fieldname, gint * value)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = vvas_structure_get_field (structure, fieldname);

  if (field == NULL || G_VALUE_TYPE (&field->value) != G_TYPE_INT)
    return FALSE;

  *value = g_value_get_int (&field->value);

  return TRUE;
}

/**
 * vvas_structure_get_uint:
 * @structure: a #VvasStructure
 * @fieldname: the name of a field
 * @value: (out): a pointer to a uint to set
 *
 * Sets the uint pointed to by @value corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: %TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a uint, this function
 * returns %FALSE.
 */
gboolean
vvas_structure_get_uint (const VvasStructure * structure,
    const gchar * fieldname, guint * value)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = vvas_structure_get_field (structure, fieldname);

  if (field == NULL || G_VALUE_TYPE (&field->value) != G_TYPE_UINT)
    return FALSE;

  *value = g_value_get_uint (&field->value);

  return TRUE;
}

/**
 * vvas_structure_get_int64:
 * @structure: a #VvasStructure
 * @fieldname: the name of a field
 * @value: (out): a pointer to a #gint64 to set
 *
 * Sets the #gint64 pointed to by @value corresponding to the value of the
 * given field. Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: %TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a #gint64, this function
 * returns %FALSE.
 *
 * Since: 1.4
 */
gboolean
vvas_structure_get_int64 (const VvasStructure * structure,
    const gchar * fieldname, gint64 * value)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = vvas_structure_get_field (structure, fieldname);

  if (field == NULL || G_VALUE_TYPE (&field->value) != G_TYPE_INT64)
    return FALSE;

  *value = g_value_get_int64 (&field->value);

  return TRUE;
}

/**
 * vvas_structure_get_uint64:
 * @structure: a #VvasStructure
 * @fieldname: the name of a field
 * @value: (out): a pointer to a #guint64 to set
 *
 * Sets the #guint64 pointed to by @value corresponding to the value of the
 * given field. Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: %TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a #guint64, this function
 * returns %FALSE.
 *
 * Since: 1.4
 */
gboolean
vvas_structure_get_uint64 (const VvasStructure * structure,
    const gchar * fieldname, guint64 * value)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = vvas_structure_get_field (structure, fieldname);

  if (field == NULL || G_VALUE_TYPE (&field->value) != G_TYPE_UINT64)
    return FALSE;

  *value = g_value_get_uint64 (&field->value);

  return TRUE;
}

/**
 * vvas_structure_get_double:
 * @structure: a #VvasStructure
 * @fieldname: the name of a field
 * @value: (out): a pointer to a gdouble to set
 *
 * Sets the double pointed to by @value corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: %TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a double, this
 * function returns %FALSE.
 */
gboolean
vvas_structure_get_double (const VvasStructure * structure,
    const gchar * fieldname, gdouble * value)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = vvas_structure_get_field (structure, fieldname);

  if (field == NULL || G_VALUE_TYPE (&field->value) != G_TYPE_DOUBLE)
    return FALSE;

  *value = g_value_get_double (&field->value);

  return TRUE;
}

/**
 * vvas_structure_get_string:
 * @structure: a #VvasStructure
 * @fieldname: the name of a field
 *
 * Finds the field corresponding to @fieldname, and returns the string
 * contained in the field's value.  Caller is responsible for making
 * sure the field exists and has the correct type.
 *
 * The string should not be modified, and remains valid until the next
 * call to a vvas_structure_*() function with the given structure.
 *
 * Returns: (nullable): a pointer to the string or %NULL when the
 * field did not exist or did not contain a string.
 */
const gchar *
vvas_structure_get_string (const VvasStructure * structure,
    const gchar * fieldname)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, NULL);
  g_return_val_if_fail (fieldname != NULL, NULL);

  field = vvas_structure_get_field (structure, fieldname);

  if (field == NULL || G_VALUE_TYPE (&field->value) != G_TYPE_STRING)
    return NULL;

  return g_value_get_string (&field->value);
}

/**
 * vvas_structure_get_enum:
 * @structure: a #VvasStructure
 * @fieldname: the name of a field
 * @enumtype: the enum type of a field
 * @value: (out): a pointer to an int to set
 *
 * Sets the int pointed to by @value corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists,
 * has the correct type and that the enumtype is correct.
 *
 * Returns: %TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain an enum of the given
 * type, this function returns %FALSE.
 */
gboolean
vvas_structure_get_enum (const VvasStructure * structure,
    const gchar * fieldname, GType enumtype, gint * value)
{
  VvasStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (enumtype != G_TYPE_INVALID, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = vvas_structure_get_field (structure, fieldname);

  if (field == NULL)
    return FALSE;
  if (!G_TYPE_CHECK_VALUE_TYPE (&field->value, enumtype))
    return FALSE;

  *value = g_value_get_enum (&field->value);

  return TRUE;
}

gboolean static
vvas_structure_get_valist (const VvasStructure * structure,
    const char *first_fieldname, va_list args)
{
  const char *field_name;
  GType expected_type = G_TYPE_INVALID;

  g_return_val_if_fail (first_fieldname != NULL, FALSE);

  field_name = first_fieldname;
  while (field_name) {
    const GValue *val = NULL;
    gchar *err = NULL;

    expected_type = va_arg (args, GType);

    val = vvas_structure_get_value (structure, field_name);

    if (val == NULL)
      goto no_such_field;

    if (G_VALUE_TYPE (val) != expected_type)
      goto wrong_type;

    G_VALUE_LCOPY (val, args, 0, &err);
    if (err) {
      g_warning ("%s: %s", G_STRFUNC, err);
      g_free (err);
      return FALSE;
    }

    field_name = va_arg (args, const gchar *);
  }

  return TRUE;

/* ERRORS */
no_such_field:
  {
    return FALSE;
  }
wrong_type:
  {
    return FALSE;
  }
}

gboolean
vvas_structure_get (const VvasStructure * structure,
    const char *first_fieldname, ...)
{
  gboolean ret;
  va_list args;

  g_return_val_if_fail (first_fieldname != NULL, FALSE);

  va_start (args, first_fieldname);
  ret = vvas_structure_get_valist (structure, first_fieldname, args);
  va_end (args);

  return ret;
}

gboolean
vvas_structure_get_array (VvasStructure * structure,
    const gchar * fieldname, GArray ** array)
{
  VvasStructureField *field;
  GValue val = G_VALUE_INIT;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (array != NULL, FALSE);

  field = vvas_structure_get_field (structure, fieldname);

  if (field == NULL || G_VALUE_TYPE (&field->value) != G_TYPE_ARRAY) {
    return FALSE;
  }

  g_value_init (&val, G_TYPE_ARRAY);

  if (g_value_transform (&field->value, &val)) {
    *array = g_value_get_boxed (&val);
    return TRUE;
  }

  g_value_unset (&val);
  return FALSE;
}

void
vvas_structure_print (const VvasStructure * s)
{
  guint strcuture_len = VVAS_STRUCTURE_LEN (s);
  guint idx;
  g_print ("Structure name: %s\n", g_quark_to_string (s->name));
  g_print ("Structure has %u fields\n", strcuture_len);
  for (idx = 0; idx < strcuture_len; idx++) {
    GValue val = s->fields[idx].value;
    g_print ("\t%s: ", g_quark_to_string (s->fields[idx].name));
    g_print ("(%s) ", G_VALUE_TYPE_NAME (&val));
    if (g_value_type_transformable (G_VALUE_TYPE (&val), G_TYPE_STRING)) {
      GValue val_str = G_VALUE_INIT;
      g_value_init (&val_str, G_TYPE_STRING);
      g_value_transform (&val, &val_str);
      g_print ("%s\n", g_value_get_string (&val_str));
      g_value_unset (&val_str);
    } else {
      g_print ("\n");
    }
  }
  g_print ("\n");
}
