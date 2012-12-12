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

#include "brw_defines.h"
#include "intel_reg.h"

#include "util/u_dual_blend.h"
#include "i965_shader.h"
#include "i965_common.h"
#include "i965_context.h"
#include "i965_cp.h"
#include "i965_resource.h"
#include "i965_state.h"
#include "i965_gpe_gen6.h"
#include "i965_3d.h"
#include "i965_3d_gen6.h"

/**
 * This should be called for all non-pipelined state changes on GEN6.
 *
 * \see intel_emit_post_sync_nonzero_flush()
 */
static void
wa_post_sync_nonzero_flush(struct i965_3d *hw3d)
{
   assert(hw3d->gen == 6);

   /* emit once */
   if (!hw3d->gen6.need_wa_flush)
      return;
   hw3d->gen6.need_wa_flush = FALSE;

   hw3d->gpe->emit_PIPE_CONTROL(hw3d->gpe, hw3d->cp,
         PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STALL_AT_SCOREBOARD,
         NULL, 0, FALSE);

   hw3d->gpe->emit_PIPE_CONTROL(hw3d->gpe, hw3d->cp,
         PIPE_CONTROL_WRITE_IMMEDIATE,
         hw3d->workaround_bo, 0, FALSE);
}

static void
gen6_upload_extra_size(struct i965_3d *hw3d,
                       const struct i965_context *i965)
{
}

static int
gen6_size_extra_size(struct i965_3d *hw3d,
                     const struct i965_context *i965)
{
   /* length of wa_post_sync_nonzero_flush() */
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_PIPE_CONTROL, 1) * 2;
}

const struct i965_3d_atom gen6_atom_extra_size = {
   .name = "GEN6 extra size",
   .pipe_dirty = I965_DIRTY_ALL,
   .hw3d_dirty = I965_3D_DIRTY_ALL,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_extra_size,
   .size = gen6_size_extra_size,
};

static void
gen6_upload_CLIP_VIEWPORT(struct i965_3d *hw3d,
                          const struct i965_context *i965)
{
   hw3d->gen6.CLIP_VIEWPORT =
      hw3d->gpe->emit_CLIP_VIEWPORT(hw3d->gpe, hw3d->cp, &i965->viewport, 1);
}

static int
gen6_size_CLIP_VIEWPORT(struct i965_3d *hw3d,
                        const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_CLIP_VIEWPORT, 1);
}

/**
 * \see gen6_clip_vp
 */
const struct i965_3d_atom gen6_atom_CLIP_VIEWPORT = {
   .name = "CLIP_VIEWPORT",
   .pipe_dirty = I965_DIRTY_VIEWPORT,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_CLIP_VIEWPORT,
   .upload = gen6_upload_CLIP_VIEWPORT,
   .size = gen6_size_CLIP_VIEWPORT,
};

static void
gen6_upload_SF_VIEWPORT(struct i965_3d *hw3d,
                        const struct i965_context *i965)
{
   hw3d->gen6.SF_VIEWPORT =
      hw3d->gpe->emit_SF_VIEWPORT(hw3d->gpe, hw3d->cp, &i965->viewport, 1);
}

static int
gen6_size_SF_VIEWPORT(struct i965_3d *hw3d,
                      const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_SF_VIEWPORT, 1);
}

/**
 * \see gen6_sf_vp
 */
const struct i965_3d_atom gen6_atom_SF_VIEWPORT = {
   .name = "SF_VIEWPORT",
   .pipe_dirty = I965_DIRTY_VIEWPORT,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_SF_VIEWPORT,
   .upload = gen6_upload_SF_VIEWPORT,
   .size = gen6_size_SF_VIEWPORT,
};

static void
gen6_upload_invariant_states(struct i965_3d *hw3d,
                             const struct i965_context *i965)
{
   if (hw3d->gen == 6)
      wa_post_sync_nonzero_flush(hw3d);

   hw3d->gpe->emit_PIPELINE_SELECT(hw3d->gpe, hw3d->cp, FALSE);

   if (hw3d->gen == 6) {
      int i;

      for (i = 0; i < 4; i++)
         hw3d->gpe->emit_3DSTATE_GS_SVB_INDEX(hw3d->gpe, hw3d->cp, i, 0, 0xffffffff);
   }

   hw3d->gpe->emit_STATE_SIP(hw3d->gpe, hw3d->cp, 0);
   hw3d->gpe->emit_3DSTATE_VF_STATISTICS(hw3d->gpe, hw3d->cp, FALSE);
}

static int
gen6_size_invariant_states(struct i965_3d *hw3d,
                           const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_PIPELINE_SELECT, 1) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_GS_SVB_INDEX, 1) * 4 +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_STATE_SIP, 1) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_VF_STATISTICS, 1);
}

/**
 * \see brw_invariant_state
 */
const struct i965_3d_atom gen6_atom_invariant_states = {
   .name = "invariant states",
   .pipe_dirty = 0,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_invariant_states,
   .size = gen6_size_invariant_states,
};

static void
gen6_upload_STATE_BASE_ADDRESS(struct i965_3d *hw3d,
                               const struct i965_context *i965)
{
   if (hw3d->gen == 6)
      wa_post_sync_nonzero_flush(hw3d);

   hw3d->gpe->emit_STATE_BASE_ADDRESS(hw3d->gpe, hw3d->cp,
         NULL, hw3d->cp->bo, hw3d->cp->bo, NULL, i965->shader_cache->bo,
         0, 0xfffff000, 0, 0);
}

static int
gen6_size_STATE_BASE_ADDRESS(struct i965_3d *hw3d,
                             const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_STATE_BASE_ADDRESS, 1);
}

/**
 * \see brw_state_base_address
 */
const struct i965_3d_atom gen6_atom_STATE_BASE_ADDRESS = {
   .name = "STATE_BASE_ADDRESS",
   .pipe_dirty = 0,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER |
                 I965_3D_DIRTY_DRV_SHADER_CACHE,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_STATE_BASE_ADDRESS,
   .upload = gen6_upload_STATE_BASE_ADDRESS,
   .size = gen6_size_STATE_BASE_ADDRESS,
};

static void
gen6_upload_CC_VIEWPORT(struct i965_3d *hw3d,
                        const struct i965_context *i965)
{
   hw3d->gen6.CC_VIEWPORT = hw3d->gpe->emit_CC_VIEWPORT(hw3d->gpe, hw3d->cp,
         &i965->viewport, 1, i965->rasterizer->depth_clip);
}

static int
gen6_size_CC_VIEWPORT(struct i965_3d *hw3d,
                      const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_CC_VIEWPORT, 1);
}

/**
 * \see brw_cc_vp
 */
const struct i965_3d_atom gen6_atom_CC_VIEWPORT = {
   .name = "CC_VIEWPORT",
   .pipe_dirty = I965_DIRTY_VIEWPORT |
                 I965_DIRTY_RASTERIZER,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_CC_VIEWPORT,
   .upload = gen6_upload_CC_VIEWPORT,
   .size = gen6_size_CC_VIEWPORT,
};

static void
gen6_upload_3DSTATE_VIEWPORT_STATE_POINTERS(struct i965_3d *hw3d,
                                            const struct i965_context *i965)
{
   hw3d->gpe->emit_3DSTATE_VIEWPORT_STATE_POINTERS(hw3d->gpe, hw3d->cp,
         hw3d->gen6.CLIP_VIEWPORT,
         hw3d->gen6.SF_VIEWPORT,
         hw3d->gen6.CC_VIEWPORT);
}

static int
gen6_size_3DSTATE_VIEWPORT_STATE_POINTERS(struct i965_3d *hw3d,
                                          const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_VIEWPORT_STATE_POINTERS, 1);
}

/**
 * \see gen6_viewport_state
 */
const struct i965_3d_atom gen6_atom_3DSTATE_VIEWPORT_STATE_POINTERS = {
   .name = "3DSTATE_VIEWPORT_STATE_POINTERS",
   .pipe_dirty = 0,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER |
                 I965_3D_DIRTY_GEN6_STATE_BASE_ADDRESS |
                 I965_3D_DIRTY_GEN6_CLIP_VIEWPORT |
                 I965_3D_DIRTY_GEN6_SF_VIEWPORT |
                 I965_3D_DIRTY_GEN6_CC_VIEWPORT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_VIEWPORT_STATE_POINTERS,
   .size = gen6_size_3DSTATE_VIEWPORT_STATE_POINTERS,
};

static void
gen6_upload_3DSTATE_URB(struct i965_3d *hw3d,
                        const struct i965_context *i965)
{
   const struct i965_shader *vs = i965->vs->shader;
   int vs_entry_size, vs_num_entries;
   int gs_entry_size, gs_num_entries;

   /* VUEs are written by both VF and VS */
   vs_entry_size = MAX2(i965->vertex_elements->num_elements, vs->out.count);
   if (!vs_entry_size)
      vs_entry_size = 1;
   vs_entry_size *= sizeof(float) * 4;

   gs_entry_size = vs_entry_size;

   vs_num_entries = (i965->urb.size * 1024) / vs_entry_size;
   if (i965->gs) {
      vs_num_entries /= 2;
      gs_num_entries = vs_num_entries;
   }
   else {
      gs_num_entries = 0;
   }

   if (vs_num_entries > i965->urb.max_vs_entries)
      vs_num_entries = i965->urb.max_vs_entries;
   if (gs_num_entries > i965->urb.max_gs_entries)
      gs_num_entries = i965->urb.max_gs_entries;

   hw3d->gpe->emit_3DSTATE_URB(hw3d->gpe, hw3d->cp,
         vs_entry_size, vs_num_entries,
         gs_entry_size, gs_num_entries);
}

static int
gen6_size_3DSTATE_URB(struct i965_3d *hw3d,
                      const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_URB, 1);
}

/**
 * \see gen6_urb
 */
const struct i965_3d_atom gen6_atom_3DSTATE_URB = {
   .name = "3DSTATE_URB",
   .pipe_dirty = I965_DIRTY_VERTEX_ELEMENTS |
                 I965_DIRTY_VS |
                 I965_DIRTY_GS,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_URB,
   .size = gen6_size_3DSTATE_URB,
};

static void
gen6_upload_BLEND_STATE(struct i965_3d *hw3d,
                        const struct i965_context *i965)
{
   hw3d->gen6.BLEND_STATE = hw3d->gpe->emit_BLEND_STATE(hw3d->gpe, hw3d->cp,
         i965->blend, &i965->framebuffer,
         &i965->depth_stencil_alpha->alpha);
}

static int
gen6_size_BLEND_STATE(struct i965_3d *hw3d,
                      const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_BLEND_STATE, i965->framebuffer.nr_cbufs);
}

/**
 * \see gen6_blend_state
 */
const struct i965_3d_atom gen6_atom_BLEND_STATE = {
   .name = "BLEND_STATE",
   .pipe_dirty = I965_DIRTY_BLEND |
                 I965_DIRTY_DEPTH_STENCIL_ALPHA |
                 I965_DIRTY_FRAMEBUFFER,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_BLEND_STATE,
   .upload = gen6_upload_BLEND_STATE,
   .size = gen6_size_BLEND_STATE,
};

static void
gen6_upload_COLOR_CALC_STATE(struct i965_3d *hw3d,
                             const struct i965_context *i965)
{
   hw3d->gen6.COLOR_CALC_STATE =
      hw3d->gpe->emit_COLOR_CALC_STATE(hw3d->gpe, hw3d->cp,
            &i965->stencil_ref,
            i965->depth_stencil_alpha->alpha.ref_value,
            &i965->blend_color);
}

static int
gen6_size_COLOR_CALC_STATE(struct i965_3d *hw3d,
                           const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_COLOR_CALC_STATE, 1);
}

/**
 * \see gen6_color_calc_state
 */
const struct i965_3d_atom gen6_atom_COLOR_CALC_STATE = {
   .name = "COLOR_CALC_STATE",
   .pipe_dirty = I965_DIRTY_STENCIL_REF |
                 I965_DIRTY_DEPTH_STENCIL_ALPHA |
                 I965_DIRTY_BLEND_COLOR,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_COLOR_CALC_STATE,
   .upload = gen6_upload_COLOR_CALC_STATE,
   .size = gen6_size_COLOR_CALC_STATE,
};

static void
gen6_upload_DEPTH_STENCIL_STATE(struct i965_3d *hw3d,
                                const struct i965_context *i965)
{
   hw3d->gen6.DEPTH_STENCIL_STATE =
      hw3d->gpe->emit_DEPTH_STENCIL_STATE(hw3d->gpe, hw3d->cp,
            i965->depth_stencil_alpha);
}

static int
gen6_size_DEPTH_STENCIL_STATE(struct i965_3d *hw3d,
                              const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_DEPTH_STENCIL_STATE, 1);
}

/**
 * \see gen6_depth_stencil_state
 */
const struct i965_3d_atom gen6_atom_DEPTH_STENCIL_STATE = {
   .name = "DEPTH_STENCIL_STATE",
   .pipe_dirty = I965_DIRTY_DEPTH_STENCIL_ALPHA,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_DEPTH_STENCIL_STATE,
   .upload = gen6_upload_DEPTH_STENCIL_STATE,
   .size = gen6_size_DEPTH_STENCIL_STATE,
};

static void
gen6_upload_3DSTATE_CC_STATE_POINTERS(struct i965_3d *hw3d,
                                      const struct i965_context *i965)
{
   hw3d->gpe->emit_3DSTATE_CC_STATE_POINTERS(hw3d->gpe, hw3d->cp,
         hw3d->gen6.BLEND_STATE,
         hw3d->gen6.DEPTH_STENCIL_STATE,
         hw3d->gen6.COLOR_CALC_STATE);
}

static int
gen6_size_3DSTATE_CC_STATE_POINTERS(struct i965_3d *hw3d,
                                    const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_CC_STATE_POINTERS, 1);
}

/**
 * \see gen6_cc_state_pointers
 */
const struct i965_3d_atom gen6_atom_3DSTATE_CC_STATE_POINTERS = {
   .name = "3DSTATE_CC_STATE_POINTERS",
   .pipe_dirty = 0,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER |
                 I965_3D_DIRTY_GEN6_STATE_BASE_ADDRESS |
                 I965_3D_DIRTY_GEN6_BLEND_STATE |
                 I965_3D_DIRTY_GEN6_COLOR_CALC_STATE |
                 I965_3D_DIRTY_GEN6_DEPTH_STENCIL_STATE,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_CC_STATE_POINTERS,
   .size = gen6_size_3DSTATE_CC_STATE_POINTERS,
};

static void
gen6_upload_const_buffers(struct i965_3d *hw3d,
                          const struct i965_context *i965)
{
   int type;
   int i;

   type = PIPE_SHADER_VERTEX;
   for (i = 0; i < i965->constant_buffers[type].num_buffers; i++) {
      const struct pipe_constant_buffer *cbuf =
         &i965->constant_buffers[type].buffers[i];

      if (!cbuf->buffer)
         break;

      hw3d->gen6.vs.SURFACE_STATE[I965_VS_CONST_SURFACE(i)] =
         hw3d->gpe->emit_SURFACE_STATE(hw3d->gpe, hw3d->cp,
               NULL, NULL, cbuf, NULL, 0);
   }

   for (; i < I965_MAX_CONST_BUFFERS; i++)
      hw3d->gen6.vs.SURFACE_STATE[I965_VS_CONST_SURFACE(i)] = 0;

   /* WM */
   type = PIPE_SHADER_FRAGMENT;
   for (i = 0; i < i965->constant_buffers[type].num_buffers; i++) {
      const struct pipe_constant_buffer *cbuf =
         &i965->constant_buffers[type].buffers[i];

      if (!cbuf->buffer)
         break;

      hw3d->gen6.wm.SURFACE_STATE[I965_WM_CONST_SURFACE(i)] =
         hw3d->gpe->emit_SURFACE_STATE(hw3d->gpe, hw3d->cp,
               NULL, NULL, cbuf, NULL, 0);
   }

   for (; i < I965_MAX_CONST_BUFFERS; i++)
      hw3d->gen6.wm.SURFACE_STATE[I965_WM_CONST_SURFACE(i)] = 0;
}

static int
gen6_size_const_buffers(struct i965_3d *hw3d,
                        const struct i965_context *i965)
{
   /* treat the states as an array as they are allocated consecutively */
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_SURFACE_STATE, I965_MAX_CONST_BUFFERS * 2);
}

/**
 * \see brw_vs_pull_constants
 * \see brw_wm_pull_constants
 */
const struct i965_3d_atom gen6_atom_const_buffers = {
   .name = "constant buffers",
   .pipe_dirty = I965_DIRTY_CONSTANT_BUFFER,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_SURFACE_STATE,
   .upload = gen6_upload_const_buffers,
   .size = gen6_size_const_buffers,
};

static void
gen6_upload_color_buffers(struct i965_3d *hw3d,
                          const struct i965_context *i965)
{
   unsigned i;

   for (i = 0; i < i965->framebuffer.nr_cbufs; i++) {
      const struct pipe_surface *surface = i965->framebuffer.cbufs[i];

      hw3d->gen6.wm.SURFACE_STATE[I965_WM_DRAW_SURFACE(i)] =
         hw3d->gpe->emit_SURFACE_STATE(hw3d->gpe, hw3d->cp,
               surface, NULL, NULL, NULL, 0);
   }

   if (i == 0) {
      struct pipe_surface surface;

      memset(&surface, 0, sizeof(surface));
      surface.width = i965->framebuffer.width;
      surface.height = i965->framebuffer.height;

      hw3d->gen6.wm.SURFACE_STATE[I965_WM_DRAW_SURFACE(i)] =
         hw3d->gpe->emit_SURFACE_STATE(hw3d->gpe, hw3d->cp,
               &surface, NULL, NULL, NULL, 0);
      i++;
   }

   for (; i < I965_MAX_DRAW_BUFFERS; i++)
      hw3d->gen6.wm.SURFACE_STATE[I965_WM_DRAW_SURFACE(i)] = 0;
}

static int
gen6_size_color_buffers(struct i965_3d *hw3d,
                        const struct i965_context *i965)
{
   /* treat the states as an array as they are allocated consecutively */
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_SURFACE_STATE, i965->framebuffer.nr_cbufs);
}

/**
 * \see gen6_renderbuffer_surfaces
 */
const struct i965_3d_atom gen6_atom_color_buffers = {
   .name = "color buffers",
   .pipe_dirty = I965_DIRTY_FRAMEBUFFER,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_SURFACE_STATE,
   .upload = gen6_upload_color_buffers,
   .size = gen6_size_color_buffers,
};

static void
gen6_upload_textures(struct i965_3d *hw3d,
                     const struct i965_context *i965)
{
   unsigned i;

   /* VS */
   for (i = 0; i < i965->sampler_views[PIPE_SHADER_VERTEX].num_views; i++) {
      const struct pipe_sampler_view *view =
         i965->sampler_views[PIPE_SHADER_VERTEX].views[i];

      if (view) {
         hw3d->gen6.vs.SURFACE_STATE[I965_VS_TEXTURE_SURFACE(i)] =
            hw3d->gpe->emit_SURFACE_STATE(hw3d->gpe, hw3d->cp,
                  NULL, view, NULL, NULL, 0);
      }
      else {
         hw3d->gen6.vs.SURFACE_STATE[I965_VS_TEXTURE_SURFACE(i)] = 0;
      }
   }

   for (; i < I965_MAX_SAMPLER_VIEWS; i++)
      hw3d->gen6.vs.SURFACE_STATE[I965_VS_TEXTURE_SURFACE(i)] = 0;

   /* WM */
   for (i = 0; i < i965->sampler_views[PIPE_SHADER_FRAGMENT].num_views; i++) {
      const struct pipe_sampler_view *view =
         i965->sampler_views[PIPE_SHADER_FRAGMENT].views[i];

      if (view) {
         hw3d->gen6.wm.SURFACE_STATE[I965_WM_TEXTURE_SURFACE(i)] =
            hw3d->gpe->emit_SURFACE_STATE(hw3d->gpe, hw3d->cp,
                  NULL, view, NULL, NULL, 0);
      }
      else {
         hw3d->gen6.wm.SURFACE_STATE[I965_WM_TEXTURE_SURFACE(i)] = 0;
      }
   }

   for (; i < I965_MAX_SAMPLER_VIEWS; i++)
      hw3d->gen6.wm.SURFACE_STATE[I965_WM_TEXTURE_SURFACE(i)] = 0;
}

static int
gen6_size_textures(struct i965_3d *hw3d,
                   const struct i965_context *i965)
{
   const int num_views = i965->sampler_views[PIPE_SHADER_VERTEX].num_views +
                         i965->sampler_views[PIPE_SHADER_FRAGMENT].num_views;

   /* treat the states as an array as they are allocated consecutively */
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_SURFACE_STATE, num_views);
}

/**
 * \see brw_texture_surfaces
 */
const struct i965_3d_atom gen6_atom_textures = {
   .name = "textures",
   .pipe_dirty = I965_DIRTY_VERTEX_SAMPLER_VIEWS |
                 I965_DIRTY_FRAGMENT_SAMPLER_VIEWS,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_SURFACE_STATE,
   .upload = gen6_upload_textures,
   .size = gen6_size_textures,
};

static void
gen6_upload_sol_surfaces(struct i965_3d *hw3d,
                         const struct i965_context *i965)
{
   int i;

   for (i = 0; i < I965_MAX_SO_BINDINGS; i++)
      hw3d->gen6.gs.SURFACE_STATE[I965_GS_SO_SURFACE(i)] = 0;
}

static int
gen6_size_sol_surfaces(struct i965_3d *hw3d,
                       const struct i965_context *i965)
{
   return 0;
}

/**
 * \see gen6_sol_surface
 */
const struct i965_3d_atom gen6_atom_sol_surfaces = {
   .name = "SOL surfaces",
   .pipe_dirty = 0,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_SURFACE_STATE,
   .upload = gen6_upload_sol_surfaces,
   .size = gen6_size_sol_surfaces,
};

static void
gen6_upload_BINDING_TABLE_STATE(struct i965_3d *hw3d,
                                const struct i965_context *i965)
{
   int num_surfaces;

   num_surfaces = I965_MAX_VS_SURFACES;
   hw3d->gen6.vs.BINDING_TABLE_STATE =
      hw3d->gpe->emit_BINDING_TABLE_STATE(hw3d->gpe, hw3d->cp,
            hw3d->gen6.vs.SURFACE_STATE, num_surfaces);

   num_surfaces = I965_MAX_GS_SURFACES;
   hw3d->gen6.gs.BINDING_TABLE_STATE =
      hw3d->gpe->emit_BINDING_TABLE_STATE(hw3d->gpe, hw3d->cp,
            hw3d->gen6.gs.SURFACE_STATE, num_surfaces);

   num_surfaces = I965_MAX_WM_SURFACES;
   hw3d->gen6.wm.BINDING_TABLE_STATE =
      hw3d->gpe->emit_BINDING_TABLE_STATE(hw3d->gpe, hw3d->cp,
            hw3d->gen6.wm.SURFACE_STATE, num_surfaces);
}

static int
gen6_size_BINDING_TABLE_STATE(struct i965_3d *hw3d,
                              const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_BINDING_TABLE_STATE, I965_MAX_VS_SURFACES) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_BINDING_TABLE_STATE, I965_MAX_GS_SURFACES) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_BINDING_TABLE_STATE, I965_MAX_WM_SURFACES);
}

/**
 * \see brw_vs_binding_table
 * \see gen6_gs_binding_table
 * \see brw_wm_binding_table
 */
const struct i965_3d_atom gen6_atom_BINDING_TABLE_STATE = {
   .name = "BINDING_TABLE_STATE",
   .pipe_dirty = 0,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER |
                 I965_3D_DIRTY_GEN6_STATE_BASE_ADDRESS |
                 I965_3D_DIRTY_GEN6_SURFACE_STATE,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_BINDING_TABLE_STATE,
   .upload = gen6_upload_BINDING_TABLE_STATE,
   .size = gen6_size_BINDING_TABLE_STATE,
};

static void
gen6_upload_samplers(struct i965_3d *hw3d,
                     const struct i965_context *i965)
{
   unsigned sh;

   for (sh = 0; sh < PIPE_SHADER_TYPES; sh++) {
      uint32_t *sampler_offset, *border_color_offsets;
      int i;

      if (!i965->samplers[sh].num_samplers)
         continue;

      switch (sh) {
      case PIPE_SHADER_VERTEX:
         sampler_offset = &hw3d->gen6.vs.SAMPLER_STATE;
         border_color_offsets = hw3d->gen6.vs.SAMPLER_BORDER_COLOR_STATE;
         break;
      case PIPE_SHADER_FRAGMENT:
         sampler_offset = &hw3d->gen6.wm.SAMPLER_STATE;
         border_color_offsets = hw3d->gen6.wm.SAMPLER_BORDER_COLOR_STATE;
         break;
      default:
         sampler_offset = NULL;
         break;
      }
      if (!sampler_offset)
         continue;

      assert(i965->samplers[sh].num_samplers <=
             i965->sampler_views[sh].num_views);

      for (i = 0; i < i965->samplers[sh].num_samplers; i++) {
         const struct pipe_sampler_state *sampler =
            i965->samplers[sh].samplers[i];

         if (sampler) {
            border_color_offsets[i] =
               hw3d->gpe->emit_SAMPLER_BORDER_COLOR_STATE(hw3d->gpe,
                     hw3d->cp, &sampler->border_color);
         }
         else {
            border_color_offsets[i] = 0;
         }
      }

      *sampler_offset =
         hw3d->gpe->emit_SAMPLER_STATE(hw3d->gpe, hw3d->cp,
            (const struct pipe_sampler_state **) i965->samplers[sh].samplers,
            (const struct pipe_sampler_view **) i965->sampler_views[sh].views,
            border_color_offsets,
            i965->samplers[sh].num_samplers);
   }
}

static int
gen6_size_samplers(struct i965_3d *hw3d,
                   const struct i965_context *i965)
{
   int size = 0, num_samplers;

   num_samplers = i965->samplers[PIPE_SHADER_VERTEX].num_samplers;
   size += hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_SAMPLER_BORDER_COLOR_STATE, num_samplers) +
           hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_SAMPLER_STATE, num_samplers);

   num_samplers = i965->samplers[PIPE_SHADER_FRAGMENT].num_samplers;
   size += hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_SAMPLER_BORDER_COLOR_STATE, num_samplers) +
           hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_SAMPLER_STATE, num_samplers);

   return size;
}

/**
 * \see brw_samplers
 */
const struct i965_3d_atom gen6_atom_samplers = {
   .name = "samplers",
   .pipe_dirty = I965_DIRTY_VERTEX_SAMPLERS |
                 I965_DIRTY_FRAGMENT_SAMPLERS,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = I965_3D_DIRTY_GEN6_SAMPLER_STATE,
   .upload = gen6_upload_samplers,
   .size = gen6_size_samplers,
};

static void
gen6_upload_3DSTATE_SAMPLER_STATE_POINTERS(struct i965_3d *hw3d,
                                           const struct i965_context *i965)
{
   hw3d->gpe->emit_3DSTATE_SAMPLER_STATE_POINTERS(hw3d->gpe, hw3d->cp,
         hw3d->gen6.vs.SAMPLER_STATE,
         0,
         hw3d->gen6.wm.SAMPLER_STATE);
}

static int
gen6_size_3DSTATE_SAMPLER_STATE_POINTERS(struct i965_3d *hw3d,
                                         const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_SAMPLER_STATE_POINTERS, 1);
}

/**
 * \see gen6_sampler_state
 */
const struct i965_3d_atom gen6_atom_3DSTATE_SAMPLER_STATE_POINTERS = {
   .name = "3DSTATE_SAMPLER_STATE_POINTERS",
   .pipe_dirty = 0,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER |
                 I965_3D_DIRTY_GEN6_STATE_BASE_ADDRESS |
                 I965_3D_DIRTY_GEN6_SAMPLER_STATE,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_SAMPLER_STATE_POINTERS,
   .size = gen6_size_3DSTATE_SAMPLER_STATE_POINTERS,
};

static void
gen6_upload_multisample_states(struct i965_3d *hw3d,
                               const struct i965_context *i965)
{
   int num_samples = 1;

   if (hw3d->gen == 6)
      wa_post_sync_nonzero_flush(hw3d);

   if (i965->framebuffer.nr_cbufs)
      num_samples = i965->framebuffer.cbufs[0]->texture->nr_samples;

   hw3d->gpe->emit_3DSTATE_MULTISAMPLE(hw3d->gpe, hw3d->cp, num_samples);
   hw3d->gpe->emit_3DSTATE_SAMPLE_MASK(hw3d->gpe, hw3d->cp,
         (num_samples > 1) ? i965->sample_mask : 0x1);
}

static int
gen6_size_multisample_states(struct i965_3d *hw3d,
                             const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_MULTISAMPLE, 1) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_SAMPLE_MASK, 1);
}

/**
 * \see gen6_multisample_state
 */
const struct i965_3d_atom gen6_atom_multisample_states = {
   .name = "multisample states",
   .pipe_dirty = I965_DIRTY_FRAMEBUFFER |
                 I965_DIRTY_SAMPLE_MASK,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_multisample_states,
   .size = gen6_size_multisample_states,
};

static void
gen6_upload_vs(struct i965_3d *hw3d,
               const struct i965_context *i965)
{
   const struct i965_shader *vs = (i965->vs)? i965->vs->shader : NULL;
   const int num_samplers = i965->samplers[PIPE_SHADER_VERTEX].num_samplers;

   if (hw3d->gen == 6)
      wa_post_sync_nonzero_flush(hw3d);

   hw3d->gpe->emit_3DSTATE_CONSTANT_VS(hw3d->gpe, hw3d->cp);

   hw3d->gpe->emit_3DSTATE_VS(hw3d->gpe, hw3d->cp,
         vs, i965->max_vs_threads, num_samplers);

   hw3d->gpe->emit_PIPE_CONTROL(hw3d->gpe, hw3d->cp,
         PIPE_CONTROL_DEPTH_STALL |
         PIPE_CONTROL_INSTRUCTION_FLUSH |
         PIPE_CONTROL_STATE_CACHE_INVALIDATE,
         NULL, 0, FALSE);
}

static int
gen6_size_vs(struct i965_3d *hw3d,
             const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_CONSTANT_VS, 1) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_VS, 1) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_PIPE_CONTROL, 1);
}

/**
 * \see gen6_vs_state
 */
const struct i965_3d_atom gen6_atom_vs = {
   .name = "vertex shader",
   .pipe_dirty = I965_DIRTY_VS |
                 I965_DIRTY_VERTEX_SAMPLERS,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER |
                 I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_vs,
   .size = gen6_size_vs,
};

static void
gen6_upload_gs(struct i965_3d *hw3d,
               const struct i965_context *i965)
{
   const struct i965_shader *gs = (i965->gs)? i965->gs->shader : NULL;
   const struct i965_shader *vs = (i965->vs)? i965->vs->shader : NULL;

   hw3d->gpe->emit_3DSTATE_CONSTANT_GS(hw3d->gpe, hw3d->cp);
   hw3d->gpe->emit_3DSTATE_GS(hw3d->gpe, hw3d->cp,
         gs, i965->max_gs_threads, vs);
}

static int
gen6_size_gs(struct i965_3d *hw3d,
             const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_CONSTANT_GS, 1) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_GS, 1);
}

/**
 * \see gen6_gs_state
 */
const struct i965_3d_atom gen6_atom_gs = {
   .name = "geometry shader",
   .pipe_dirty = I965_DIRTY_GS,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_gs,
   .size = gen6_size_gs,
};

static void
gen6_upload_3DSTATE_CLIP(struct i965_3d *hw3d,
                         const struct i965_context *i965)
{
   const int vp_width = (int) (fabs(i965->viewport.scale[0]) * 2.0f);
   const int vp_height = (int) (fabs(i965->viewport.scale[1]) * 2.0f);

   hw3d->gpe->emit_3DSTATE_CLIP(hw3d->gpe, hw3d->cp,
         i965->rasterizer,
         (i965->fs && i965->fs->shader->in.has_linear_interp),
         (i965->framebuffer.width <= vp_width &&
          i965->framebuffer.height <= vp_height));
}

static int
gen6_size_3DSTATE_CLIP(struct i965_3d *hw3d,
                       const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_CLIP, 1);
}

/**
 * \see gen6_clip_state
 */
const struct i965_3d_atom gen6_atom_3DSTATE_CLIP = {
   .name = "3DSTATE_CLIP",
   .pipe_dirty = I965_DIRTY_RASTERIZER |
                 I965_DIRTY_FS,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_CLIP,
   .size = gen6_size_3DSTATE_CLIP,
};

static void
gen6_upload_3DSTATE_SF(struct i965_3d *hw3d,
                       const struct i965_context *i965)
{
   const struct i965_shader *vs = (i965->vs)? i965->vs->shader : NULL;
   const struct i965_shader *fs = (i965->fs)? i965->fs->shader : NULL;

   hw3d->gpe->emit_3DSTATE_SF(hw3d->gpe, hw3d->cp, i965->rasterizer, vs, fs);
}

static int
gen6_size_3DSTATE_SF(struct i965_3d *hw3d,
                     const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_SF, 1);
}

/**
 * \see gen6_sf_state
 */
const struct i965_3d_atom gen6_atom_3DSTATE_SF = {
   .name = "3DSTATE_SF",
   .pipe_dirty = I965_DIRTY_RASTERIZER |
                 I965_DIRTY_VS |
                 I965_DIRTY_FS,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_SF,
   .size = gen6_size_3DSTATE_SF,
};

static void
gen6_upload_wm(struct i965_3d *hw3d,
               const struct i965_context *i965)
{
   const struct i965_shader *fs = (i965->fs)? i965->fs->shader : NULL;
   const int num_samplers = i965->samplers[PIPE_SHADER_FRAGMENT].num_samplers;
   const boolean dual_blend = (i965->blend->rt[0].blend_enable &&
                               util_blend_state_is_dual(i965->blend, 0));

   hw3d->gpe->emit_3DSTATE_CONSTANT_PS(hw3d->gpe, hw3d->cp);
   hw3d->gpe->emit_3DSTATE_WM(hw3d->gpe, hw3d->cp,
         fs, i965->max_wm_threads, num_samplers,
         i965->rasterizer, dual_blend);
}

static int
gen6_size_wm(struct i965_3d *hw3d,
             const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_CONSTANT_PS, 1) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_WM, 1);
}

/**
 * \see gen6_wm_state
 */
const struct i965_3d_atom gen6_atom_wm = {
   .name = "WM",
   .pipe_dirty = I965_DIRTY_FS |
                 I965_DIRTY_FRAGMENT_SAMPLERS |
                 I965_DIRTY_BLEND |
                 I965_DIRTY_RASTERIZER,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER |
                 I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_wm,
   .size = gen6_size_wm,
};

static void
gen6_upload_scissor_states(struct i965_3d *hw3d,
                           const struct i965_context *i965)
{
   hw3d->gen6.SCISSOR_RECT =
      hw3d->gpe->emit_SCISSOR_RECT(hw3d->gpe, hw3d->cp, &i965->scissor, 1);

   hw3d->gpe->emit_3DSTATE_SCISSOR_STATE_POINTERS(hw3d->gpe, hw3d->cp,
         hw3d->gen6.SCISSOR_RECT);
}

static int
gen6_size_scissor_states(struct i965_3d *hw3d,
                         const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_SCISSOR_RECT, 1) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_SCISSOR_STATE_POINTERS, 1);
}

/**
 * \see gen6_scissor_state
 */
const struct i965_3d_atom gen6_atom_scissor_states = {
   .name = "scissor states",
   .pipe_dirty = I965_DIRTY_SCISSOR,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_scissor_states,
   .size = gen6_size_scissor_states,
};

static void
gen6_upload_3DSTATE_BINDING_TABLE_POINTERS(struct i965_3d *hw3d,
                                           const struct i965_context *i965)
{
   hw3d->gpe->emit_3DSTATE_BINDING_TABLE_POINTERS(hw3d->gpe, hw3d->cp,
         hw3d->gen6.vs.BINDING_TABLE_STATE,
         hw3d->gen6.gs.BINDING_TABLE_STATE,
         hw3d->gen6.wm.BINDING_TABLE_STATE);
}

static int
gen6_size_3DSTATE_BINDING_TABLE_POINTERS(struct i965_3d *hw3d,
                                         const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_BINDING_TABLE_POINTERS, 1);
}

/**
 * \see gen6_binding_table_pointers
 */
const struct i965_3d_atom gen6_atom_3DSTATE_BINDING_TABLE_POINTERS = {
   .name = "3DSTATE_BINDING_TABLE_POINTERS",
   .pipe_dirty = 0,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER |
                 I965_3D_DIRTY_GEN6_BINDING_TABLE_STATE,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_BINDING_TABLE_POINTERS,
   .size = gen6_size_3DSTATE_BINDING_TABLE_POINTERS,
};

static void
gen6_upload_depth_buffer(struct i965_3d *hw3d,
                         const struct i965_context *i965)
{
   if (hw3d->gen == 6)
      wa_post_sync_nonzero_flush(hw3d);

   hw3d->gpe->emit_3DSTATE_DEPTH_BUFFER(hw3d->gpe, hw3d->cp,
         i965->framebuffer.zsbuf);

   /* TODO */
   hw3d->gpe->emit_3DSTATE_CLEAR_PARAMS(hw3d->gpe, hw3d->cp, 0.0f);
}

static int
gen6_size_depth_buffer(struct i965_3d *hw3d,
                       const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_DEPTH_BUFFER, 1) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_CLEAR_PARAMS, 1);
}

/**
 * \see brw_depthbuffer
 */
const struct i965_3d_atom gen6_atom_depth_buffer = {
   .name = "depth buffer",
   .pipe_dirty = I965_DIRTY_FRAMEBUFFER,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_depth_buffer,
   .size = gen6_size_depth_buffer,
};

static void
gen6_upload_poly_stipple(struct i965_3d *hw3d,
                         const struct i965_context *i965)
{
   if (!i965->rasterizer->poly_stipple_enable)
      return;

   if (hw3d->gen == 6)
      wa_post_sync_nonzero_flush(hw3d);

   hw3d->gpe->emit_3DSTATE_POLY_STIPPLE_PATTERN(hw3d->gpe, hw3d->cp,
         &i965->poly_stipple);

   hw3d->gpe->emit_3DSTATE_POLY_STIPPLE_OFFSET(hw3d->gpe, hw3d->cp, 0, 0);
}

static int
gen6_size_poly_stipple(struct i965_3d *hw3d,
                       const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_POLY_STIPPLE_PATTERN, 1) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_POLY_STIPPLE_OFFSET, 1);
}

/**
 * \see brw_polygon_stipple
 * \see brw_polygon_stipple_offset
 */
const struct i965_3d_atom gen6_atom_poly_stipple = {
   .name = "3DSTATE_POLY_STIPPLE_PATTERN",
   .pipe_dirty = I965_DIRTY_RASTERIZER |
                 I965_DIRTY_POLY_STIPPLE,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_poly_stipple,
   .size = gen6_size_poly_stipple,
};

static void
gen6_upload_3DSTATE_LINE_STIPPLE_PATTERN(struct i965_3d *hw3d,
                                         const struct i965_context *i965)
{
   if (!i965->rasterizer->line_stipple_enable)
      return;

   if (hw3d->gen == 6)
      wa_post_sync_nonzero_flush(hw3d);

   hw3d->gpe->emit_3DSTATE_LINE_STIPPLE(hw3d->gpe, hw3d->cp,
         i965->rasterizer->line_stipple_pattern,
         i965->rasterizer->line_stipple_factor + 1);
}

static int
gen6_size_3DSTATE_LINE_STIPPLE_PATTERN(struct i965_3d *hw3d,
                                       const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_LINE_STIPPLE, 1);
}

/**
 * \see brw_line_stipple
 */
const struct i965_3d_atom gen6_atom_3DSTATE_LINE_STIPPLE_PATTERN = {
   .name = "3DSTATE_LINE_STIPPLE_PATTERN",
   .pipe_dirty = I965_DIRTY_RASTERIZER,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_LINE_STIPPLE_PATTERN,
   .size = gen6_size_3DSTATE_LINE_STIPPLE_PATTERN,
};

static void
gen6_upload_3DSTATE_AA_LINE_PARAMETERS(struct i965_3d *hw3d,
                                       const struct i965_context *i965)
{
   if (!i965->rasterizer->line_smooth)
      return;

   if (hw3d->gen == 6)
      wa_post_sync_nonzero_flush(hw3d);

   hw3d->gpe->emit_3DSTATE_AA_LINE_PARAMETERS(hw3d->gpe, hw3d->cp);
}

static int
gen6_size_3DSTATE_AA_LINE_PARAMETERS(struct i965_3d *hw3d,
                                     const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_AA_LINE_PARAMETERS, 1);
}

/**
 * \see brw_aa_line_parameters
 */
const struct i965_3d_atom gen6_atom_3DSTATE_AA_LINE_PARAMETERS = {
   .name = "3DSTATE_AA_LINE_PARAMETERS",
   .pipe_dirty = I965_DIRTY_RASTERIZER,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_AA_LINE_PARAMETERS,
   .size = gen6_size_3DSTATE_AA_LINE_PARAMETERS,
};

static void
gen6_upload_3DSTATE_DRAWING_RECTANGLE(struct i965_3d *hw3d,
                                      const struct i965_context *i965)
{
   if (hw3d->gen == 6)
      wa_post_sync_nonzero_flush(hw3d);

   hw3d->gpe->emit_3DSTATE_DRAWING_RECTANGLE(hw3d->gpe, hw3d->cp,
         i965->framebuffer.width, i965->framebuffer.height);
}

static int
gen6_size_3DSTATE_DRAWING_RECTANGLE(struct i965_3d *hw3d,
                                    const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_DRAWING_RECTANGLE, 1);
}

/**
 * \see brw_drawing_rect
 */
const struct i965_3d_atom gen6_atom_3DSTATE_DRAWING_RECTANGLE = {
   .name = "3DSTATE_DRAWING_RECTANGLE",
   .pipe_dirty = I965_DIRTY_FRAMEBUFFER,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_DRAWING_RECTANGLE,
   .size = gen6_size_3DSTATE_DRAWING_RECTANGLE,
};

static void
gen6_upload_3DSTATE_GS_SVB_INDEX(struct i965_3d *hw3d,
                                 const struct i965_context *i965)
{
   hw3d->gpe->emit_3DSTATE_GS_SVB_INDEX(hw3d->gpe, hw3d->cp, 0, 0, 0);
}

static int
gen6_size_3DSTATE_GS_SVB_INDEX(struct i965_3d *hw3d,
                                 const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_GS_SVB_INDEX, 1);
}

/**
 * \see gen6_sol_indices
 */
const struct i965_3d_atom gen6_atom_3DSTATE_GS_SVB_INDEX = {
   .name = "3DSTATE_GS_SVB_INDEX",
   .pipe_dirty = 0,
   .hw3d_dirty = I965_3D_DIRTY_DRV_HW_CONTEXT,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_GS_SVB_INDEX,
   .size = gen6_size_3DSTATE_GS_SVB_INDEX,
};

static void
gen6_upload_3DSTATE_INDEX_BUFFER(struct i965_3d *hw3d,
                                 const struct i965_context *i965)
{
   hw3d->gpe->emit_3DSTATE_INDEX_BUFFER(hw3d->gpe, hw3d->cp, &i965->index_buffer);
}

static int
gen6_size_3DSTATE_INDEX_BUFFER(struct i965_3d *hw3d,
                               const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_INDEX_BUFFER, 1);
}

/**
 * \see brw_index_buffer
 */
const struct i965_3d_atom gen6_atom_3DSTATE_INDEX_BUFFER = {
   .name = "3DSTATE_INDEX_BUFFER",
   .pipe_dirty = I965_DIRTY_INDEX_BUFFER,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_3DSTATE_INDEX_BUFFER,
   .size = gen6_size_3DSTATE_INDEX_BUFFER,
};

static void
gen6_upload_vertices(struct i965_3d *hw3d,
                     const struct i965_context *i965)
{
   const struct i965_vertex_element *ive = i965->vertex_elements;

   hw3d->gpe->emit_3DSTATE_VERTEX_BUFFERS(hw3d->gpe, hw3d->cp,
         i965->vertex_buffers.buffers, i965->vertex_buffers.num_buffers);

   hw3d->gpe->emit_3DSTATE_VERTEX_ELEMENTS(hw3d->gpe, hw3d->cp,
         ive->elements, ive->num_elements);
}

static int
gen6_size_vertices(struct i965_3d *hw3d,
                                 const struct i965_context *i965)
{
   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_VERTEX_BUFFERS, i965->vertex_buffers.num_buffers) +
          hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DSTATE_VERTEX_ELEMENTS, i965->vertex_elements->num_elements);
}

/**
 * \see brw_vertices
 */
const struct i965_3d_atom gen6_atom_vertices = {
   .name = "vertices",
   .pipe_dirty = I965_DIRTY_VERTEX_BUFFERS |
                 I965_DIRTY_VERTEX_ELEMENTS,
   .hw3d_dirty = I965_3D_DIRTY_DRV_BATCH_BUFFER,
   .hw3d_dirty_set = 0,
   .upload = gen6_upload_vertices,
   .size = gen6_size_vertices,
};

static uint32_t
get_hw3d_dirty(struct i965_3d *hw3d, const struct i965_context *i965)
{
   uint32_t hw3d_dirty = 0;

   /* new batch */
   if (hw3d->new_batch) {
      hw3d_dirty |= I965_3D_DIRTY_DRV_BATCH_BUFFER;
      /* every new batch should be treated as a new HW context */
      if (!hw3d->cp->hw_ctx)
         hw3d_dirty |= I965_3D_DIRTY_DRV_HW_CONTEXT;
   }

   /* new shader cache */
   if (hw3d->shader_cache_seqno != i965->shader_cache->seqno)
      hw3d_dirty |= I965_3D_DIRTY_DRV_SHADER_CACHE;

   return hw3d_dirty;
}

/**
 * Upload an array of atoms.
 */
int
i965_3d_upload_atoms(struct i965_3d *hw3d, const struct i965_context *i965,
                     const struct i965_3d_atom **atoms, int num_atoms,
                     boolean dry_run)
{
   uint32_t hw3d_dirty = get_hw3d_dirty(hw3d, i965);
   int emit_size = 0, max_size = 0, i;

   /* no state change */
   if (!i965->dirty && !hw3d_dirty)
      return 0;

   if (dry_run) {
      /* get the max size */
      for (i = 0; i < num_atoms; i++) {
         const struct i965_3d_atom *atom = atoms[i];

         if ((i965->dirty & atom->pipe_dirty) ||
             (hw3d_dirty & atom->hw3d_dirty)) {
            max_size += atom->size(hw3d, i965);
            hw3d_dirty |= atom->hw3d_dirty_set;
         }
      }

      return max_size;
   }
   else {
      /* available space before emitting */
      emit_size = i965_cp_space(hw3d->cp);

      for (i = 0; i < num_atoms; i++) {
         const struct i965_3d_atom *atom = atoms[i];

         if ((i965->dirty & atom->pipe_dirty) ||
             (hw3d_dirty & atom->hw3d_dirty)) {
            atom->upload(hw3d, i965);
            max_size += atom->size(hw3d, i965);
            hw3d_dirty |= atom->hw3d_dirty_set;
         }
      }

      /* get space used */
      emit_size -= i965_cp_space(hw3d->cp);
      assert(emit_size <= max_size);

      return emit_size;
   }
}

static int
i965_3d_upload_context_gen6(struct i965_3d *hw3d,
                            const struct i965_context *i965,
                            boolean dry_run)
{
   /**
    * \see gen6_atoms
    */
   static const struct i965_3d_atom *atoms[] = {
      &gen6_atom_extra_size,
      &gen6_atom_CLIP_VIEWPORT,
      &gen6_atom_SF_VIEWPORT,
      &gen6_atom_invariant_states,
      &gen6_atom_STATE_BASE_ADDRESS,
      &gen6_atom_CC_VIEWPORT,
      &gen6_atom_3DSTATE_VIEWPORT_STATE_POINTERS,
      &gen6_atom_3DSTATE_URB,
      &gen6_atom_BLEND_STATE,
      &gen6_atom_COLOR_CALC_STATE,
      &gen6_atom_DEPTH_STENCIL_STATE,
      &gen6_atom_3DSTATE_CC_STATE_POINTERS,
      &gen6_atom_const_buffers,
      &gen6_atom_color_buffers,
      &gen6_atom_textures,
      &gen6_atom_sol_surfaces,
      &gen6_atom_BINDING_TABLE_STATE,
      &gen6_atom_samplers,
      &gen6_atom_3DSTATE_SAMPLER_STATE_POINTERS,
      &gen6_atom_multisample_states,
      &gen6_atom_vs,
      &gen6_atom_gs,
      &gen6_atom_3DSTATE_CLIP,
      &gen6_atom_3DSTATE_SF,
      &gen6_atom_wm,
      &gen6_atom_scissor_states,
      &gen6_atom_3DSTATE_BINDING_TABLE_POINTERS,
      &gen6_atom_depth_buffer,
      &gen6_atom_poly_stipple,
      &gen6_atom_3DSTATE_LINE_STIPPLE_PATTERN,
      &gen6_atom_3DSTATE_AA_LINE_PARAMETERS,
      &gen6_atom_3DSTATE_DRAWING_RECTANGLE,
      &gen6_atom_3DSTATE_GS_SVB_INDEX,
      &gen6_atom_3DSTATE_INDEX_BUFFER,
      &gen6_atom_vertices,
   };

   return i965_3d_upload_atoms(hw3d, i965, atoms, Elements(atoms), dry_run);
}

static int
i965_3d_draw_gen6(struct i965_3d *hw3d, const struct pipe_draw_info *info,
                  boolean dry_run)
{
   const int size = hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_3DPRIMITIVE, 1);

   if (!dry_run) {
      hw3d->gpe->emit_3DPRIMITIVE(hw3d->gpe, hw3d->cp, info);
      hw3d->gen6.need_wa_flush = TRUE;
   }

   return size;
}

/**
 * Emit PIPE_CONTROL to flush all caches.
 */
int
i965_3d_flush_gen6(struct i965_3d *hw3d, boolean dry_run)
{
   if (!dry_run) {
      if (hw3d->gen == 6)
         wa_post_sync_nonzero_flush(hw3d);

      hw3d->gpe->emit_PIPE_CONTROL(hw3d->gpe, hw3d->cp,
            PIPE_CONTROL_INSTRUCTION_FLUSH |
            PIPE_CONTROL_WRITE_FLUSH |
            PIPE_CONTROL_DEPTH_CACHE_FLUSH |
            PIPE_CONTROL_VF_CACHE_INVALIDATE |
            PIPE_CONTROL_TC_FLUSH |
            PIPE_CONTROL_NO_WRITE |
            PIPE_CONTROL_CS_STALL,
            0, 0, FALSE);
   }

   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_PIPE_CONTROL, 1) +
      ((hw3d->gen == 6) ? gen6_atom_extra_size.size(hw3d, NULL) : 0);
}

/**
 * Emit PIPE_CONTROL with PIPE_CONTROL_WRITE_TIMESTAMP post-sync op.
 */
int
i965_3d_write_timestamp_gen6(struct i965_3d *hw3d, struct intel_bo *bo,
                             int index, boolean dry_run)
{
   if (!dry_run) {
      /* a variant of wa_post_sync_nonzero_flush() */
      hw3d->gpe->emit_PIPE_CONTROL(hw3d->gpe, hw3d->cp,
            PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STALL_AT_SCOREBOARD,
            NULL, 0,
            FALSE);

      hw3d->gpe->emit_PIPE_CONTROL(hw3d->gpe, hw3d->cp,
            PIPE_CONTROL_WRITE_TIMESTAMP,
            bo, index * sizeof(uint64_t) | PIPE_CONTROL_GLOBAL_GTT_WRITE,
            TRUE);
   }

   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_PIPE_CONTROL, 1) * 2;
}

/**
 * Emit PIPE_CONTROL with PIPE_CONTROL_WRITE_DEPTH_COUNT post-sync op.
 */
int
i965_3d_write_depth_count_gen6(struct i965_3d *hw3d, struct intel_bo *bo,
                               int index, boolean dry_run)
{
   if (!dry_run) {
      if (hw3d->gen == 6)
         wa_post_sync_nonzero_flush(hw3d);

      hw3d->gpe->emit_PIPE_CONTROL(hw3d->gpe, hw3d->cp,
            PIPE_CONTROL_DEPTH_STALL | PIPE_CONTROL_WRITE_DEPTH_COUNT,
            bo, index * sizeof(uint64_t) | PIPE_CONTROL_GLOBAL_GTT_WRITE,
            TRUE);
   }

   return hw3d->gpe->emit_max(hw3d->gpe, I965_GPE_GEN6_PIPE_CONTROL, 1) +
      ((hw3d->gen == 6) ? gen6_atom_extra_size.size(hw3d, NULL) : 0);
}

/**
 * Initialize the 3D context for GEN6.
 */
void
i965_3d_init_gen6(struct i965_3d *hw3d)
{
   STATIC_ASSERT(I965_3D_STATE_COUNT <= 32);

   hw3d->gpe = i965_gpe_gen6_get();

   hw3d->upload_context = i965_3d_upload_context_gen6;
   hw3d->draw = i965_3d_draw_gen6;
   hw3d->flush = i965_3d_flush_gen6;
   hw3d->write_timestamp = i965_3d_write_timestamp_gen6;
   hw3d->write_depth_count = i965_3d_write_depth_count_gen6;
   hw3d->dump = i965_3d_dump_gen6;

   hw3d->gen6.need_wa_flush = TRUE;
}
