/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012 LunarG Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#ifndef I965_3D_GEN6_H
#define I965_3D_GEN6_H

#include "i965_common.h"
#include "i965_gpe_gen6.h"

struct pipe_draw_info;
struct intel_bo;
struct i965_context;
struct i965_cp;

enum i965_3d_atom_state {
   /* driver states */
   I965_3D_DRV_BATCH_BUFFER,
   I965_3D_DRV_HW_CONTEXT,
   I965_3D_DRV_SHADER_CACHE,

   /* GEN6 states */
   I965_3D_GEN6_STATE_BASE_ADDRESS,
   I965_3D_GEN6_CLIP_VIEWPORT,
   I965_3D_GEN6_SF_VIEWPORT,
   I965_3D_GEN6_COLOR_CALC_STATE,
   I965_3D_GEN6_DEPTH_STENCIL_STATE,
   I965_3D_GEN6_BLEND_STATE,
   I965_3D_GEN6_CC_VIEWPORT,
   I965_3D_GEN6_BINDING_TABLE_STATE,
   I965_3D_GEN6_SURFACE_STATE,
   I965_3D_GEN6_SAMPLER_STATE,

   I965_3D_STATE_COUNT,
};

enum i965_3d_atom_dirty_flags {
   I965_3D_DIRTY_DRV_BATCH_BUFFER            = 1 << I965_3D_DRV_BATCH_BUFFER,
   I965_3D_DIRTY_DRV_HW_CONTEXT              = 1 << I965_3D_DRV_HW_CONTEXT,
   I965_3D_DIRTY_DRV_SHADER_CACHE            = 1 << I965_3D_DRV_SHADER_CACHE,

   I965_3D_DIRTY_GEN6_STATE_BASE_ADDRESS     = 1 << I965_3D_GEN6_STATE_BASE_ADDRESS,
   I965_3D_DIRTY_GEN6_CLIP_VIEWPORT          = 1 << I965_3D_GEN6_CLIP_VIEWPORT,
   I965_3D_DIRTY_GEN6_SF_VIEWPORT            = 1 << I965_3D_GEN6_SF_VIEWPORT,
   I965_3D_DIRTY_GEN6_COLOR_CALC_STATE       = 1 << I965_3D_GEN6_COLOR_CALC_STATE,
   I965_3D_DIRTY_GEN6_DEPTH_STENCIL_STATE    = 1 << I965_3D_GEN6_DEPTH_STENCIL_STATE,
   I965_3D_DIRTY_GEN6_BLEND_STATE            = 1 << I965_3D_GEN6_BLEND_STATE,
   I965_3D_DIRTY_GEN6_CC_VIEWPORT            = 1 << I965_3D_GEN6_CC_VIEWPORT,
   I965_3D_DIRTY_GEN6_BINDING_TABLE_STATE    = 1 << I965_3D_GEN6_BINDING_TABLE_STATE,
   I965_3D_DIRTY_GEN6_SURFACE_STATE          = 1 << I965_3D_GEN6_SURFACE_STATE,
   I965_3D_DIRTY_GEN6_SAMPLER_STATE          = 1 << I965_3D_GEN6_SAMPLER_STATE,

   I965_3D_DIRTY_ALL                         = 0xffffffff,
};

/**
 * GEN6 specific states.
 */
struct i965_3d_gen6 {
   uint32_t CLIP_VIEWPORT;
   uint32_t SF_VIEWPORT;
   uint32_t SCISSOR_RECT;
   uint32_t COLOR_CALC_STATE;
   uint32_t DEPTH_STENCIL_STATE;
   uint32_t BLEND_STATE;
   uint32_t CC_VIEWPORT;

   struct {
      uint32_t BINDING_TABLE_STATE;
      uint32_t SURFACE_STATE[I965_MAX_VS_SURFACES];
      uint32_t SAMPLER_STATE;
      uint32_t SAMPLER_BORDER_COLOR_STATE[I965_MAX_SAMPLERS];
   } vs;

   struct {
      uint32_t BINDING_TABLE_STATE;
      uint32_t SURFACE_STATE[I965_MAX_GS_SURFACES];
   } gs;

   struct {
      uint32_t BINDING_TABLE_STATE;
      uint32_t SURFACE_STATE[I965_MAX_WM_SURFACES];
      uint32_t SAMPLER_STATE;
      uint32_t SAMPLER_BORDER_COLOR_STATE[I965_MAX_SAMPLERS];
   } wm;

   boolean need_wa_flush;
};

/**
 * 3D context.
 */
struct i965_3d {
   struct i965_cp *cp;
   int gen;

   boolean new_batch;
   uint32_t shader_cache_seqno;
   struct intel_bo *workaround_bo;

   struct {
      struct pipe_query *query;
      unsigned mode;
   } render_condition;

   struct list_head occlusion_queries;
   struct list_head timer_queries;
   struct list_head prim_queries;

   struct i965_3d_gen6 gen6;

   const struct i965_gpe_gen6 *gpe;

   int (*upload_context)(struct i965_3d *hw3d,
                         const struct i965_context *i965,
                         boolean dry_run);

   int (*draw)(struct i965_3d *hw3d, const struct pipe_draw_info *info,
               boolean dry_run);

   int (*flush)(struct i965_3d *hw3d, boolean dry_run);

   int (*write_timestamp)(struct i965_3d *hw3d, struct intel_bo *bo,
                          int index, boolean dry_run);

   int (*write_depth_count)(struct i965_3d *hw3d, struct intel_bo *bo,
                            int index, boolean dry_run);

   void (*dump)(struct i965_3d *hw3d);
};

/**
 * An atom tracks what HW states need to be uploaded when a driver state
 * changes.
 */
struct i965_3d_atom {
   const char *name;
   uint32_t pipe_dirty;
   uint32_t hw3d_dirty;
   uint32_t hw3d_dirty_set;
   void (*upload)(struct i965_3d *hw3d, const struct i965_context *i965);
   int (*size)(struct i965_3d *hw3d, const struct i965_context *i965);
};

extern const struct i965_3d_atom gen6_atom_extra_size;
extern const struct i965_3d_atom gen6_atom_CLIP_VIEWPORT;
extern const struct i965_3d_atom gen6_atom_SF_VIEWPORT;
extern const struct i965_3d_atom gen6_atom_invariant_states;
extern const struct i965_3d_atom gen6_atom_STATE_BASE_ADDRESS;
extern const struct i965_3d_atom gen6_atom_CC_VIEWPORT;
extern const struct i965_3d_atom gen6_atom_3DSTATE_VIEWPORT_STATE_POINTERS;
extern const struct i965_3d_atom gen6_atom_3DSTATE_URB;
extern const struct i965_3d_atom gen6_atom_BLEND_STATE;
extern const struct i965_3d_atom gen6_atom_COLOR_CALC_STATE;
extern const struct i965_3d_atom gen6_atom_DEPTH_STENCIL_STATE;
extern const struct i965_3d_atom gen6_atom_3DSTATE_CC_STATE_POINTERS;
extern const struct i965_3d_atom gen6_atom_const_buffers;
extern const struct i965_3d_atom gen6_atom_color_buffers;
extern const struct i965_3d_atom gen6_atom_textures;
extern const struct i965_3d_atom gen6_atom_sol_surfaces;
extern const struct i965_3d_atom gen6_atom_BINDING_TABLE_STATE;
extern const struct i965_3d_atom gen6_atom_samplers;
extern const struct i965_3d_atom gen6_atom_3DSTATE_SAMPLER_STATE_POINTERS;
extern const struct i965_3d_atom gen6_atom_multisample_states;
extern const struct i965_3d_atom gen6_atom_vs;
extern const struct i965_3d_atom gen6_atom_gs;
extern const struct i965_3d_atom gen6_atom_3DSTATE_CLIP;
extern const struct i965_3d_atom gen6_atom_3DSTATE_SF;
extern const struct i965_3d_atom gen6_atom_wm;
extern const struct i965_3d_atom gen6_atom_scissor_states;
extern const struct i965_3d_atom gen6_atom_3DSTATE_BINDING_TABLE_POINTERS;
extern const struct i965_3d_atom gen6_atom_depth_buffer;
extern const struct i965_3d_atom gen6_atom_poly_stipple;
extern const struct i965_3d_atom gen6_atom_3DSTATE_LINE_STIPPLE_PATTERN;
extern const struct i965_3d_atom gen6_atom_3DSTATE_AA_LINE_PARAMETERS;
extern const struct i965_3d_atom gen6_atom_3DSTATE_DRAWING_RECTANGLE;
extern const struct i965_3d_atom gen6_atom_3DSTATE_GS_SVB_INDEX;
extern const struct i965_3d_atom gen6_atom_3DSTATE_INDEX_BUFFER;
extern const struct i965_3d_atom gen6_atom_vertices;

int
i965_3d_upload_atoms(struct i965_3d *hw3d, const struct i965_context *i965,
                     const struct i965_3d_atom **atoms, int num_atoms,
                     boolean dry_run);

int
i965_3d_flush_gen6(struct i965_3d *hw3d, boolean dry_run);

int
i965_3d_write_timestamp_gen6(struct i965_3d *hw3d, struct intel_bo *bo,
                             int index, boolean dry_run);

int
i965_3d_write_depth_count_gen6(struct i965_3d *hw3d, struct intel_bo *bo,
                               int index, boolean dry_run);

void
i965_3d_dump_gen6(struct i965_3d *hw3d);

void
i965_3d_init_gen6(struct i965_3d *hw3d);

#endif /* I965_3D_GEN6_H */
