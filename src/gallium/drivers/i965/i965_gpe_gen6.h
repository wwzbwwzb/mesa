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

#ifndef I965_GPE_GEN6_H
#define I965_GPE_GEN6_H

#include "i965_common.h"

struct intel_bo;
struct i965_cp;
struct i965_resource;
struct i965_shader;

/**
 * States that GEN6 GPE could emit.
 */
enum i965_gpe_gen6_emit {
   /* GPE */
   I965_GPE_GEN6_PIPELINE_SELECT,
   I965_GPE_GEN6_STATE_BASE_ADDRESS,
   I965_GPE_GEN6_STATE_SIP,

   /* 3D */
   I965_GPE_GEN6_3DSTATE_CC_STATE_POINTERS,
   I965_GPE_GEN6_3DSTATE_BINDING_TABLE_POINTERS,
   I965_GPE_GEN6_3DSTATE_SAMPLER_STATE_POINTERS,
   I965_GPE_GEN6_3DSTATE_VIEWPORT_STATE_POINTERS,
   I965_GPE_GEN6_3DSTATE_SCISSOR_STATE_POINTERS,
   I965_GPE_GEN6_3DSTATE_URB,
   I965_GPE_GEN6_PIPE_CONTROL,

   /* VF */
   I965_GPE_GEN6_3DSTATE_INDEX_BUFFER,
   I965_GPE_GEN6_3DSTATE_VERTEX_BUFFERS,
   I965_GPE_GEN6_3DSTATE_VERTEX_ELEMENTS,
   I965_GPE_GEN6_3DPRIMITIVE,
   I965_GPE_GEN6_3DSTATE_VF_STATISTICS,

   /* VS */
   I965_GPE_GEN6_3DSTATE_VS,
   I965_GPE_GEN6_3DSTATE_CONSTANT_VS,

   /* GS */
   I965_GPE_GEN6_3DSTATE_GS_SVB_INDEX,
   I965_GPE_GEN6_3DSTATE_GS,
   I965_GPE_GEN6_3DSTATE_CONSTANT_GS,

   /* CLIP */
   I965_GPE_GEN6_3DSTATE_CLIP,
   I965_GPE_GEN6_CLIP_VIEWPORT,

   /* SF */
   I965_GPE_GEN6_3DSTATE_DRAWING_RECTANGLE,
   I965_GPE_GEN6_3DSTATE_SF,
   I965_GPE_GEN6_SF_VIEWPORT,
   I965_GPE_GEN6_SCISSOR_RECT,

   /* WM */
   I965_GPE_GEN6_3DSTATE_WM,
   I965_GPE_GEN6_3DSTATE_CONSTANT_PS,
   I965_GPE_GEN6_3DSTATE_SAMPLE_MASK,
   I965_GPE_GEN6_3DSTATE_AA_LINE_PARAMETERS,
   I965_GPE_GEN6_3DSTATE_LINE_STIPPLE,
   I965_GPE_GEN6_3DSTATE_POLY_STIPPLE_OFFSET,
   I965_GPE_GEN6_3DSTATE_POLY_STIPPLE_PATTERN,
   I965_GPE_GEN6_3DSTATE_MULTISAMPLE,
   I965_GPE_GEN6_3DSTATE_DEPTH_BUFFER,
   I965_GPE_GEN6_3DSTATE_STENCIL_BUFFER,
   I965_GPE_GEN6_3DSTATE_HIER_DEPTH_BUFFER,
   I965_GPE_GEN6_3DSTATE_CLEAR_PARAMS,

   /* CC */
   I965_GPE_GEN6_COLOR_CALC_STATE,
   I965_GPE_GEN6_DEPTH_STENCIL_STATE,
   I965_GPE_GEN6_BLEND_STATE,
   I965_GPE_GEN6_CC_VIEWPORT,

   /* subsystem */
   I965_GPE_GEN6_BINDING_TABLE_STATE,
   I965_GPE_GEN6_SURFACE_STATE,
   I965_GPE_GEN6_SAMPLER_STATE,
   I965_GPE_GEN6_SAMPLER_BORDER_COLOR_STATE,

   I965_GPE_GEN6_COUNT,
};

/**
 * GEN6 graphics processing engine
 *
 * This is a low-level interface that usually should not be used directly.
 */
struct i965_gpe_gen6 {
   int gen;

   /* get the maximum emit size of a state */
   int (*emit_max)(const struct i965_gpe_gen6 *gpe, int state, int array_size);

   /* GPE */
   void (*emit_PIPELINE_SELECT)(const struct i965_gpe_gen6 *gpe,
                                struct i965_cp *cp, boolean media);

   void (*emit_STATE_BASE_ADDRESS)(const struct i965_gpe_gen6 *gpe,
                                   struct i965_cp *cp,
                                   struct intel_bo *general_state_bo,
                                   struct intel_bo *surface_state_bo,
                                   struct intel_bo *dynamic_state_bo,
                                   struct intel_bo *indirect_object_bo,
                                   struct intel_bo *instruction_bo,
                                   unsigned general_state_ub,
                                   unsigned dynamic_state_ub,
                                   unsigned indirect_object_ub,
                                   unsigned instruction_ub);

   void (*emit_STATE_SIP)(const struct i965_gpe_gen6 *gpe,
                          struct i965_cp *cp, uint32_t sip);

   /* 3D */
   void (*emit_3DSTATE_CC_STATE_POINTERS)(const struct i965_gpe_gen6 *gpe,
                                          struct i965_cp *cp,
                                          uint32_t blend_state,
                                          uint32_t depth_stencil_state,
                                          uint32_t color_calc_state);

   void (*emit_3DSTATE_BINDING_TABLE_POINTERS)(const struct i965_gpe_gen6 *gpe,
                                               struct i965_cp *cp,
                                               uint32_t vs_binding_table,
                                               uint32_t gs_binding_table,
                                               uint32_t ps_binding_table);

   void (*emit_3DSTATE_SAMPLER_STATE_POINTERS)(const struct i965_gpe_gen6 *gpe,
                                               struct i965_cp *cp,
                                               uint32_t vs_sampler_state,
                                               uint32_t gs_sampler_state,
                                               uint32_t ps_sampler_state);

   void (*emit_3DSTATE_VIEWPORT_STATE_POINTERS)(const struct i965_gpe_gen6 *gpe,
                                                struct i965_cp *cp,
                                                uint32_t clip_viewport,
                                                uint32_t sf_viewport,
                                                uint32_t cc_viewport);

   void (*emit_3DSTATE_SCISSOR_STATE_POINTERS)(const struct i965_gpe_gen6 *gpe,
                                               struct i965_cp *cp,
                                               uint32_t scissor_rect);

   void (*emit_3DSTATE_URB)(const struct i965_gpe_gen6 *gpe,
                            struct i965_cp *cp,
                            int vs_entry_size, int num_vs_entries,
                            int gs_entry_size, int num_gs_entries);

   void (*emit_PIPE_CONTROL)(const struct i965_gpe_gen6 *gpe,
                             struct i965_cp *cp,
                             uint32_t dw1,
                             struct intel_bo *bo, uint32_t bo_offset,
                             boolean is_64);

   /* VF */
   void (*emit_3DSTATE_INDEX_BUFFER)(const struct i965_gpe_gen6 *gpe,
                                     struct i965_cp *cp,
                                     const struct pipe_index_buffer *ib);

   void (*emit_3DSTATE_VERTEX_BUFFERS)(const struct i965_gpe_gen6 *gpe,
                                       struct i965_cp *cp,
                                       const struct pipe_vertex_buffer *vbuffers,
                                       int num_buffers);

   void (*emit_3DSTATE_VERTEX_ELEMENTS)(const struct i965_gpe_gen6 *gpe,
                                        struct i965_cp *cp,
                                        const struct pipe_vertex_element *velements,
                                        int num_elements);

   void (*emit_3DPRIMITIVE)(const struct i965_gpe_gen6 *gpe,
                            struct i965_cp *cp,
                            const struct pipe_draw_info *info);

   void (*emit_3DSTATE_VF_STATISTICS)(const struct i965_gpe_gen6 *gpe,
                                      struct i965_cp *cp,
                                      boolean enable);

   /* VS */
   void (*emit_3DSTATE_VS)(const struct i965_gpe_gen6 *gpe,
                           struct i965_cp *cp,
                           const struct i965_shader *vs,
                           int max_threads, int num_samplers);

   void (*emit_3DSTATE_CONSTANT_VS)(const struct i965_gpe_gen6 *gpe,
                                    struct i965_cp *cp);

   /* GS */
   void (*emit_3DSTATE_GS_SVB_INDEX)(const struct i965_gpe_gen6 *gpe,
                                     struct i965_cp *cp,
                                     int index, unsigned svbi,
                                     unsigned max_svbi);

   void (*emit_3DSTATE_GS)(const struct i965_gpe_gen6 *gpe,
                           struct i965_cp *cp,
                           const struct i965_shader *gs,
                           int max_threads, const struct i965_shader *vs);

   void (*emit_3DSTATE_CONSTANT_GS)(const struct i965_gpe_gen6 *gpe,
                                    struct i965_cp *cp);

   /* CLIP */
   void (*emit_3DSTATE_CLIP)(const struct i965_gpe_gen6 *gpe,
                             struct i965_cp *cp,
                             const struct pipe_rasterizer_state *rasterizer,
                             boolean has_linear_interp,
                             boolean has_full_viewport);

   uint32_t (*emit_CLIP_VIEWPORT)(const struct i965_gpe_gen6 *gpe,
                                  struct i965_cp *cp,
                                  const struct pipe_viewport_state *viewports,
                                  int num_viewports);

   /* SF */
   void (*emit_3DSTATE_DRAWING_RECTANGLE)(const struct i965_gpe_gen6 *gpe,
                                          struct i965_cp *cp,
                                          int width, int height);

   void (*emit_3DSTATE_SF)(const struct i965_gpe_gen6 *gpe,
                           struct i965_cp *cp,
                           const struct pipe_rasterizer_state *rasterizer,
                           const struct i965_shader *vs,
                           const struct i965_shader *fs);

   uint32_t (*emit_SF_VIEWPORT)(const struct i965_gpe_gen6 *gpe,
                                struct i965_cp *cp,
                                const struct pipe_viewport_state *viewports,
                                int num_viewports);

   uint32_t (*emit_SCISSOR_RECT)(const struct i965_gpe_gen6 *gpe,
                                 struct i965_cp *cp,
                                 const struct pipe_scissor_state *scissors,
                                 int num_scissors);

   /* WM */
   void (*emit_3DSTATE_WM)(const struct i965_gpe_gen6 *gpe,
                           struct i965_cp *cp,
                           const struct i965_shader *fs,
                           int max_threads, int num_samplers,
                           const struct pipe_rasterizer_state *rasterizer,
                           boolean dual_blend);

   void (*emit_3DSTATE_CONSTANT_PS)(const struct i965_gpe_gen6 *gpe,
                                    struct i965_cp *cp);

   void (*emit_3DSTATE_SAMPLE_MASK)(const struct i965_gpe_gen6 *gpe,
                                    struct i965_cp *cp,
                                    unsigned sample_mask);

   void (*emit_3DSTATE_AA_LINE_PARAMETERS)(const struct i965_gpe_gen6 *gpe,
                                           struct i965_cp *cp);

   void (*emit_3DSTATE_LINE_STIPPLE)(const struct i965_gpe_gen6 *gpe,
                                     struct i965_cp *cp,
                                     unsigned pattern, unsigned factor);

   void (*emit_3DSTATE_POLY_STIPPLE_OFFSET)(const struct i965_gpe_gen6 *gpe,
                                            struct i965_cp *cp,
                                            int x_offset, int y_offset);

   void (*emit_3DSTATE_POLY_STIPPLE_PATTERN)(const struct i965_gpe_gen6 *gpe,
                                             struct i965_cp *cp,
                                             const struct pipe_poly_stipple *pattern);

   void (*emit_3DSTATE_MULTISAMPLE)(const struct i965_gpe_gen6 *gpe,
                                    struct i965_cp *cp,
                                    int num_samples);

   void (*emit_3DSTATE_DEPTH_BUFFER)(const struct i965_gpe_gen6 *gpe,
                                     struct i965_cp *cp,
                                     const struct pipe_surface *surface);

   void (*emit_3DSTATE_STENCIL_BUFFER)(const struct i965_gpe_gen6 *gpe,
                                       struct i965_cp *cp,
                                       const struct pipe_surface *surface);

   void (*emit_3DSTATE_HIER_DEPTH_BUFFER)(const struct i965_gpe_gen6 *gpe,
                                          struct i965_cp *cp,
                                          const struct pipe_surface *surface);

   void (*emit_3DSTATE_CLEAR_PARAMS)(const struct i965_gpe_gen6 *gpe,
                                     struct i965_cp *cp,
                                     float clear_val);

   /* CC */
   uint32_t (*emit_COLOR_CALC_STATE)(const struct i965_gpe_gen6 *gpe,
                                     struct i965_cp *cp,
                                     const struct pipe_stencil_ref *stencil_ref,
                                     float alpha_ref,
                                     const struct pipe_blend_color *blend_color);

   uint32_t (*emit_DEPTH_STENCIL_STATE)(const struct i965_gpe_gen6 *gpe,
                                        struct i965_cp *cp,
                                        const struct pipe_depth_stencil_alpha_state *dsa);

   uint32_t (*emit_BLEND_STATE)(const struct i965_gpe_gen6 *gpe,
                                struct i965_cp *cp,
                                const struct pipe_blend_state *blend,
                                const struct pipe_framebuffer_state *framebuffer,
                                const struct pipe_alpha_state *alpha);

   uint32_t (*emit_CC_VIEWPORT)(const struct i965_gpe_gen6 *gpe,
                                struct i965_cp *cp,
                                const struct pipe_viewport_state *viewports,
                                int num_viewports, boolean depth_clip);

   /* subsystem */
   uint32_t (*emit_BINDING_TABLE_STATE)(const struct i965_gpe_gen6 *gpe,
                                        struct i965_cp *cp,
                                        uint32_t *surface_states,
                                        int num_surface_states);

   uint32_t (*emit_SURFACE_STATE)(const struct i965_gpe_gen6 *gpe,
                                  struct i965_cp *cp,
                                  const struct pipe_surface *surface,
                                  const struct pipe_sampler_view *view,
                                  const struct pipe_constant_buffer *cbuf,
                                  const struct pipe_stream_output_target *so,
                                  unsigned so_num_components);

   uint32_t (*emit_SAMPLER_STATE)(const struct i965_gpe_gen6 *gpe,
                                  struct i965_cp *cp,
                                  const struct pipe_sampler_state **samplers,
                                  const struct pipe_sampler_view **sampler_views,
                                  const uint32_t *sampler_border_colors,
                                  int num_samplers);

   uint32_t (*emit_SAMPLER_BORDER_COLOR_STATE)(const struct i965_gpe_gen6 *gpe,
                                               struct i965_cp *cp,
                                               const union pipe_color_union *color);
};

const struct i965_gpe_gen6 *
i965_gpe_gen6_get(void);

#endif /* I965_GPE_GEN6_H */
