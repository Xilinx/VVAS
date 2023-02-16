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

#ifndef __VVAS_STRUCTURE_H__
#define __VVAS_STRUCTURE_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _VvasStructField {
  GQuark name;  //name of the field
  GValue value; //value of the field
}VvasStructureField;


typedef struct _vvas_struct {
  GQuark name;        //name of the structure
  guint fields_len;   // Number of valid items in fields
  guint fields_alloc; // Allocated items in fields

  VvasStructureField *fields;

  VvasStructureField arr[1];
}VvasStructure;

VvasStructure * vvas_structure_new_empty (const gchar * name) G_GNUC_MALLOC;

VvasStructure * vvas_structure_new (const gchar * name,
                                    const gchar * firstfield,
                                    ...) G_GNUC_NULL_TERMINATED  G_GNUC_MALLOC;

VvasStructure * vvas_structure_copy (const VvasStructure  * structure) G_GNUC_MALLOC;

void vvas_structure_free (VvasStructure * structure);

const gchar * vvas_structure_get_name (const VvasStructure  * structure);

gboolean vvas_structure_has_name (const VvasStructure * structure,
                                  const gchar         * name);

void vvas_structure_set_name (VvasStructure * structure,
                              const gchar * name);

void vvas_structure_set_value (VvasStructure * structure,
                               const gchar * fieldname,
                               const GValue * value);

void vvas_structure_set (VvasStructure * structure,
                         const gchar   * fieldname,
                         ...) G_GNUC_NULL_TERMINATED;

gboolean vvas_structure_get (const VvasStructure * structure,
                             const char          * first_fieldname,
                             ...) G_GNUC_NULL_TERMINATED;

const GValue * vvas_structure_get_value (const VvasStructure  * structure,
                                         const gchar          * fieldname);

void vvas_structure_remove_field (VvasStructure * structure,
                                  const gchar   * fieldname);

void vvas_structure_remove_fields (VvasStructure * structure,
                                   const gchar   * fieldname,
                                   ...) G_GNUC_NULL_TERMINATED;

void vvas_structure_remove_all_fields (VvasStructure * structure);


GType vvas_structure_get_field_type (const VvasStructure * structure,
                                     const gchar         * fieldname);

gint vvas_structure_n_fields (const VvasStructure  * structure);


const gchar * vvas_structure_nth_field_name (const VvasStructure  * structure,
                                             guint                 index);

gboolean vvas_structure_has_field (const VvasStructure * structure,
                                   const gchar         * fieldname);

gboolean vvas_structure_has_field_typed (const VvasStructure * structure,
                                         const gchar         * fieldname,
                                         GType                 type);

/* utility functions */
gboolean vvas_structure_get_boolean (const VvasStructure * structure,
                                     const gchar         * fieldname,
                                     gboolean            * value);

gboolean vvas_structure_get_int (const VvasStructure * structure,
                                 const gchar         * fieldname,
                                 gint                * value);

gboolean vvas_structure_get_uint (const VvasStructure * structure,
                                  const gchar         * fieldname,
                                  guint               * value);

gboolean vvas_structure_get_int64 (const VvasStructure * structure,
                                   const gchar         * fieldname,
                                   gint64              * value);

gboolean vvas_structure_get_uint64 (const VvasStructure * structure,
                                    const gchar         * fieldname,
                                    guint64             * value);

gboolean vvas_structure_get_double (const VvasStructure  * structure,
                                    const gchar          * fieldname,
                                    gdouble              * value);

const gchar * vvas_structure_get_string (const VvasStructure  * structure,
                                         const gchar          * fieldname);

gboolean vvas_structure_get_enum (const VvasStructure  * structure,
                                  const gchar          * fieldname,
                                  GType                  enumtype,
                                  gint                 * value);

gboolean vvas_structure_get_array (VvasStructure        * structure,
                                   const gchar          * fieldname,
                                   GArray              ** array);

void vvas_structure_print (const VvasStructure *s);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(VvasStructure, vvas_structure_free)

G_END_DECLS

#endif
