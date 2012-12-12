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

#include "util/u_half.h"
#include "i965_shader.h"
#include "i965_common.h"
#include "i965_context.h"
#include "i965_cp.h"
#include "i965_resource.h"
#include "i965_state.h"
#include "i965_translate.h"
#include "i965_gpe_gen6.h"

/* GPE */
#define GEN6_SIZE_PIPELINE_SELECT                  (1)
#define GEN6_SIZE_STATE_BASE_ADDRESS               (10)
#define GEN6_SIZE_STATE_SIP                        (2)

/* 3D */
#define GEN6_SIZE_3DSTATE_CC_STATE_POINTERS        (4)
#define GEN6_SIZE_3DSTATE_BINDING_TABLE_POINTERS   (4)
#define GEN6_SIZE_3DSTATE_SAMPLER_STATE_POINTERS   (4)
#define GEN6_SIZE_3DSTATE_VIEWPORT_STATE_POINTERS  (4)
#define GEN6_SIZE_3DSTATE_SCISSOR_STATE_POINTERS   (2)
#define GEN6_SIZE_3DSTATE_URB                      (3)
#define GEN6_MAX_PIPE_CONTROL                      (5)

/* VF */
#define GEN6_SIZE_3DSTATE_INDEX_BUFFER             (3)
#define GEN6_MAX_3DSTATE_VERTEX_BUFFERS            (4 * 33 + 1)
#define GEN6_MAX_3DSTATE_VERTEX_ELEMENTS           (2 * 34 + 1)
#define GEN6_SIZE_3DPRIMITIVE                      (6)
#define GEN6_SIZE_3DSTATE_VF_STATISTICS            (1)

/* VS */
#define GEN6_SIZE_3DSTATE_VS                       (6)
#define GEN6_SIZE_3DSTATE_CONSTANT_VS              (5)

/* GS */
#define GEN6_SIZE_3DSTATE_GS_SVB_INDEX             (4)
#define GEN6_SIZE_3DSTATE_GS                       (7)
#define GEN6_SIZE_3DSTATE_CONSTANT_GS              (5)

/* CLIP */
#define GEN6_SIZE_3DSTATE_CLIP                     (4)
#define GEN6_MAX_CLIP_VIEWPORT                     (4 * 16 + 7)

/* SF */
#define GEN6_SIZE_3DSTATE_DRAWING_RECTANGLE        (4)
#define GEN6_SIZE_3DSTATE_SF                       (20)
#define GEN6_MAX_SF_VIEWPORT                       (8 * 16 + 7)
#define GEN6_MAX_SCISSOR_RECT                      (2 * 16 + 7)

/* WM */
#define GEN6_SIZE_3DSTATE_WM                       (9)
#define GEN6_SIZE_3DSTATE_CONSTANT_PS              (5)
#define GEN6_SIZE_3DSTATE_SAMPLE_MASK              (2)
#define GEN6_SIZE_3DSTATE_AA_LINE_PARAMETERS       (3)
#define GEN6_SIZE_3DSTATE_LINE_STIPPLE             (3)
#define GEN6_SIZE_3DSTATE_POLY_STIPPLE_OFFSET      (2)
#define GEN6_SIZE_3DSTATE_POLY_STIPPLE_PATTERN     (33)
#define GEN6_SIZE_3DSTATE_MULTISAMPLE              (3)
#define GEN6_SIZE_3DSTATE_DEPTH_BUFFER             (7)
#define GEN6_SIZE_3DSTATE_STENCIL_BUFFER           (3)
#define GEN6_SIZE_3DSTATE_HIER_DEPTH_BUFFER        (3)
#define GEN6_SIZE_3DSTATE_CLEAR_PARAMS             (2)

/* CC */
#define GEN6_MAX_COLOR_CALC_STATE                  (6 + 15)
#define GEN6_MAX_DEPTH_STENCIL_STATE               (3 + 15)
#define GEN6_MAX_BLEND_STATE                       (2 * 8 + 15)
#define GEN6_MAX_CC_VIEWPORT                       (2 * 16 + 7)

/* subsystem */
#define GEN6_MAX_BINDING_TABLE_STATE               (1 * 256 + 7)
#define GEN6_MAX_SURFACE_STATE                     (6 + 7)
#define GEN6_MAX_SAMPLER_STATE                     (4 * 16 + 7)
#define GEN6_MAX_SAMPLER_BORDER_COLOR_STATE        (12 + 7)

static void
gen6_emit_PIPELINE_SELECT(const struct i965_gpe_gen6 *gpe,
                          struct i965_cp *cp, boolean media)
{
   const int cmd_len = GEN6_SIZE_PIPELINE_SELECT;
   const int pipeline = (media) ? 0x1 : 0x0;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, CMD_PIPELINE_SELECT_GM45 << 16 | pipeline);
   i965_cp_end(cp);
}

static void
gen6_emit_STATE_BASE_ADDRESS(const struct i965_gpe_gen6 *gpe,
                             struct i965_cp *cp,
                             struct intel_bo *general_state_bo,
                             struct intel_bo *surface_state_bo,
                             struct intel_bo *dynamic_state_bo,
                             struct intel_bo *indirect_object_bo,
                             struct intel_bo *instruction_bo,
                             unsigned general_state_ub,
                             unsigned dynamic_state_ub,
                             unsigned indirect_object_ub,
                             unsigned instruction_ub)
{
   const int cmd_len = GEN6_SIZE_STATE_BASE_ADDRESS;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, CMD_STATE_BASE_ADDRESS << 16 | (cmd_len - 2));

   i965_cp_write_bo(cp, general_state_bo,
                        INTEL_DOMAIN_RENDER,
                        INTEL_DOMAIN_RENDER, 1);
   i965_cp_write_bo(cp, surface_state_bo,
                        INTEL_DOMAIN_SAMPLER,
                        0, 1);
   i965_cp_write_bo(cp, dynamic_state_bo,
                        INTEL_DOMAIN_RENDER | INTEL_DOMAIN_INSTRUCTION,
                        0, 1);
   i965_cp_write_bo(cp, indirect_object_bo,
                        0,
                        0, 1);
   i965_cp_write_bo(cp, instruction_bo,
                        INTEL_DOMAIN_INSTRUCTION,
                        0, 1);

   i965_cp_write(cp, general_state_ub | 1);
   i965_cp_write(cp, dynamic_state_ub | 1);
   i965_cp_write(cp, indirect_object_ub | 1);
   i965_cp_write(cp, instruction_ub | 1);
   i965_cp_end(cp);
}

static void
gen6_emit_STATE_SIP(const struct i965_gpe_gen6 *gpe,
                    struct i965_cp *cp, uint32_t sip)
{
   const int cmd_len = GEN6_SIZE_STATE_SIP;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, CMD_STATE_SIP << 16 | (cmd_len - 2));
   i965_cp_write(cp, sip);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_CC_STATE_POINTERS(const struct i965_gpe_gen6 *gpe,
                                    struct i965_cp *cp,
                                    uint32_t blend_state,
                                    uint32_t depth_stencil_state,
                                    uint32_t color_calc_state)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_CC_STATE_POINTERS;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_CC_STATE_POINTERS << 16 | (cmd_len - 2));
   i965_cp_write(cp, blend_state | 1);
   i965_cp_write(cp, depth_stencil_state | 1);
   i965_cp_write(cp, color_calc_state | 1);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_BINDING_TABLE_POINTERS(const struct i965_gpe_gen6 *gpe,
                                         struct i965_cp *cp,
                                         uint32_t vs_binding_table,
                                         uint32_t gs_binding_table,
                                         uint32_t ps_binding_table)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_BINDING_TABLE_POINTERS;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_BINDING_TABLE_POINTERS << 16 | (cmd_len - 2) |
                     GEN6_BINDING_TABLE_MODIFY_VS |
                     GEN6_BINDING_TABLE_MODIFY_GS |
                     GEN6_BINDING_TABLE_MODIFY_PS);
   i965_cp_write(cp, vs_binding_table);
   i965_cp_write(cp, gs_binding_table);
   i965_cp_write(cp, ps_binding_table);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_SAMPLER_STATE_POINTERS(const struct i965_gpe_gen6 *gpe,
                                         struct i965_cp *cp,
                                         uint32_t vs_sampler_state,
                                         uint32_t gs_sampler_state,
                                         uint32_t ps_sampler_state)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_SAMPLER_STATE_POINTERS;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_SAMPLER_STATE_POINTERS << 16 | (cmd_len - 2) |
                     VS_SAMPLER_STATE_CHANGE |
                     GS_SAMPLER_STATE_CHANGE |
                     PS_SAMPLER_STATE_CHANGE);
   i965_cp_write(cp, vs_sampler_state);
   i965_cp_write(cp, gs_sampler_state);
   i965_cp_write(cp, ps_sampler_state);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_VIEWPORT_STATE_POINTERS(const struct i965_gpe_gen6 *gpe,
                                          struct i965_cp *cp,
                                          uint32_t clip_viewport,
                                          uint32_t sf_viewport,
                                          uint32_t cc_viewport)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_VIEWPORT_STATE_POINTERS;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_VIEWPORT_STATE_POINTERS << 16 | (cmd_len - 2) |
                     GEN6_CLIP_VIEWPORT_MODIFY |
                     GEN6_SF_VIEWPORT_MODIFY |
                     GEN6_CC_VIEWPORT_MODIFY);
   i965_cp_write(cp, clip_viewport);
   i965_cp_write(cp, sf_viewport);
   i965_cp_write(cp, cc_viewport);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_SCISSOR_STATE_POINTERS(const struct i965_gpe_gen6 *gpe,
                                         struct i965_cp *cp,
                                         uint32_t scissor_rect)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_SCISSOR_STATE_POINTERS;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_SCISSOR_STATE_POINTERS << 16 | (cmd_len - 2));
   i965_cp_write(cp, scissor_rect);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_URB(const struct i965_gpe_gen6 *gpe,
                      struct i965_cp *cp,
                      int vs_entry_size, int num_vs_entries,
                      int gs_entry_size, int num_gs_entries)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_URB;
   int vs_urb_rows, gs_urb_rows;

   assert(gpe->gen == 6);

   /* in 1024-bit URB rows */
   vs_urb_rows = (vs_entry_size + 127) / 128;
   if (!vs_urb_rows)
      vs_urb_rows = 1;

   gs_urb_rows = (gs_entry_size + 127) / 128;
   if (!gs_urb_rows)
      gs_urb_rows = 1;

   /* the range is [24, 256] in multiples of 4 */
   num_vs_entries &= ~3;
   assert(num_vs_entries >= 24);

   /* the range is [0, 256] in multiples of 4 */
   num_gs_entries &= ~3;

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_URB << 16 | (cmd_len - 2));
   i965_cp_write(cp, (vs_urb_rows - 1) << GEN6_URB_VS_SIZE_SHIFT |
                     num_vs_entries << GEN6_URB_VS_ENTRIES_SHIFT);
   i965_cp_write(cp, (gs_urb_rows - 1) << GEN6_URB_GS_SIZE_SHIFT |
                     num_gs_entries << GEN6_URB_GS_ENTRIES_SHIFT);
   i965_cp_end(cp);
}

static void
gen6_emit_PIPE_CONTROL(const struct i965_gpe_gen6 *gpe,
                       struct i965_cp *cp,
                       uint32_t dw1,
                       struct intel_bo *bo, uint32_t bo_offset,
                       boolean is_64)
{
   const int cmd_len = (is_64) ? 5 : 4;
   const uint32_t read_domains = INTEL_DOMAIN_INSTRUCTION;
   const uint32_t write_domain = INTEL_DOMAIN_INSTRUCTION;

   assert(gpe->gen == 6);
   assert(cmd_len <= GEN6_MAX_PIPE_CONTROL);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_PIPE_CONTROL | (cmd_len - 2));
   i965_cp_write(cp, dw1);
   i965_cp_write_bo(cp, bo, read_domains, write_domain, bo_offset);
   i965_cp_write(cp, 0);
   if (is_64)
      i965_cp_write(cp, 0);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_INDEX_BUFFER(const struct i965_gpe_gen6 *gpe,
                               struct i965_cp *cp,
                               const struct pipe_index_buffer *ib)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_INDEX_BUFFER;
   const struct i965_resource *res = i965_resource(ib->buffer);
   uint32_t start_offset, end_offset;
   int type;

   assert(gpe->gen == 6);

   if (!res)
      return;

   type = i965_translate_index_size(ib->index_size);

   start_offset = ib->offset;
   if (start_offset % ib->index_size) {
      /* TODO copy to an aligned temporary bo */
      start_offset -= start_offset % ib->index_size;
   }

   end_offset = res->bo->get_size(res->bo) - 1;
   end_offset -= end_offset % ib->index_size;

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, CMD_INDEX_BUFFER << 16 | (cmd_len - 2) |
                     type << 8);
   i965_cp_write_bo(cp, res->bo, INTEL_DOMAIN_VERTEX, 0, start_offset);
   i965_cp_write_bo(cp, res->bo, INTEL_DOMAIN_VERTEX, 0, end_offset);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_VERTEX_BUFFERS(const struct i965_gpe_gen6 *gpe,
                                 struct i965_cp *cp,
                                 const struct pipe_vertex_buffer *vbuffers,
                                 int num_vbuffers)
{
   int cmd_len;
   int i;

   assert(gpe->gen == 6);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 82:
    *
    *     "From 1 to 33 VBs can be specified..."
    */
   assert(num_vbuffers <= 33);

   if (!num_vbuffers)
      return;

   cmd_len = 4 * num_vbuffers + 1;
   assert(cmd_len <= GEN6_MAX_3DSTATE_VERTEX_BUFFERS);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_VERTEX_BUFFERS << 16 | (cmd_len - 2));

   for (i = 0; i < num_vbuffers; i++) {
      const struct pipe_vertex_buffer *vb = &vbuffers[i];
      const struct i965_resource *res = i965_resource(vb->buffer);
      uint32_t dw, step_rate;

      /*
       * Always 0 as we do not claim instance divisor support yet.  Otherwise,
       * we need to map pipe vertex buffers to hw vertex buffers first.
       */
      step_rate = 0;

      dw = i << GEN6_VB0_INDEX_SHIFT;
      if (step_rate)
         dw |= GEN6_VB0_ACCESS_INSTANCEDATA;
      else
         dw |= GEN6_VB0_ACCESS_VERTEXDATA;
      if (gpe->gen >= 7)
         dw |= GEN7_VB0_ADDRESS_MODIFYENABLE;

      /* use null vb if the stride is unsupported */
      if (vb->stride <= 2048) {
         dw |= vb->stride << BRW_VB0_PITCH_SHIFT;
      }
      else {
         debug_printf("unsupported vb stride %d\n", vb->stride);
         dw |= 1 << 13;
      }

      if (res) {
         const uint32_t start_offset = vb->buffer_offset;
         const uint32_t end_offset = res->bo->get_size(res->bo) - 1;

         i965_cp_write(cp, dw);
         i965_cp_write_bo(cp, res->bo, INTEL_DOMAIN_VERTEX, 0, start_offset);
         i965_cp_write_bo(cp, res->bo, INTEL_DOMAIN_VERTEX, 0, end_offset);
         i965_cp_write(cp, step_rate);
      }
      else {
         i965_cp_write(cp, dw | 1 << 13);
         i965_cp_write(cp, 0);
         i965_cp_write(cp, 0);
         i965_cp_write(cp, step_rate);
      }
   }

   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_VERTEX_ELEMENTS(const struct i965_gpe_gen6 *gpe,
                                  struct i965_cp *cp,
                                  const struct pipe_vertex_element *velements,
                                  int num_velements)
{
   int cmd_len;
   int format, i;

   assert(gpe->gen == 6);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 93:
    *
    *     "Up to 34 (DevSNB+) vertex elements are supported."
    */
   assert(num_velements <= 34);

   if (!num_velements) {
      cmd_len = 3;
      format = BRW_SURFACEFORMAT_R32G32B32A32_FLOAT;

      i965_cp_begin(cp, cmd_len);
      i965_cp_write(cp, _3DSTATE_VERTEX_ELEMENTS << 16 | (cmd_len - 2));
      i965_cp_write(cp,
            0 << GEN6_VE0_INDEX_SHIFT |
            GEN6_VE0_VALID |
            format << BRW_VE0_FORMAT_SHIFT |
            0 << BRW_VE0_SRC_OFFSET_SHIFT);
      i965_cp_write(cp,
            BRW_VE1_COMPONENT_STORE_0 << BRW_VE1_COMPONENT_0_SHIFT |
            BRW_VE1_COMPONENT_STORE_0 << BRW_VE1_COMPONENT_1_SHIFT |
            BRW_VE1_COMPONENT_STORE_0 << BRW_VE1_COMPONENT_2_SHIFT |
            BRW_VE1_COMPONENT_STORE_1_FLT << BRW_VE1_COMPONENT_3_SHIFT);
      i965_cp_end(cp);

      return;
   }

   cmd_len = 2 * num_velements + 1;
   assert(cmd_len <= GEN6_MAX_3DSTATE_VERTEX_ELEMENTS);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_VERTEX_ELEMENTS << 16 | (cmd_len - 2));

   for (i = 0; i < num_velements; i++) {
      const struct pipe_vertex_element *ve = &velements[i];
      int comp[4] = {
         BRW_VE1_COMPONENT_STORE_SRC,
         BRW_VE1_COMPONENT_STORE_SRC,
         BRW_VE1_COMPONENT_STORE_SRC,
         BRW_VE1_COMPONENT_STORE_SRC,
      };

      switch (util_format_get_nr_components(ve->src_format)) {
      case 1: comp[1] = BRW_VE1_COMPONENT_STORE_0;
      case 2: comp[2] = BRW_VE1_COMPONENT_STORE_0;
      case 3: comp[3] = (util_format_is_pure_integer(ve->src_format)) ?
                        BRW_VE1_COMPONENT_STORE_1_INT :
                        BRW_VE1_COMPONENT_STORE_1_FLT;
      }

      format = i965_translate_vertex_format(ve->src_format);

      i965_cp_write(cp,
            ve->vertex_buffer_index << GEN6_VE0_INDEX_SHIFT |
            GEN6_VE0_VALID |
            format << BRW_VE0_FORMAT_SHIFT |
            ve->src_offset << BRW_VE0_SRC_OFFSET_SHIFT);

      i965_cp_write(cp,
            comp[0] << BRW_VE1_COMPONENT_0_SHIFT |
            comp[1] << BRW_VE1_COMPONENT_1_SHIFT |
            comp[2] << BRW_VE1_COMPONENT_2_SHIFT |
            comp[3] << BRW_VE1_COMPONENT_3_SHIFT);
   }

   i965_cp_end(cp);
}

static void
gen6_emit_3DPRIMITIVE(const struct i965_gpe_gen6 *gpe,
                      struct i965_cp *cp,
                      const struct pipe_draw_info *info)
{
   const int cmd_len = GEN6_SIZE_3DPRIMITIVE;
   const int prim = i965_translate_pipe_prim(info->mode);
   const int vb_access = (info->indexed) ?
      GEN4_3DPRIM_VERTEXBUFFER_ACCESS_RANDOM :
      GEN4_3DPRIM_VERTEXBUFFER_ACCESS_SEQUENTIAL;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, CMD_3D_PRIM << 16 | (cmd_len - 2) |
                     prim << GEN4_3DPRIM_TOPOLOGY_TYPE_SHIFT |
                     vb_access);
   i965_cp_write(cp, info->count);
   i965_cp_write(cp, info->start);
   i965_cp_write(cp, info->instance_count);
   i965_cp_write(cp, info->start_instance);
   i965_cp_write(cp, info->index_bias);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_VF_STATISTICS(const struct i965_gpe_gen6 *gpe,
                                struct i965_cp *cp, boolean enable)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_VF_STATISTICS;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, GM45_3DSTATE_VF_STATISTICS << 16 | !!enable);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_VS(const struct i965_gpe_gen6 *gpe,
                     struct i965_cp *cp,
                     const struct i965_shader *vs,
                     int max_threads, int num_samplers)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_VS;
   uint32_t dw2, dw4, dw5;
   int vue_read_len;

   assert(gpe->gen == 6);

   if (!vs) {
      i965_cp_begin(cp, cmd_len);
      i965_cp_write(cp, _3DSTATE_VS << 16 | (cmd_len - 2));
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_end(cp);
      return;
   }

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 135:
    *
    *     "(Vertex URB Entry Read Length) Specifies the number of pairs of
    *      128-bit vertex elements to be passed into the payload for each
    *      vertex."
    *
    *     "It is UNDEFINED to set this field to 0 indicating no Vertex URB data
    *      to be read and passed to the thread."
    */
   vue_read_len = (vs->in.count + 1) / 2;
   if (!vue_read_len)
      vue_read_len = 1;

   dw2 = ((num_samplers + 3) / 4) << GEN6_VS_SAMPLER_COUNT_SHIFT;
   if (FALSE)
      dw2 |= GEN6_VS_FLOATING_POINT_MODE_ALT;

   dw4 = vs->in.start_grf << GEN6_VS_DISPATCH_START_GRF_SHIFT |
         vue_read_len << GEN6_VS_URB_READ_LENGTH_SHIFT |
         0 << GEN6_VS_URB_ENTRY_READ_OFFSET_SHIFT;

   dw5 = (max_threads - 1) << GEN6_VS_MAX_THREADS_SHIFT |
         GEN6_VS_STATISTICS_ENABLE |
         GEN6_VS_ENABLE;

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_VS << 16 | (cmd_len - 2));
   i965_cp_write(cp, vs->cache_offset);
   i965_cp_write(cp, dw2);
   i965_cp_write(cp, 0);
   i965_cp_write(cp, dw4);
   i965_cp_write(cp, dw5);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_CONSTANT_VS(const struct i965_gpe_gen6 *gpe,
                              struct i965_cp *cp)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_CONSTANT_VS;

   assert(gpe->gen == 6);

   /* no push constant (yet) */
   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_CONSTANT_VS << 16 | (cmd_len - 2));
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_GS_SVB_INDEX(const struct i965_gpe_gen6 *gpe,
                               struct i965_cp *cp,
                               int index, unsigned svbi,
                               unsigned max_svbi)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_GS_SVB_INDEX;

   assert(gpe->gen == 6);
   assert(index >= 0 && index < 4);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_GS_SVB_INDEX << 16 | (cmd_len - 2));
   i965_cp_write(cp, index << SVB_INDEX_SHIFT);
   i965_cp_write(cp, svbi);
   i965_cp_write(cp, max_svbi);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_GS(const struct i965_gpe_gen6 *gpe,
                     struct i965_cp *cp,
                     const struct i965_shader *gs,
                     int max_threads, const struct i965_shader *vs)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_GS;

   if (!gs) {
      i965_cp_begin(cp, cmd_len);
      i965_cp_write(cp, _3DSTATE_GS << 16 | (cmd_len - 2));
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 1 << GEN6_GS_DISPATCH_START_GRF_SHIFT);
      i965_cp_write(cp, GEN6_GS_STATISTICS_ENABLE | GEN6_GS_RENDERING_ENABLE);
      i965_cp_write(cp, 0);
      i965_cp_end(cp);
      return;
   }

   /* VS ouputs must match GS inputs */
   assert(memcmp(&gs->in, &vs->out, sizeof(gs->in)) == 0);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_GS << 16 | (cmd_len - 2));
   i965_cp_write(cp, gs->cache_offset);
   i965_cp_write(cp, GEN6_GS_SPF_MODE |
                     GEN6_GS_VECTOR_MASK_ENABLE);
   i965_cp_write(cp, 0);
   i965_cp_write(cp, gs->in.start_grf << GEN6_GS_DISPATCH_START_GRF_SHIFT |
                     ((gs->in.count + 1) / 2) << GEN6_GS_URB_READ_LENGTH_SHIFT |
                     0 << GEN6_GS_URB_ENTRY_READ_OFFSET_SHIFT);
   i965_cp_write(cp, (max_threads - 1) << GEN6_GS_MAX_THREADS_SHIFT |
                     GEN6_GS_STATISTICS_ENABLE |
                     GEN6_GS_SO_STATISTICS_ENABLE |
                     GEN6_GS_RENDERING_ENABLE);
   i965_cp_write(cp, GEN6_GS_SVBI_PAYLOAD_ENABLE |
                     GEN6_GS_SVBI_POSTINCREMENT_ENABLE |
                     0 << GEN6_GS_SVBI_POSTINCREMENT_VALUE_SHIFT |
                     GEN6_GS_ENABLE);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_CONSTANT_GS(const struct i965_gpe_gen6 *gpe,
                              struct i965_cp *cp)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_CONSTANT_GS;

   assert(gpe->gen == 6);

   /* no push constant (yet) */
   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_CONSTANT_GS << 16 | (cmd_len - 2));
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_CLIP(const struct i965_gpe_gen6 *gpe,
                       struct i965_cp *cp,
                       const struct pipe_rasterizer_state *rasterizer,
                       boolean has_linear_interp,
                       boolean has_full_viewport)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_CLIP;
   uint32_t dw2, dw3;

   assert(gpe->gen == 6);

   dw2 = GEN6_CLIP_ENABLE |
         GEN6_CLIP_API_OGL |
         GEN6_CLIP_XY_TEST |
         rasterizer->clip_plane_enable << GEN6_USER_CLIP_CLIP_DISTANCES_SHIFT |
         GEN6_CLIP_MODE_NORMAL;

   /* enable guardband test when the viewport matches the fb size */
   if (has_full_viewport)
      dw2 |= GEN6_CLIP_GB_TEST;

   if (rasterizer->depth_clip)
      dw2 |= GEN6_CLIP_Z_TEST;

   if (has_linear_interp)
      dw2 |= GEN6_CLIP_NON_PERSPECTIVE_BARYCENTRIC_ENABLE;

   if (rasterizer->flatshade_first) {
      dw2 |= 0 << GEN6_CLIP_TRI_PROVOKE_SHIFT |
             1 << GEN6_CLIP_TRIFAN_PROVOKE_SHIFT |
             0 << GEN6_CLIP_LINE_PROVOKE_SHIFT;
   }
   else {
      dw2 |= 2 << GEN6_CLIP_TRI_PROVOKE_SHIFT |
             2 << GEN6_CLIP_TRIFAN_PROVOKE_SHIFT |
             1 << GEN6_CLIP_LINE_PROVOKE_SHIFT;
   }

   dw3 = 0x1 << GEN6_CLIP_MIN_POINT_WIDTH_SHIFT |
         0x7ff << GEN6_CLIP_MAX_POINT_WIDTH_SHIFT |
         GEN6_CLIP_FORCE_ZERO_RTAINDEX;

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_CLIP << 16 | (cmd_len - 2));
   i965_cp_write(cp, GEN6_CLIP_STATISTICS_ENABLE);
   i965_cp_write(cp, dw2);
   i965_cp_write(cp, dw3);
   i965_cp_end(cp);
}

static uint32_t
gen6_emit_CLIP_VIEWPORT(const struct i965_gpe_gen6 *gpe,
                        struct i965_cp *cp,
                        const struct pipe_viewport_state *viewports,
                        int num_viewports)
{
   const int state_align = 32 / 4;
   const int state_len = 4 * num_viewports;
   uint32_t state_offset;
   int i;

   assert(gpe->gen == 6);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 193:
    *
    *     "The viewport-related state is stored as an array of up to 16
    *      elements..."
    */
   assert(num_viewports && num_viewports <= 16);

   assert(state_len + state_align - 1 <= GEN6_MAX_CLIP_VIEWPORT);
   i965_cp_steal(cp, "CLIP_VIEWPORT", state_len, state_align, &state_offset);

   for (i = 0; i < num_viewports; i++) {
      const struct pipe_viewport_state *vp = &viewports[i];
      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 234:
       *
       *     "The screen-aligned 2D bounding-box of an object must not exceed
       *      8K pixels in either X or Y..."
       *
       *     "A similar restriction applies to [DevSNB], though the guardband
       *      and maximum delta are doubled from legacy products."
       */
      const float max_delta = 16384.0f;
      const float max_extent = max_delta / 2.0f;
      const float xscale = fabs(vp->scale[0]);
      const float yscale = fabs(vp->scale[1]);
      float xmin, xmax, ymin, ymax;

      /* screen space to NDC space */
      xmin = (-max_extent - vp->translate[0]) / xscale;
      xmax = ( max_extent - vp->translate[0]) / xscale;
      ymin = (-max_extent - vp->translate[1]) / yscale;
      ymax = ( max_extent - vp->translate[1]) / yscale;

      i965_cp_write(cp, fui(xmin));
      i965_cp_write(cp, fui(xmax));
      i965_cp_write(cp, fui(ymin));
      i965_cp_write(cp, fui(ymax));
   }

   i965_cp_end(cp);

   return state_offset;
}

static void
gen6_emit_3DSTATE_DRAWING_RECTANGLE(const struct i965_gpe_gen6 *gpe,
                                    struct i965_cp *cp,
                                    int width, int height)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_DRAWING_RECTANGLE;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_DRAWING_RECTANGLE << 16 | (cmd_len - 2));
   i965_cp_write(cp, 0);
   i965_cp_write(cp, ((width - 1) & 0xffff) | (height - 1) << 16);
   i965_cp_write(cp, 0);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_SF(const struct i965_gpe_gen6 *gpe,
                     struct i965_cp *cp,
                     const struct pipe_rasterizer_state *rasterizer,
                     const struct i965_shader *vs,
                     const struct i965_shader *fs)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_SF;
   uint32_t dw1, dw2, dw3, dw4, dw16, dw17;
   uint16_t attr_ctrl[PIPE_MAX_SHADER_INPUTS];
   int vue_offset, vue_len;
   int line_width, point_size;
   int i;

   assert(gpe->gen == 6);

   dw1 = fs->in.count << GEN6_SF_NUM_OUTPUTS_SHIFT |
         GEN6_SF_SWIZZLE_ENABLE;

   /* skip PSIZE and POSITION */
   assert(vs->out.semantic_names[0] == TGSI_SEMANTIC_PSIZE);
   assert(vs->out.semantic_names[1] == TGSI_SEMANTIC_POSITION);
   vue_offset = 2;
   vue_len = vs->out.count - vue_offset;
   if (!vue_len)
      vue_len = 1;

   dw1 |= (vue_len + 1) / 2 << GEN6_SF_URB_ENTRY_READ_LENGTH_SHIFT |
          vue_offset / 2 << GEN6_SF_URB_ENTRY_READ_OFFSET_SHIFT;

   switch (rasterizer->sprite_coord_mode) {
   case PIPE_SPRITE_COORD_UPPER_LEFT:
      dw1 |= GEN6_SF_POINT_SPRITE_UPPERLEFT;
      break;
   case PIPE_SPRITE_COORD_LOWER_LEFT:
      dw1 |= GEN6_SF_POINT_SPRITE_LOWERLEFT;
      break;
   }

   dw2 = GEN6_SF_STATISTICS_ENABLE |
         GEN6_SF_VIEWPORT_TRANSFORM_ENABLE;

   if (rasterizer->offset_tri)
      dw2 |= GEN6_SF_GLOBAL_DEPTH_OFFSET_SOLID;
   if (rasterizer->offset_line)
      dw2 |= GEN6_SF_GLOBAL_DEPTH_OFFSET_WIREFRAME;
   if (rasterizer->offset_point)
      dw2 |= GEN6_SF_GLOBAL_DEPTH_OFFSET_POINT;

   switch (rasterizer->fill_front) {
   case PIPE_POLYGON_MODE_FILL:
      dw2 |= GEN6_SF_FRONT_SOLID;
      break;
   case PIPE_POLYGON_MODE_LINE:
      dw2 |= GEN6_SF_FRONT_WIREFRAME;
      break;
   case PIPE_POLYGON_MODE_POINT:
      dw2 |= GEN6_SF_FRONT_POINT;
      break;
   }
   switch (rasterizer->fill_back) {
   case PIPE_POLYGON_MODE_FILL:
      dw2 |= GEN6_SF_BACK_SOLID;
      break;
   case PIPE_POLYGON_MODE_LINE:
      dw2 |= GEN6_SF_BACK_WIREFRAME;
      break;
   case PIPE_POLYGON_MODE_POINT:
      dw2 |= GEN6_SF_BACK_POINT;
      break;
   }

   if (rasterizer->front_ccw)
      dw2 |= GEN6_SF_WINDING_CCW;

   dw3 = 0;

   switch (rasterizer->cull_face) {
   case PIPE_FACE_NONE:
      dw3 |= GEN6_SF_CULL_NONE;
      break;
   case PIPE_FACE_FRONT:
      dw3 |= GEN6_SF_CULL_FRONT;
      break;
   case PIPE_FACE_BACK:
      dw3 |= GEN6_SF_CULL_BACK;
      break;
   case PIPE_FACE_FRONT_AND_BACK:
      dw3 |= GEN6_SF_CULL_BOTH;
      break;
   }

   /* in U3.7 */
   line_width = (int) (rasterizer->line_width * 128.0f + 0.5f);
   line_width = CLAMP(line_width, 1, 1023);

   dw3 |= line_width << GEN6_SF_LINE_WIDTH_SHIFT;

   if (rasterizer->scissor)
      dw3 |= GEN6_SF_SCISSOR_ENABLE;

   if (rasterizer->line_smooth) {
      dw3 |= GEN6_SF_LINE_AA_ENABLE |
             GEN6_SF_LINE_AA_MODE_TRUE |
             GEN6_SF_LINE_END_CAP_WIDTH_1_0;
   }
   if (FALSE)
      dw3 |= GEN6_SF_MSRAST_ON_PATTERN;

   dw4 = 0;

   if (rasterizer->flatshade_first) {
      dw4 |= 0 << GEN6_SF_TRI_PROVOKE_SHIFT |
             0 << GEN6_SF_LINE_PROVOKE_SHIFT |
             1 << GEN6_SF_TRIFAN_PROVOKE_SHIFT;
   }
   else {
      dw4 |= 2 << GEN6_SF_TRI_PROVOKE_SHIFT |
             1 << GEN6_SF_LINE_PROVOKE_SHIFT |
             2 << GEN6_SF_TRIFAN_PROVOKE_SHIFT;
   }

   if (!rasterizer->point_size_per_vertex)
      dw4 |= GEN6_SF_USE_STATE_POINT_WIDTH;

   /* in U8.3 */
   point_size = (int) (rasterizer->point_size * 8.0f + 0.5f);
   point_size = CLAMP(point_size, 1, 2047);
   dw4 |= point_size;

   dw16 = 0;
   dw17 = 0;
   for (i = 0; i < fs->in.count; i++) {
      const int semantic = fs->in.semantic_names[i];
      const int index = fs->in.semantic_indices[i];
      const int interp = fs->in.interp[i];
      uint16_t ctrl;
      int j;

      if (semantic == TGSI_SEMANTIC_GENERIC &&
          (rasterizer->sprite_coord_enable & (1 << index)))
         dw16 |= 1 << i;

      if (interp == TGSI_INTERPOLATE_CONSTANT ||
          (interp == TGSI_INTERPOLATE_COLOR && rasterizer->flatshade))
         dw17 |= 1 << i;

      /* find the matching VS OUT for FS IN[i] */
      ctrl = 0;
      for (j = 0; j < vue_len; j++) {
         if (vs->out.semantic_names[j + vue_offset] != semantic ||
             vs->out.semantic_indices[j + vue_offset] != index)
            continue;

         ctrl = j;

         if (semantic == TGSI_SEMANTIC_COLOR &&
             rasterizer->light_twoside) {
            const int next = j + vue_offset + 1;

            if (vs->out.semantic_names[next] == TGSI_SEMANTIC_BCOLOR &&
                vs->out.semantic_indices[next] == index) {
               ctrl |= ATTRIBUTE_SWIZZLE_INPUTATTR_FACING <<
                  ATTRIBUTE_SWIZZLE_SHIFT;
            }
         }

         break;
      }

      /* if there is no COLOR, try BCOLOR */
      if (j >= vue_len && semantic == TGSI_SEMANTIC_COLOR) {
         for (j = 0; j < vue_len; j++) {
            if (vs->out.semantic_names[j + vue_offset] !=
                  TGSI_SEMANTIC_BCOLOR ||
                vs->out.semantic_indices[j + vue_offset] != index)
               continue;

            ctrl = j;
            break;
         }
      }

      attr_ctrl[i] = ctrl;
   }

   for (; i < Elements(attr_ctrl); i++)
      attr_ctrl[i] = 0;

   /* only the first 16 attributes can be remapped */
   for (i = 16; i < Elements(attr_ctrl); i++)
      assert(attr_ctrl[i] == 0 || attr_ctrl[i] == i);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_SF << 16 | (cmd_len - 2));
   i965_cp_write(cp, dw1);
   i965_cp_write(cp, dw2);
   i965_cp_write(cp, dw3);
   i965_cp_write(cp, dw4);
   i965_cp_write(cp, fui(rasterizer->offset_units * 2.0f));
   i965_cp_write(cp, fui(rasterizer->offset_scale));
   i965_cp_write(cp, fui(rasterizer->offset_clamp));
   for (i = 0; i < 8; i++)
      i965_cp_write(cp, attr_ctrl[2 * i + 1] << 16 | attr_ctrl[2 * i]);
   i965_cp_write(cp, dw16);
   i965_cp_write(cp, dw17);
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_end(cp);
}

static uint32_t
gen6_emit_SF_VIEWPORT(const struct i965_gpe_gen6 *gpe,
                      struct i965_cp *cp,
                      const struct pipe_viewport_state *viewports,
                      int num_viewports)
{
   const int state_align = 32 / 4;
   const int state_len = 8 * num_viewports;
   uint32_t state_offset;
   int i;

   assert(gpe->gen == 6);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 262:
    *
    *     "The viewport-specific state used by the SF unit (SF_VIEWPORT) is
    *      stored as an array of up to 16 elements..."
    */
   assert(num_viewports && num_viewports <= 16);

   assert(state_len + state_align - 1 <= GEN6_MAX_SF_VIEWPORT);
   i965_cp_steal(cp, "SF_VIEWPORT", state_len, state_align, &state_offset);

   for (i = 0; i < num_viewports; i++) {
      const struct pipe_viewport_state *vp = &viewports[i];

      i965_cp_write(cp, fui(vp->scale[0]));
      i965_cp_write(cp, fui(vp->scale[1]));
      i965_cp_write(cp, fui(vp->scale[2]));
      i965_cp_write(cp, fui(vp->translate[0]));
      i965_cp_write(cp, fui(vp->translate[1]));
      i965_cp_write(cp, fui(vp->translate[2]));
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
   }

   i965_cp_end(cp);

   return state_offset;
}

static uint32_t
gen6_emit_SCISSOR_RECT(const struct i965_gpe_gen6 *gpe,
                       struct i965_cp *cp,
                       const struct pipe_scissor_state *scissors,
                       int num_scissors)
{
   const int state_align = 32 / 4;
   const int state_len = 2 * num_scissors;
   uint32_t state_offset;
   int i;

   assert(gpe->gen == 6);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 263:
    *
    *     "The viewport-specific state used by the SF unit (SCISSOR_RECT) is
    *      stored as an array of up to 16 elements..."
    */
   assert(num_scissors && num_scissors <= 16);

   assert(state_len + state_align - 1 <= GEN6_MAX_SCISSOR_RECT);
   i965_cp_steal(cp, "SCISSOR_RECT", state_len, state_align, &state_offset);

   for (i = 0; i < num_scissors; i++) {
      uint32_t dw0, dw1;

      if (scissors[i].minx >= scissors[i].maxx ||
          scissors[i].miny >= scissors[i].maxy) {
         /* we have to make min greater than max as they are both inclusive */
         dw0 = 1 << 16 | 1;
         dw1 = 0;
      }
      else {
         dw0 = scissors[i].miny << 16 | scissors[i].minx;
         dw1 = (scissors[i].maxy - 1) << 16 | (scissors[i].maxx - 1);
      }

      i965_cp_write(cp, dw0);
      i965_cp_write(cp, dw1);
   }

   i965_cp_end(cp);

   return state_offset;
}

static void
gen6_emit_3DSTATE_WM(const struct i965_gpe_gen6 *gpe,
                     struct i965_cp *cp,
                     const struct i965_shader *fs,
                     int max_threads, int num_samplers,
                     const struct pipe_rasterizer_state *rasterizer,
                     boolean dual_blend)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_WM;
   uint32_t dw2, dw4, dw5, dw6;
   const boolean dispatch_8 = FALSE;
   const boolean dispatch_16 = TRUE;

   assert(gpe->gen == 6);

   if (!fs) {
      i965_cp_begin(cp, cmd_len);
      i965_cp_write(cp, _3DSTATE_WM << 16 | (cmd_len - 2));
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_end(cp);
      return;
   }

   dw2 = (num_samplers + 3) / 4 << GEN6_WM_SAMPLER_COUNT_SHIFT;
   if (FALSE)
      dw2 |= GEN6_WM_FLOATING_POINT_MODE_ALT;

   dw4 = GEN6_WM_STATISTICS_ENABLE |
         fs->in.start_grf << GEN6_WM_DISPATCH_START_GRF_SHIFT_0 |
         0 << GEN6_WM_DISPATCH_START_GRF_SHIFT_2;

   dw5 = (max_threads - 1) << GEN6_WM_MAX_THREADS_SHIFT |
         GEN6_WM_LINE_END_CAP_AA_WIDTH_0_5 |
         GEN6_WM_LINE_AA_WIDTH_1_0;

   if (fs->has_kill)
      dw5 |= GEN6_WM_KILL_ENABLE;

   if (fs->out.has_pos)
      dw5 |= GEN6_WM_COMPUTED_DEPTH;
   if (fs->in.has_pos)
      dw5 |= GEN6_WM_USES_SOURCE_DEPTH | GEN6_WM_USES_SOURCE_W;

   if (TRUE)
      dw5 |= GEN6_WM_DISPATCH_ENABLE;

   if (rasterizer->poly_stipple_enable)
      dw5 |= GEN6_WM_POLYGON_STIPPLE_ENABLE;
   if (rasterizer->line_stipple_enable)
      dw5 |= GEN6_WM_LINE_STIPPLE_ENABLE;

   if (dual_blend)
      dw5 |= GEN6_WM_DUAL_SOURCE_BLEND_ENABLE;

   if (dispatch_16)
      dw5 |= GEN6_WM_16_DISPATCH_ENABLE;
   if (dispatch_8)
      dw5 |= GEN6_WM_8_DISPATCH_ENABLE;

   dw6 = fs->in.count << GEN6_WM_NUM_SF_OUTPUTS_SHIFT |
         fs->in.barycentric_interpolation_mode <<
           GEN6_WM_BARYCENTRIC_INTERPOLATION_MODE_SHIFT;

   if (FALSE) {
      if (rasterizer->multisample)
         dw6 |= GEN6_WM_MSRAST_ON_PATTERN;
      else
         dw6 |= GEN6_WM_MSRAST_OFF_PIXEL;
      dw6 |= GEN6_WM_MSDISPMODE_PERPIXEL;
   }
   else {
      dw6 |= GEN6_WM_MSRAST_OFF_PIXEL |
             GEN6_WM_MSDISPMODE_PERSAMPLE;
   }

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_WM << 16 | (cmd_len - 2));
   i965_cp_write(cp, fs->cache_offset);
   i965_cp_write(cp, dw2);
   i965_cp_write(cp, 0);
   i965_cp_write(cp, dw4);
   i965_cp_write(cp, dw5);
   i965_cp_write(cp, dw6);
   i965_cp_write(cp, 0); /* kernel 1 */
   i965_cp_write(cp, 0); /* kernel 2 */
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_CONSTANT_PS(const struct i965_gpe_gen6 *gpe,
                              struct i965_cp *cp)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_CONSTANT_PS;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_CONSTANT_PS << 16 | (cmd_len - 2));
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_SAMPLE_MASK(const struct i965_gpe_gen6 *gpe,
                              struct i965_cp *cp,
                              unsigned sample_mask)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_SAMPLE_MASK;

   assert(gpe->gen == 6);

   /* at most 4 samples on GEN6 */
   assert((sample_mask & 0xf) == sample_mask);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_SAMPLE_MASK << 16 | (cmd_len - 2));
   i965_cp_write(cp, sample_mask);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_AA_LINE_PARAMETERS(const struct i965_gpe_gen6 *gpe,
                                     struct i965_cp *cp)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_AA_LINE_PARAMETERS;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_AA_LINE_PARAMETERS << 16 | (cmd_len - 2));
   i965_cp_write(cp, 0);
   i965_cp_write(cp, 0);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_LINE_STIPPLE(const struct i965_gpe_gen6 *gpe,
                               struct i965_cp *cp,
                               unsigned pattern, unsigned factor)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_LINE_STIPPLE;
   unsigned inverse;

   assert(gpe->gen == 6);
   assert((pattern & 0xffff) == pattern);
   assert(factor >= 1 && factor <= 256);

   /* in U1.13 */
   inverse = (unsigned) (8192.0f / factor + 0.5f);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_LINE_STIPPLE_PATTERN << 16 | (cmd_len - 2));
   i965_cp_write(cp, pattern);
   i965_cp_write(cp, inverse << 16 | factor);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_POLY_STIPPLE_OFFSET(const struct i965_gpe_gen6 *gpe,
                                      struct i965_cp *cp,
                                      int x_offset, int y_offset)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_POLY_STIPPLE_OFFSET;

   assert(gpe->gen == 6);
   assert(x_offset >= 0 && x_offset <= 31);
   assert(y_offset >= 0 && y_offset <= 31);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_POLY_STIPPLE_OFFSET << 16 | (cmd_len - 2));
   i965_cp_write(cp, x_offset << 8 | y_offset);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_POLY_STIPPLE_PATTERN(const struct i965_gpe_gen6 *gpe,
                                       struct i965_cp *cp,
                                       const struct pipe_poly_stipple *pattern)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_POLY_STIPPLE_PATTERN;
   int i;

   assert(gpe->gen == 6);
   assert(Elements(pattern->stipple) == 32);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_POLY_STIPPLE_PATTERN << 16 | (cmd_len - 2));
   for (i = 0; i < 32; i++)
      i965_cp_write(cp, pattern->stipple[i]);
   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_MULTISAMPLE(const struct i965_gpe_gen6 *gpe,
                              struct i965_cp *cp,
                              int num_samples)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_MULTISAMPLE;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_MULTISAMPLE << 16 | (cmd_len - 2));

   /* see gen6_emit_3dstate_multisample() */
   switch (num_samples) {
   case 0:
   case 1:
      i965_cp_write(cp, MS_PIXEL_LOCATION_CENTER | MS_NUMSAMPLES_1);
      i965_cp_write(cp, 0);
      break;
   case 4:
      i965_cp_write(cp, MS_PIXEL_LOCATION_CENTER | MS_NUMSAMPLES_4);
      i965_cp_write(cp, 0xae2ae662);
      break;
   default:
      assert(!"unsupported sample count");
      i965_cp_write(cp, MS_PIXEL_LOCATION_CENTER | MS_NUMSAMPLES_1);
      i965_cp_write(cp, 0);
      break;
   }

   i965_cp_end(cp);
}

static void
gen6_emit_3DSTATE_DEPTH_BUFFER(const struct i965_gpe_gen6 *gpe,
                               struct i965_cp *cp,
                               const struct pipe_surface *surface)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_DEPTH_BUFFER;

   assert(gpe->gen == 6);

   if (surface) {
      struct i965_resource *res = i965_resource(surface->texture);
      uint32_t slice_offset, x_offset, y_offset;
      int format, pitch;

      /* required for GEN6+ */
      assert(res->tiling == INTEL_TILING_Y);
      assert(surface->u.tex.first_layer == surface->u.tex.last_layer);

      format = i965_translate_depth_format(surface->format);
      pitch = res->bo_stride - 1;

      slice_offset = i965_resource_get_slice_offset(res,
            surface->u.tex.level, surface->u.tex.first_layer,
            TRUE, &x_offset, &y_offset);

      i965_cp_begin(cp, cmd_len);
      i965_cp_write(cp, _3DSTATE_DEPTH_BUFFER << 16 | (cmd_len - 2));
      i965_cp_write(cp, BRW_SURFACE_2D << 29 |
                        (res->tiling != INTEL_TILING_NONE) << 27 |
                        (res->tiling == INTEL_TILING_Y) << 26 |
                        format << 18 |
                        pitch);
      i965_cp_write_bo(cp, res->bo,
            INTEL_DOMAIN_RENDER, INTEL_DOMAIN_RENDER, slice_offset);
      i965_cp_write(cp, (surface->height + y_offset - 1) << 19 |
                        (surface->width + x_offset - 1) << 6 |
                        BRW_SURFACE_MIPMAPLAYOUT_BELOW << 1);
      i965_cp_write(cp, surface->u.tex.first_layer << 10);
      i965_cp_write(cp, y_offset << 16 | x_offset);
      i965_cp_write(cp, 0);
      i965_cp_end(cp);
   }
   else {
      i965_cp_begin(cp, cmd_len);
      i965_cp_write(cp, _3DSTATE_DEPTH_BUFFER << 16 | (cmd_len - 2));
      i965_cp_write(cp, BRW_SURFACE_NULL << 29 |
                        BRW_DEPTHFORMAT_D32_FLOAT << 18);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_write(cp, 0);
      i965_cp_end(cp);
   }
}

static void
gen6_emit_3DSTATE_STENCIL_BUFFER(const struct i965_gpe_gen6 *gpe,
                                 struct i965_cp *cp,
                                 const struct pipe_surface *surface)
{
   assert(!"no 3DSTATE_STENCIL_BUFFER support yet");
}

static void
gen6_emit_3DSTATE_HIER_DEPTH_BUFFER(const struct i965_gpe_gen6 *gpe,
                                    struct i965_cp *cp,
                                    const struct pipe_surface *surface)
{
   assert(!"no 3DSTATE_HIER_DEPTH_BUFFER support yet");
}

static void
gen6_emit_3DSTATE_CLEAR_PARAMS(const struct i965_gpe_gen6 *gpe,
                               struct i965_cp *cp,
                               float clear_val)
{
   const int cmd_len = GEN6_SIZE_3DSTATE_CLEAR_PARAMS;

   assert(gpe->gen == 6);

   i965_cp_begin(cp, cmd_len);
   i965_cp_write(cp, _3DSTATE_CLEAR_PARAMS << 16 | (cmd_len - 2) |
                     GEN5_DEPTH_CLEAR_VALID);
   i965_cp_write(cp, fui(clear_val));
   i965_cp_end(cp);
}

static uint32_t
gen6_emit_COLOR_CALC_STATE(const struct i965_gpe_gen6 *gpe,
                           struct i965_cp *cp,
                           const struct pipe_stencil_ref *stencil_ref,
                           float alpha_ref,
                           const struct pipe_blend_color *blend_color)
{
   const int state_align = 64 / 4;
   const int state_len = 6;
   uint32_t state_offset;

   assert(gpe->gen == 6);

   assert(state_len + state_align - 1 <= GEN6_MAX_COLOR_CALC_STATE);
   i965_cp_steal(cp, "COLOR_CALC_STATE",
         state_len, state_align, &state_offset);

   i965_cp_write(cp, stencil_ref->ref_value[0] << 24 |
                     stencil_ref->ref_value[1] << 16 |
                     BRW_ALPHATEST_FORMAT_UNORM8);
   i965_cp_write(cp, float_to_ubyte(alpha_ref));
   i965_cp_write(cp, fui(blend_color->color[0]));
   i965_cp_write(cp, fui(blend_color->color[1]));
   i965_cp_write(cp, fui(blend_color->color[2]));
   i965_cp_write(cp, fui(blend_color->color[3]));
   i965_cp_end(cp);

   return state_offset;
}

static uint32_t
gen6_emit_DEPTH_STENCIL_STATE(const struct i965_gpe_gen6 *gpe,
                              struct i965_cp *cp,
                              const struct pipe_depth_stencil_alpha_state *dsa)
{
   const int state_align = 64 / 4;
   const int state_len = 3;
   uint32_t state_offset;
   uint32_t dw0, dw1, dw2;

   assert(gpe->gen == 6);

   if (dsa->stencil[0].enabled) {
      dw0 = 1 << 31 |
            i965_translate_dsa_func(dsa->stencil[0].func) << 28 |
            i965_translate_pipe_stencil_op(dsa->stencil[0].fail_op) << 25 |
            i965_translate_pipe_stencil_op(dsa->stencil[0].zfail_op) << 22 |
            i965_translate_pipe_stencil_op(dsa->stencil[0].zpass_op) << 19;
      if (dsa->stencil[0].writemask)
         dw0 |= 1 << 18;

      dw1 = dsa->stencil[0].valuemask << 24 |
            dsa->stencil[0].writemask << 16;

      if (dsa->stencil[1].enabled) {
         dw0 |= 1 << 15 |
                i965_translate_dsa_func(dsa->stencil[1].func) << 12 |
                i965_translate_pipe_stencil_op(dsa->stencil[1].fail_op) << 9 |
                i965_translate_pipe_stencil_op(dsa->stencil[1].zfail_op) << 6 |
                i965_translate_pipe_stencil_op(dsa->stencil[1].zpass_op) << 3;
         if (dsa->stencil[1].writemask)
            dw0 |= 1 << 18;

         dw1 |= dsa->stencil[1].valuemask << 8 |
                dsa->stencil[1].writemask;
      }
   }
   else {
      dw0 = 0;
      dw1 = 0;
   }

   dw2 = dsa->depth.enabled << 31 |
         dsa->depth.writemask << 26;
   if (dsa->depth.enabled)
      dw2 |= i965_translate_dsa_func(dsa->depth.func) << 27;
   else
      dw2 |= BRW_COMPAREFUNCTION_ALWAYS << 27;

   assert(state_len + state_align - 1 <= GEN6_MAX_DEPTH_STENCIL_STATE);
   i965_cp_steal(cp, "DEPTH_STENCIL_STATE",
         state_len, state_align, &state_offset);
   i965_cp_write(cp, dw0);
   i965_cp_write(cp, dw1);
   i965_cp_write(cp, dw2);
   i965_cp_end(cp);

   return state_offset;
}

static uint32_t
gen6_emit_BLEND_STATE(const struct i965_gpe_gen6 *gpe,
                      struct i965_cp *cp,
                      const struct pipe_blend_state *blend,
                      const struct pipe_framebuffer_state *framebuffer,
                      const struct pipe_alpha_state *alpha)
{
   const int state_align = 64 / 4;
   int state_len;
   uint32_t state_offset;
   int num_targets, i;

   assert(gpe->gen == 6);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 376:
    *
    *     "The blend state is stored as an array of up to 8 elements..."
    */
   num_targets = framebuffer->nr_cbufs;
   assert(num_targets <= 8);

   if (!num_targets) {
      if (!alpha->enabled)
         return 0;
      /* to be able to reference alpha func */
      num_targets = 1;
   }

   state_len = 2 * num_targets;
   assert(state_len + state_align - 1 <= GEN6_MAX_BLEND_STATE);

   i965_cp_steal(cp, "BLEND_STATE", state_len, state_align, &state_offset);

   for (i = 0; i < num_targets; i++) {
      const struct pipe_rt_blend_state *rt =
         &blend->rt[(blend->independent_blend_enable) ? i : 0];
      uint32_t dw0, dw1;

      dw0 = 0;
      dw1 = BRW_RENDERTARGET_CLAMPRANGE_FORMAT << 2 | 0x3;

      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 365:
       *
       *     "* Color Buffer Blending and Logic Ops must not be enabled
       *        simultaneously, or behavior is UNDEFINED.
       *      * Logic Ops are only supported on *_UNORM surfaces (excluding
       *        _SRGB variants), otherwise Logic Ops must be DISABLED."
       *
       * Since blend->logicop_enable takes precedence over rt->blend_enable,
       * and logicop is ignored for non-UNORM color buffers, no special care
       * is needed.
       */
      if (blend->logicop_enable) {
         const struct util_format_description *desc = (framebuffer->nr_cbufs) ?
            util_format_description(framebuffer->cbufs[i]->format) : NULL;
         boolean ignore_logicop = FALSE;

         if (desc) {
            int i;
            for (i = 0; i < 4; i++) {
               if (desc->channel[i].type != UTIL_FORMAT_TYPE_VOID &&
                   desc->channel[i].type != UTIL_FORMAT_TYPE_UNSIGNED) {
                  ignore_logicop = TRUE;
                  break;
               }
            }
         }

         if (!ignore_logicop) {
            dw1 |= 1 << 22 |
                   i965_translate_pipe_logicop(blend->logicop_func) << 18;
         }
      }
      else if (rt->blend_enable) {
         dw0 |= 1 << 31 |
                i965_translate_pipe_blend(rt->alpha_func) << 26 |
                i965_translate_pipe_blendfactor(rt->alpha_src_factor) << 20 |
                i965_translate_pipe_blendfactor(rt->alpha_dst_factor) << 15 |
                i965_translate_pipe_blend(rt->rgb_func) << 11 |
                i965_translate_pipe_blendfactor(rt->rgb_src_factor) << 5 |
                i965_translate_pipe_blendfactor(rt->rgb_dst_factor);
         if (rt->rgb_func != rt->alpha_func ||
             rt->rgb_src_factor != rt->alpha_src_factor ||
             rt->rgb_dst_factor != rt->alpha_dst_factor)
            dw0 |= 1 << 30;
      }

      if (blend->alpha_to_coverage)
         dw1 |= 1 << 31;
      if (blend->alpha_to_one)
         dw1 |= 1 << 30;
      if (gpe->gen >= 7)
         dw1 |= 1 << 29;
      if (!(rt->colormask & PIPE_MASK_A))
         dw1 |= 1 << 27;
      if (!(rt->colormask & PIPE_MASK_R))
         dw1 |= 1 << 26;
      if (!(rt->colormask & PIPE_MASK_G))
         dw1 |= 1 << 25;
      if (!(rt->colormask & PIPE_MASK_B))
         dw1 |= 1 << 24;
      if (alpha->enabled) {
         dw1 |= 1 << 16 |
                i965_translate_dsa_func(alpha->func) << 13;
      }
      if (blend->dither)
         dw1 |= 1 << 12;

      i965_cp_write(cp, dw0);
      i965_cp_write(cp, dw1);
   }

   i965_cp_end(cp);

   return state_offset;
}

static uint32_t
gen6_emit_CC_VIEWPORT(const struct i965_gpe_gen6 *gpe,
                      struct i965_cp *cp,
                      const struct pipe_viewport_state *viewports,
                      int num_viewports, boolean depth_clip)
{
   const int state_align = 32 / 4;
   const int state_len = 2 * num_viewports;
   uint32_t state_offset;
   int i;

   assert(gpe->gen == 6);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 385:
    *
    *     "The viewport state is stored as an array of up to 16 elements..."
    */
   assert(num_viewports && num_viewports <= 16);

   assert(state_len + state_align - 1 <= GEN6_MAX_CC_VIEWPORT);
   i965_cp_steal(cp, "CC_VIEWPORT", state_len, state_align, &state_offset);

   for (i = 0; i < num_viewports; i++) {
      const struct pipe_viewport_state *vp = &viewports[i];
      float min, max;

      if (depth_clip) {
         min = 0.0f;
         max = 1.0f;
      }
      else {
         float scale = fabs(vp->scale[2]);
         min = vp->translate[2] - scale;
         max = vp->translate[2] + scale;
      }

      i965_cp_write(cp, fui(min));
      i965_cp_write(cp, fui(max));
   }


   i965_cp_end(cp);

   return state_offset;
}

static uint32_t
gen6_emit_BINDING_TABLE_STATE(const struct i965_gpe_gen6 *gpe,
                              struct i965_cp *cp,
                              uint32_t *surface_states,
                              int num_surface_states)
{
   const int state_align = 32 / 4;
   const int state_len = num_surface_states;
   uint32_t state_offset;

   assert(gpe->gen == 6);

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 69:
    *
    *     "It is stored as an array of up to 256 elements..."
    */
   assert(num_surface_states <= 256);

   if (!num_surface_states)
      return 0;

   assert(state_len + state_align - 1 <= GEN6_MAX_BINDING_TABLE_STATE);

   i965_cp_steal(cp, "BINDING_TABLE_STATE",
         state_len, state_align, &state_offset);
   i965_cp_write_multi(cp, surface_states, num_surface_states);
   i965_cp_end(cp);

   return state_offset;
}

static void
gen6_fill_SURFACE_STATE_NULL(uint32_t *dw, unsigned width, unsigned height,
                             unsigned depth, unsigned lod)
{
   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 71:
    *
    *     "A null surface will be used in instances where an actual surface is
    *      not bound. When a write message is generated to a null surface, no
    *      actual surface is written to. When a read message (including any
    *      sampling engine message) is generated to a null surface, the result
    *      is all zeros. Note that a null surface type is allowed to be used
    *      with all messages, even if it is not specificially indicated as
    *      supported. All of the remaining fields in surface state are ignored
    *      for null surfaces, with the following exceptions:
    *
    *        * [DevSNB+]: Width, Height, Depth, and LOD fields must match the
    *          depth buffer's corresponding state for all render target
    *          surfaces, including null.
    *        * Surface Format must be R8G8B8A8_UNORM."
    *
    * From the Sandy Bridge PRM, volume 4 part 1, page 82:
    *
    *     "If Surface Type is SURFTYPE_NULL, this field (Tiled Surface) must be
    *      TRUE"
    */

   dw[0] = BRW_SURFACE_NULL << BRW_SURFACE_TYPE_SHIFT |
           BRW_SURFACEFORMAT_B8G8R8A8_UNORM << BRW_SURFACE_FORMAT_SHIFT;

   dw[1] = 0;

   dw[2] = (height - 1) << BRW_SURFACE_HEIGHT_SHIFT |
           (width  - 1) << BRW_SURFACE_WIDTH_SHIFT |
           lod << BRW_SURFACE_LOD_SHIFT;

   dw[3] = (depth - 1) << BRW_SURFACE_DEPTH_SHIFT |
           BRW_SURFACE_TILED;

   dw[4] = 0;
   dw[5] = 0;
}

static void
gen6_fill_SURFACE_STATE_BUFFER(uint32_t *dw, const struct i965_resource *res,
                               enum pipe_format format,
                               unsigned bo_size, unsigned bo_offset,
                               boolean for_render)
{
   int width, height, depth, pitch;
   int surface_format, num_entries;

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 76:
    *
    *     "For SURFTYPE_BUFFER render targets, this field (Surface Base
    *      Address) specifies the base address of first element of the surface.
    *      The surface is interpreted as a simple array of that single element
    *      type. The address must be naturally-aligned to the element size
    *      (e.g., a buffer containing R32G32B32A32_FLOAT elements must be
    *      16-byte aligned).
    *
    *      For SURFTYPE_BUFFER non-rendertarget surfaces, this field specifies
    *      the base address of the first element of the surface, computed in
    *      software by adding the surface base address to the byte offset of
    *      the element in the buffer."
    *
    * From the Sandy Bridge PRM, volume 4 part 1, page 77:
    *
    *     "For buffer surfaces, the number of entries in the buffer ranges
    *      from 1 to 2^27."
    *
    * From the Sandy Bridge PRM, volume 4 part 1, page 81:
    *
    *     "For surfaces of type SURFTYPE_BUFFER, this field (Surface Pitch)
    *     indicates the size of the structure."
    *
    * From the Sandy Bridge PRM, volume 4 part 1, page 82:
    *
    *     "If Surface Type is SURFTYPE_BUFFER, this field (Tiled Surface) must
    *     be FALSE (buffers are supported only in linear memory)"
    */

   surface_format = i965_translate_color_format(format);
   pitch = util_format_get_blocksize(format);
   num_entries = bo_size / pitch;

   if (for_render)
      assert(bo_offset % pitch == 0);
   assert(num_entries >= 1 && num_entries <= 1 << 27);
   assert(res->tiling == INTEL_TILING_NONE);

   pitch--;
   num_entries--;
   width  = (num_entries & 0x0000007f);
   height = (num_entries & 0x000fff80) >> 7;
   depth  = (num_entries & 0x07f00000) >> 20;

   dw[0] = BRW_SURFACE_BUFFER << BRW_SURFACE_TYPE_SHIFT |
           surface_format << BRW_SURFACE_FORMAT_SHIFT;
   if (for_render)
      dw[0] |= BRW_SURFACE_RC_READ_WRITE;

   dw[1] = bo_offset;

   dw[2] = height << BRW_SURFACE_HEIGHT_SHIFT |
           width << BRW_SURFACE_WIDTH_SHIFT;

   dw[3] = depth << BRW_SURFACE_DEPTH_SHIFT |
           pitch << BRW_SURFACE_PITCH_SHIFT;

   dw[4] = 0;
   dw[5] = 0;
}

static void
gen6_fill_SURFACE_STATE(uint32_t *dw, struct i965_resource *res,
                        enum pipe_format format,
                        unsigned first_level, unsigned num_levels,
                        unsigned first_layer, unsigned num_layers,
                        boolean for_render)
{
   int surface_type, surface_format;
   int width, height, depth, pitch, lod;
   unsigned level_offset, x_offset, y_offset;

   surface_type = i965_translate_texture(res->base.target);
   if (for_render)
      surface_format = i965_translate_render_format(format);
   else
      surface_format = i965_translate_texture_format(format);
   assert(surface_format >= 0);

   /* check the base level */
   width = res->base.width0 - 1;
   height = res->base.height0 - 1;
   if (res->base.target == PIPE_TEXTURE_3D)
      depth = res->base.depth0 - 1;
   else if (res->base.target == PIPE_TEXTURE_CUBE && !for_render)
      depth = num_layers / 6 - 1;
   else
      depth = num_layers - 1;
   pitch = res->bo_stride - 1;

   assert(width >= 0 && height >= 0 && depth >= 0 && pitch >= 0);
   switch (surface_type) {
   case BRW_SURFACE_1D:
      assert(width <= 8191 && height == 0 && depth <= 511);
      break;
   case BRW_SURFACE_2D:
      assert(width <= 8191 && height <= 8191 && depth <= 511);
      break;
   case BRW_SURFACE_3D:
      assert(width <= 2047 && height <= 2047 && depth <= 2047);
      break;
   case BRW_SURFACE_CUBE:
      assert(width <= 8191 && height <= 8191 && depth <= 83);
      assert(width == height);
      break;
   default:
      assert(!"unexpected surface type");
      break;
   }

   if (for_render) {
      /*
       * The hardware requires LOD to be the same for all render targets and
       * the depth buffer.  Thus, we compute the offset to the LOD manually
       * and always set LOD to zero.
       */
      lod = 0;

      width = u_minify(res->base.width0, first_level) - 1;
      height = u_minify(res->base.height0, first_level) - 1;
      if (surface_type == BRW_SURFACE_3D)
         depth = u_minify(res->base.depth0, first_level) - 1;
      else if (surface_type == BRW_SURFACE_CUBE)
         depth = 0;

      assert(num_layers == 1);
      level_offset = i965_resource_get_slice_offset(res,
            first_level, first_layer, TRUE, &x_offset, &y_offset);

      assert(x_offset % 4 == 0);
      assert(y_offset % 2 == 0);
      x_offset /= 4;
      y_offset /= 2;

      first_layer = 0;
   }
   else {
      lod = num_levels - 1;

      level_offset = 0;
      x_offset = 0;
      y_offset = 0;
   }

   dw[0] = surface_type << BRW_SURFACE_TYPE_SHIFT |
           surface_format << BRW_SURFACE_FORMAT_SHIFT |
           BRW_SURFACE_MIPMAPLAYOUT_BELOW << BRW_SURFACE_MIPLAYOUT_SHIFT;
   /*
    * i965 sets this flag for constant buffers and sol surfaces, instead of
    * render buffers.  Why?
    */
   if (for_render)
      dw[0] |= BRW_SURFACE_RC_READ_WRITE;
   if (surface_type == BRW_SURFACE_CUBE && !for_render)
      dw[0] |= BRW_SURFACE_CUBEFACE_ENABLES;

   dw[1] = level_offset;

   dw[2] = height << BRW_SURFACE_HEIGHT_SHIFT |
           width << BRW_SURFACE_WIDTH_SHIFT |
           lod << BRW_SURFACE_LOD_SHIFT;

   dw[3] = depth << BRW_SURFACE_DEPTH_SHIFT |
           pitch << BRW_SURFACE_PITCH_SHIFT |
           i965_translate_winsys_tiling(res->tiling);

   dw[4] = first_level << BRW_SURFACE_MIN_LOD_SHIFT |
           first_layer << 17 |
           depth << 8 |
           ((res->base.nr_samples > 1) ? BRW_SURFACE_MULTISAMPLECOUNT_4 :
                                         BRW_SURFACE_MULTISAMPLECOUNT_1);

   dw[5] = x_offset << BRW_SURFACE_X_OFFSET_SHIFT |
           y_offset << BRW_SURFACE_Y_OFFSET_SHIFT;
   if (res->valign_4)
      dw[5] |= BRW_SURFACE_VERTICAL_ALIGN_ENABLE;
}

static uint32_t
gen6_emit_SURFACE_STATE(const struct i965_gpe_gen6 *gpe,
                        struct i965_cp *cp,
                        const struct pipe_surface *surface,
                        const struct pipe_sampler_view *view,
                        const struct pipe_constant_buffer *cbuf,
                        const struct pipe_stream_output_target *so,
                        unsigned so_num_components)
{
   const int state_align = 32 / 4;
   const int state_len = 6;
   uint32_t state_offset;
   struct i965_resource *res;
   boolean for_render;
   uint32_t read_domains, write_domain;
   uint32_t dw[6];

   assert(gpe->gen == 6);

   /* XXX should we still stick to one command/state one function? */
   if (surface) {
      res = i965_resource(surface->texture);
      for_render = TRUE;

      if (res) {
         gen6_fill_SURFACE_STATE(dw, res, surface->format,
               surface->u.tex.level, 1,
               surface->u.tex.first_layer,
               surface->u.tex.last_layer - surface->u.tex.first_layer + 1,
               for_render);
      }
      else {
         gen6_fill_SURFACE_STATE_NULL(dw,
               surface->width, surface->height, 0, 0);
      }
   }
   else if (view) {
      res = i965_resource(view->texture);
      for_render = FALSE;

      gen6_fill_SURFACE_STATE(dw, res, view->format,
            view->u.tex.first_level,
            view->u.tex.last_level - view->u.tex.first_level + 1,
            view->u.tex.first_layer,
            view->u.tex.last_layer - view->u.tex.first_layer + 1,
            for_render);
   }
   else if (cbuf) {
      res = i965_resource(cbuf->buffer);
      for_render = FALSE;

      gen6_fill_SURFACE_STATE_BUFFER(dw, res,
            PIPE_FORMAT_R32G32B32A32_FLOAT,
            cbuf->buffer_size, cbuf->buffer_offset, for_render);
   }
   else {
      enum pipe_format format;

      assert(so);
      switch (so_num_components) {
      case 1:
         format = PIPE_FORMAT_R32_FLOAT;
         break;
      case 2:
         format = PIPE_FORMAT_R32G32_FLOAT;
         break;
      case 3:
         format = PIPE_FORMAT_R32G32B32_FLOAT;
         break;
      case 4:
         format = PIPE_FORMAT_R32G32B32A32_FLOAT;
         break;
      default:
         assert(!"unexpected SO components length");
         format = PIPE_FORMAT_R32_FLOAT;
         break;
      }

      res = i965_resource(so->buffer);
      for_render = TRUE;

      gen6_fill_SURFACE_STATE_BUFFER(dw, res, format,
            so->buffer_size, so->buffer_offset, for_render);
   }

   if (for_render) {
      read_domains = INTEL_DOMAIN_RENDER;
      write_domain = INTEL_DOMAIN_RENDER;
   }
   else {
      read_domains = INTEL_DOMAIN_SAMPLER;
      write_domain = 0;
   }

   assert(state_len + state_align - 1 <= GEN6_MAX_SURFACE_STATE);

   i965_cp_steal(cp, "SURFACE_STATE", state_len, state_align, &state_offset);
   i965_cp_write(cp, dw[0]);
   i965_cp_write_bo(cp, (res) ? res->bo : NULL,
                    read_domains, write_domain, dw[1]);
   i965_cp_write(cp, dw[2]);
   i965_cp_write(cp, dw[3]);
   i965_cp_write(cp, dw[4]);
   i965_cp_write(cp, dw[5]);
   i965_cp_end(cp);

   return state_offset;
}

static uint32_t
gen6_emit_SAMPLER_STATE(const struct i965_gpe_gen6 *gpe,
                        struct i965_cp *cp,
                        const struct pipe_sampler_state **samplers,
                        const struct pipe_sampler_view **sampler_views,
                        const uint32_t *sampler_border_colors,
                        int num_samplers)
{
   const int state_align = 32 / 4;
   const int state_len = 4 * num_samplers;
   uint32_t state_offset;
   int i;

   assert(gpe->gen == 6);

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 101:
    *
    *     "The sampler state is stored as an array of up to 16 elements..."
    */
   assert(num_samplers <= 16);

   if (!num_samplers)
      return 0;

   assert(state_len + state_align - 1 <= GEN6_MAX_SAMPLER_STATE);
   i965_cp_steal(cp, "SAMPLER_STATE", state_len, state_align, &state_offset);

   for (i = 0; i < num_samplers; i++) {
      const struct pipe_sampler_state *sampler = samplers[i];
      const struct pipe_sampler_view *view = sampler_views[i];
      const uint32_t border_color = sampler_border_colors[i];
      int mip_filter, min_filter, mag_filter, max_aniso;
      int lod_bias, max_lod, min_lod;
      int wrap_s, wrap_t, wrap_r;
      boolean clamp_to_edge;
      uint32_t dw0, dw1, dw2, dw3;

      /* there may be holes */
      if (!sampler) {
         i965_cp_write(cp, 0);
         i965_cp_write(cp, 0);
         i965_cp_write(cp, 0);
         i965_cp_write(cp, 0);
         continue;
      }

      if (sampler->max_anisotropy) {
         min_filter = BRW_MAPFILTER_ANISOTROPIC;
         mag_filter = BRW_MAPFILTER_ANISOTROPIC;
         mip_filter = i965_translate_tex_mipfilter(sampler->min_mip_filter);

         max_aniso = (MAX2(sampler->max_anisotropy, 2) - 2) / 2;
         if (max_aniso > BRW_ANISORATIO_16)
            max_aniso = BRW_ANISORATIO_16;
      }
      else {
         min_filter = i965_translate_tex_filter(sampler->min_img_filter);
         mag_filter = i965_translate_tex_filter(sampler->mag_img_filter);
         mip_filter = i965_translate_tex_mipfilter(sampler->min_mip_filter);

         max_aniso = 0;
      }

      if ((sampler->min_img_filter == PIPE_TEX_FILTER_NEAREST &&
           sampler->min_mip_filter == PIPE_TEX_MIPFILTER_NONE) ||
          (sampler->mag_img_filter == PIPE_TEX_FILTER_NEAREST &&
           !sampler->max_anisotropy))
         clamp_to_edge = TRUE;
      else
         clamp_to_edge = FALSE;

      switch (view->texture->target) {
      case PIPE_TEXTURE_CUBE:
         if (sampler->seamless_cube_map &&
             (sampler->min_img_filter != PIPE_TEX_FILTER_NEAREST ||
              sampler->mag_img_filter != PIPE_TEX_FILTER_NEAREST)) {
            wrap_s = BRW_TEXCOORDMODE_CUBE;
            wrap_t = BRW_TEXCOORDMODE_CUBE;
            wrap_r = BRW_TEXCOORDMODE_CUBE;
         }
         else {
            wrap_s = BRW_TEXCOORDMODE_CLAMP;
            wrap_t = BRW_TEXCOORDMODE_CLAMP;
            wrap_r = BRW_TEXCOORDMODE_CLAMP;
         }
         break;
      case PIPE_TEXTURE_1D:
         wrap_s = i965_translate_tex_wrap(sampler->wrap_s, clamp_to_edge);
         wrap_t = BRW_TEXCOORDMODE_WRAP;
         wrap_r = BRW_TEXCOORDMODE_WRAP;
         break;
      default:
         wrap_s = i965_translate_tex_wrap(sampler->wrap_s, clamp_to_edge);
         wrap_t = i965_translate_tex_wrap(sampler->wrap_t, clamp_to_edge);
         wrap_r = i965_translate_tex_wrap(sampler->wrap_r, clamp_to_edge);
         break;
      }

      if (sampler->min_mip_filter != PIPE_TEX_MIPFILTER_NONE) {
         /* in S4.6 */
         lod_bias = (int) (CLAMP(sampler->lod_bias, -16.0f, 15.0f) * 64.0f);
         lod_bias &= 0x7ff;

         /* in U4.6 */
         max_lod = (int) (CLAMP(sampler->max_lod, 0.0f, 13.0f) * 64.0f);
         min_lod = (int) (CLAMP(sampler->min_lod, 0.0f, 13.0f) * 64.0f);
      }
      else {
         /* always use the base level */
         lod_bias = 0;
         max_lod = 0;
         min_lod = 0;
      }

      dw0 = 1 << 28 |
            mip_filter << 20 |
            mag_filter << 17 |
            min_filter << 14 |
            lod_bias << 3;
      if (min_filter != mag_filter)
         dw0 |= 1 << 27;
      if (sampler->compare_mode != PIPE_TEX_COMPARE_NONE)
         dw0 |= i965_translate_shadow_func(sampler->compare_func);

      dw1 = min_lod << 22 |
            max_lod << 12 |
            wrap_s << 6 |
            wrap_t << 3 |
            wrap_r;

      /* the PRM says it is reserved, but i965 sets it */
      assert(!(border_color & 0x1f));
      dw2 = border_color;

      dw3 = max_aniso << 19;

      if (!sampler->normalized_coords)
         dw3 |= 1;

      if (min_filter != BRW_MAPFILTER_NEAREST) {
         dw3 |= (BRW_ADDRESS_ROUNDING_ENABLE_U_MIN |
                 BRW_ADDRESS_ROUNDING_ENABLE_V_MIN |
                 BRW_ADDRESS_ROUNDING_ENABLE_R_MIN) << 13;
      }
      if (mag_filter != BRW_MAPFILTER_NEAREST) {
         dw3 |= (BRW_ADDRESS_ROUNDING_ENABLE_U_MAG |
                 BRW_ADDRESS_ROUNDING_ENABLE_V_MAG |
                 BRW_ADDRESS_ROUNDING_ENABLE_R_MAG) << 13;
      }

      i965_cp_write(cp, dw0);
      i965_cp_write(cp, dw1);
      i965_cp_write(cp, dw2);
      i965_cp_write(cp, dw3);
   }

   i965_cp_end(cp);

   return state_offset;
}

static uint32_t
gen6_emit_SAMPLER_BORDER_COLOR_STATE(const struct i965_gpe_gen6 *gpe,
                                     struct i965_cp *cp,
                                     const union pipe_color_union *color)
{
   const int state_align = 32 / 4;
   const int state_len = 12;
   uint32_t state_offset;
   float rgba[4] = {
      color->f[0], color->f[1], color->f[2], color->f[3],
   };
   uint32_t dw[12];

   assert(gpe->gen == 6);

   dw[0] = float_to_ubyte(rgba[0]) |
           float_to_ubyte(rgba[1]) << 8 |
           float_to_ubyte(rgba[2]) << 16 |
           float_to_ubyte(rgba[3]) << 24;

   dw[1] = fui(rgba[0]);
   dw[2] = fui(rgba[1]);
   dw[3] = fui(rgba[2]);
   dw[4] = fui(rgba[3]);

   dw[5] = util_float_to_half(rgba[0]) |
           util_float_to_half(rgba[1]) << 16;
   dw[6] = util_float_to_half(rgba[2]) |
           util_float_to_half(rgba[3]) << 16;

   dw[7] = util_iround(CLAMP(rgba[0], 0.0f, 1.0f) * 65535.0f) |
           util_iround(CLAMP(rgba[1], 0.0f, 1.0f) * 65535.0f) << 16;
   dw[8] = util_iround(CLAMP(rgba[2], 0.0f, 1.0f) * 65535.0f) |
           util_iround(CLAMP(rgba[3], 0.0f, 1.0f) * 65535.0f) << 16;

   rgba[0] = CLAMP(rgba[0], -1.0f, 1.0f);
   rgba[1] = CLAMP(rgba[1], -1.0f, 1.0f);
   rgba[2] = CLAMP(rgba[2], -1.0f, 1.0f);
   rgba[3] = CLAMP(rgba[3], -1.0f, 1.0f);

   /* -1.0f should be -32768, no? */
   dw[9] = util_iround(rgba[0] * 32767.0f) |
           util_iround(rgba[1] * 32767.0f) << 16;
   dw[10] = util_iround(rgba[2] * 32767.0f) |
            util_iround(rgba[3] * 32767.0f) << 16;

   dw[11] = util_iround(rgba[0] * 127.0f) |
            util_iround(rgba[1] * 127.0f) << 8 |
            util_iround(rgba[2] * 127.0f) << 16 |
            util_iround(rgba[3] * 127.0f) << 24;

   assert(state_len + state_align - 1 <=
         GEN6_MAX_SAMPLER_BORDER_COLOR_STATE);

   i965_cp_steal(cp, "SAMPLER_BORDER_COLOR_STATE",
         state_len, state_align, &state_offset);
   i965_cp_write_multi(cp, dw, 12);
   i965_cp_end(cp);

   return state_offset;
}

static int
gen6_emit_max(const struct i965_gpe_gen6 *gpe, int state, int array_size)
{
   int size, extra = 0;

   if (!array_size)
      array_size = 1;

   switch (state) {
   case I965_GPE_GEN6_PIPELINE_SELECT:
      size = GEN6_SIZE_PIPELINE_SELECT;
      break;
   case I965_GPE_GEN6_STATE_BASE_ADDRESS:
      size = GEN6_SIZE_STATE_BASE_ADDRESS;
      break;
   case I965_GPE_GEN6_STATE_SIP:
      size = GEN6_SIZE_STATE_SIP;
      break;
   case I965_GPE_GEN6_3DSTATE_CC_STATE_POINTERS:
      size = GEN6_SIZE_3DSTATE_CC_STATE_POINTERS;
      break;
   case I965_GPE_GEN6_3DSTATE_BINDING_TABLE_POINTERS:
      size = GEN6_SIZE_3DSTATE_BINDING_TABLE_POINTERS;
      break;
   case I965_GPE_GEN6_3DSTATE_SAMPLER_STATE_POINTERS:
      size = GEN6_SIZE_3DSTATE_SAMPLER_STATE_POINTERS;
      break;
   case I965_GPE_GEN6_3DSTATE_VIEWPORT_STATE_POINTERS:
      size = GEN6_SIZE_3DSTATE_VIEWPORT_STATE_POINTERS;
      break;
   case I965_GPE_GEN6_3DSTATE_SCISSOR_STATE_POINTERS:
      size = GEN6_SIZE_3DSTATE_SCISSOR_STATE_POINTERS;
      break;
   case I965_GPE_GEN6_3DSTATE_URB:
      size = GEN6_SIZE_3DSTATE_URB;
      break;
   case I965_GPE_GEN6_PIPE_CONTROL:
      size = 5;
      assert(size == GEN6_MAX_PIPE_CONTROL);
      break;
   case I965_GPE_GEN6_3DSTATE_INDEX_BUFFER:
      size = GEN6_SIZE_3DSTATE_INDEX_BUFFER;
      break;
   case I965_GPE_GEN6_3DSTATE_VERTEX_BUFFERS:
      size = 4;
      extra = 1;
      assert(size * 33 + extra == GEN6_MAX_3DSTATE_VERTEX_BUFFERS);
      break;
   case I965_GPE_GEN6_3DSTATE_VERTEX_ELEMENTS:
      size = 2;
      extra = 1;
      assert(size * 34 + extra == GEN6_MAX_3DSTATE_VERTEX_ELEMENTS);
      break;
   case I965_GPE_GEN6_3DPRIMITIVE:
      size = GEN6_SIZE_3DPRIMITIVE;
      break;
   case I965_GPE_GEN6_3DSTATE_VF_STATISTICS:
      size = GEN6_SIZE_3DSTATE_VF_STATISTICS;
      break;
   case I965_GPE_GEN6_3DSTATE_VS:
      size = GEN6_SIZE_3DSTATE_VS;
      break;
   case I965_GPE_GEN6_3DSTATE_CONSTANT_VS:
      size = GEN6_SIZE_3DSTATE_CONSTANT_VS;
      break;
   case I965_GPE_GEN6_3DSTATE_GS_SVB_INDEX:
      size = GEN6_SIZE_3DSTATE_GS_SVB_INDEX;
      break;
   case I965_GPE_GEN6_3DSTATE_GS:
      size = GEN6_SIZE_3DSTATE_GS;
      break;
   case I965_GPE_GEN6_3DSTATE_CONSTANT_GS:
      size = GEN6_SIZE_3DSTATE_CONSTANT_GS;
      break;
   case I965_GPE_GEN6_3DSTATE_CLIP:
      size = GEN6_SIZE_3DSTATE_CLIP;
      break;
   case I965_GPE_GEN6_CLIP_VIEWPORT:
      size = 4;
      extra = 7;
      assert(size * 16 + extra == GEN6_MAX_CLIP_VIEWPORT);
      break;
   case I965_GPE_GEN6_3DSTATE_DRAWING_RECTANGLE:
      size = GEN6_SIZE_3DSTATE_DRAWING_RECTANGLE;
      break;
   case I965_GPE_GEN6_3DSTATE_SF:
      size = GEN6_SIZE_3DSTATE_SF;
      break;
   case I965_GPE_GEN6_SF_VIEWPORT:
      size = 8;
      extra = 7;
      assert(size * 16 + extra == GEN6_MAX_SF_VIEWPORT);
      break;
   case I965_GPE_GEN6_SCISSOR_RECT:
      size = 2;
      extra = 7;
      assert(size * 16 + extra == GEN6_MAX_SCISSOR_RECT);
      break;
   case I965_GPE_GEN6_3DSTATE_WM:
      size = GEN6_SIZE_3DSTATE_WM;
      break;
   case I965_GPE_GEN6_3DSTATE_CONSTANT_PS:
      size = GEN6_SIZE_3DSTATE_CONSTANT_PS;
      break;
   case I965_GPE_GEN6_3DSTATE_SAMPLE_MASK:
      size = GEN6_SIZE_3DSTATE_SAMPLE_MASK;
      break;
   case I965_GPE_GEN6_3DSTATE_AA_LINE_PARAMETERS:
      size = GEN6_SIZE_3DSTATE_AA_LINE_PARAMETERS;
      break;
   case I965_GPE_GEN6_3DSTATE_LINE_STIPPLE:
      size = GEN6_SIZE_3DSTATE_LINE_STIPPLE;
      break;
   case I965_GPE_GEN6_3DSTATE_POLY_STIPPLE_OFFSET:
      size = GEN6_SIZE_3DSTATE_POLY_STIPPLE_OFFSET;
      break;
   case I965_GPE_GEN6_3DSTATE_POLY_STIPPLE_PATTERN:
      size = GEN6_SIZE_3DSTATE_POLY_STIPPLE_PATTERN;
      break;
   case I965_GPE_GEN6_3DSTATE_MULTISAMPLE:
      size = GEN6_SIZE_3DSTATE_MULTISAMPLE;
      break;
   case I965_GPE_GEN6_3DSTATE_DEPTH_BUFFER:
      size = GEN6_SIZE_3DSTATE_DEPTH_BUFFER;
      break;
   case I965_GPE_GEN6_3DSTATE_STENCIL_BUFFER:
      size = GEN6_SIZE_3DSTATE_STENCIL_BUFFER;
      break;
   case I965_GPE_GEN6_3DSTATE_HIER_DEPTH_BUFFER:
      size = GEN6_SIZE_3DSTATE_HIER_DEPTH_BUFFER;
      break;
   case I965_GPE_GEN6_3DSTATE_CLEAR_PARAMS:
      size = GEN6_SIZE_3DSTATE_CLEAR_PARAMS;
      break;
   case I965_GPE_GEN6_COLOR_CALC_STATE:
      size = 6;
      extra = 15;
      assert(size * 1 + extra == GEN6_MAX_COLOR_CALC_STATE);
      break;
   case I965_GPE_GEN6_DEPTH_STENCIL_STATE:
      size = 3;
      extra = 15;
      assert(size * 1 + extra == GEN6_MAX_DEPTH_STENCIL_STATE);
      break;
   case I965_GPE_GEN6_BLEND_STATE:
      size = 2;
      extra = 15;
      assert(size * 8 + extra == GEN6_MAX_BLEND_STATE);
      break;
   case I965_GPE_GEN6_CC_VIEWPORT:
      size = 2;
      extra = 7;
      assert(size * 16 + extra == GEN6_MAX_CC_VIEWPORT);
      break;
   case I965_GPE_GEN6_BINDING_TABLE_STATE:
      size = 1;
      extra = 7;
      assert(size * 256 + extra == GEN6_MAX_BINDING_TABLE_STATE);
      break;
   case I965_GPE_GEN6_SURFACE_STATE:
      size = 6;
      extra = 7;
      assert(size * 1 + extra == GEN6_MAX_SURFACE_STATE);
      /* every SURFACE_STATE must be aligned */
      if (array_size > 1)
         size = align(size, 8);
      break;
   case I965_GPE_GEN6_SAMPLER_STATE:
      size = 4;
      extra = 7;
      assert(size * 16 + extra == GEN6_MAX_SAMPLER_STATE);
      break;
   case I965_GPE_GEN6_SAMPLER_BORDER_COLOR_STATE:
      size = 12;
      extra = 7;
      assert(size * 1 + extra == GEN6_MAX_SAMPLER_BORDER_COLOR_STATE);
      /* every SAMPLER_BORDER_COLOR_STATE must be aligned */
      if (array_size > 1)
         size = align(size, 8);
      break;
   }

   return size * array_size + extra;
}

static const struct i965_gpe_gen6 gen6_gpe = {
   .gen = 6,

   .emit_max = gen6_emit_max,

   /* GPE */
   .emit_PIPELINE_SELECT = gen6_emit_PIPELINE_SELECT,
   .emit_STATE_BASE_ADDRESS = gen6_emit_STATE_BASE_ADDRESS,
   .emit_STATE_SIP = gen6_emit_STATE_SIP,

   /* 3D */
   .emit_3DSTATE_CC_STATE_POINTERS = gen6_emit_3DSTATE_CC_STATE_POINTERS,
   .emit_3DSTATE_BINDING_TABLE_POINTERS = gen6_emit_3DSTATE_BINDING_TABLE_POINTERS,
   .emit_3DSTATE_SAMPLER_STATE_POINTERS = gen6_emit_3DSTATE_SAMPLER_STATE_POINTERS,
   .emit_3DSTATE_VIEWPORT_STATE_POINTERS = gen6_emit_3DSTATE_VIEWPORT_STATE_POINTERS,
   .emit_3DSTATE_SCISSOR_STATE_POINTERS = gen6_emit_3DSTATE_SCISSOR_STATE_POINTERS,
   .emit_3DSTATE_URB = gen6_emit_3DSTATE_URB,
   .emit_PIPE_CONTROL = gen6_emit_PIPE_CONTROL,

   /* VF */
   .emit_3DSTATE_INDEX_BUFFER = gen6_emit_3DSTATE_INDEX_BUFFER,
   .emit_3DSTATE_VERTEX_BUFFERS = gen6_emit_3DSTATE_VERTEX_BUFFERS,
   .emit_3DSTATE_VERTEX_ELEMENTS = gen6_emit_3DSTATE_VERTEX_ELEMENTS,
   .emit_3DPRIMITIVE = gen6_emit_3DPRIMITIVE,
   .emit_3DSTATE_VF_STATISTICS = gen6_emit_3DSTATE_VF_STATISTICS,

   /* VS */
   .emit_3DSTATE_VS = gen6_emit_3DSTATE_VS,
   .emit_3DSTATE_CONSTANT_VS = gen6_emit_3DSTATE_CONSTANT_VS,

   /* GS */
   .emit_3DSTATE_GS_SVB_INDEX = gen6_emit_3DSTATE_GS_SVB_INDEX,
   .emit_3DSTATE_GS = gen6_emit_3DSTATE_GS,
   .emit_3DSTATE_CONSTANT_GS = gen6_emit_3DSTATE_CONSTANT_GS,

   /* CLIP */
   .emit_3DSTATE_CLIP = gen6_emit_3DSTATE_CLIP,
   .emit_CLIP_VIEWPORT = gen6_emit_CLIP_VIEWPORT,

   /* SF */
   .emit_3DSTATE_DRAWING_RECTANGLE = gen6_emit_3DSTATE_DRAWING_RECTANGLE,
   .emit_3DSTATE_SF = gen6_emit_3DSTATE_SF,
   .emit_SF_VIEWPORT = gen6_emit_SF_VIEWPORT,
   .emit_SCISSOR_RECT = gen6_emit_SCISSOR_RECT,

   /* WM */
   .emit_3DSTATE_WM = gen6_emit_3DSTATE_WM,
   .emit_3DSTATE_CONSTANT_PS = gen6_emit_3DSTATE_CONSTANT_PS,
   .emit_3DSTATE_SAMPLE_MASK = gen6_emit_3DSTATE_SAMPLE_MASK,
   .emit_3DSTATE_AA_LINE_PARAMETERS = gen6_emit_3DSTATE_AA_LINE_PARAMETERS,
   .emit_3DSTATE_LINE_STIPPLE = gen6_emit_3DSTATE_LINE_STIPPLE,
   .emit_3DSTATE_POLY_STIPPLE_OFFSET = gen6_emit_3DSTATE_POLY_STIPPLE_OFFSET,
   .emit_3DSTATE_POLY_STIPPLE_PATTERN = gen6_emit_3DSTATE_POLY_STIPPLE_PATTERN,
   .emit_3DSTATE_MULTISAMPLE = gen6_emit_3DSTATE_MULTISAMPLE,
   .emit_3DSTATE_DEPTH_BUFFER = gen6_emit_3DSTATE_DEPTH_BUFFER,
   .emit_3DSTATE_STENCIL_BUFFER = gen6_emit_3DSTATE_STENCIL_BUFFER,
   .emit_3DSTATE_HIER_DEPTH_BUFFER = gen6_emit_3DSTATE_HIER_DEPTH_BUFFER,
   .emit_3DSTATE_CLEAR_PARAMS = gen6_emit_3DSTATE_CLEAR_PARAMS,

   /* CC */
   .emit_COLOR_CALC_STATE = gen6_emit_COLOR_CALC_STATE,
   .emit_DEPTH_STENCIL_STATE = gen6_emit_DEPTH_STENCIL_STATE,
   .emit_BLEND_STATE = gen6_emit_BLEND_STATE,
   .emit_CC_VIEWPORT = gen6_emit_CC_VIEWPORT,

   /* subsystem */
   .emit_BINDING_TABLE_STATE = gen6_emit_BINDING_TABLE_STATE,
   .emit_SURFACE_STATE = gen6_emit_SURFACE_STATE,
   .emit_SAMPLER_STATE = gen6_emit_SAMPLER_STATE,
   .emit_SAMPLER_BORDER_COLOR_STATE = gen6_emit_SAMPLER_BORDER_COLOR_STATE,
};

const struct i965_gpe_gen6 *
i965_gpe_gen6_get(void)
{
   return &gen6_gpe;
}
