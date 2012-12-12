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

#include "intel_reg.h"

#include "util/u_clear.h"
#include "util/u_surface.h"
#include "util/u_pack_color.h"
#include "util/u_blitter.h"
#include "util/u_surface.h"
#include "i965_common.h"
#include "i965_screen.h"
#include "i965_context.h"
#include "i965_cp.h"
#include "i965_blit.h"
#include "i965_resource.h"

static boolean
blitter_xy_color_blt(struct pipe_context *pipe,
                     struct pipe_resource *r,
                     int16_t x1, int16_t y1,
                     int16_t x2, int16_t y2,
                     uint32_t color)
{
   struct i965_context *i965 = i965_context(pipe);
   struct i965_resource *res = i965_resource(r);
   uint32_t cmd, br13;
   int cpp, stride;
   struct intel_bo *bo_check[2];

   /* nothing to clear */
   if (x1 >= x2 || y1 >= y2)
      return TRUE;

   /* how to support Y-tiling? */
   if (res->tiling == INTEL_TILING_Y)
      return FALSE;

   cmd = XY_COLOR_BLT_CMD;
   br13 = 0xf0 << 16;

   cpp = util_format_get_blocksize(res->base.format);
   switch (cpp) {
   case 4:
      cmd |= XY_BLT_WRITE_ALPHA | XY_BLT_WRITE_RGB;
      br13 |= BR13_8888;
      break;
   case 2:
      br13 |= BR13_565;
      break;
   case 1:
      br13 |= BR13_8;
      break;
   default:
      return FALSE;
      break;
   }

   stride = res->bo_stride;
   if (res->tiling != INTEL_TILING_NONE) {
      assert(res->tiling == INTEL_TILING_X);

      cmd |= XY_DST_TILED;
      /* in dwords */
      stride /= 4;
   }

   /* make room if necessary */
   bo_check[0] = i965->cp->bo;
   bo_check[1] = res->bo;
   if (i965->winsys->check_aperture_space(i965->winsys, bo_check, 2))
      i965_cp_flush(i965->cp);

   i965_cp_set_ring(i965->cp, INTEL_RING_BLT);

   i965_cp_begin(i965->cp, 6);
   i965_cp_write(i965->cp, cmd);
   i965_cp_write(i965->cp, br13 | stride);
   i965_cp_write(i965->cp, (y1 << 16) | x1);
   i965_cp_write(i965->cp, (y2 << 16) | x2);
   i965_cp_write_bo(i965->cp, res->bo,
                    INTEL_DOMAIN_RENDER,
                    INTEL_DOMAIN_RENDER,
                    0);
   i965_cp_write(i965->cp, color);
   i965_cp_end(i965->cp);

   return TRUE;
}

enum i965_blitter_op {
   I965_BLITTER_CLEAR,
   I965_BLITTER_CLEAR_SURFACE,
   I965_BLITTER_BLIT,
};

static void
i965_blitter_begin(struct i965_context *i965, enum i965_blitter_op op)
{
   /* as documented in util/u_blitter.h */
   util_blitter_save_vertex_buffer_slot(i965->blitter,
         i965->vertex_buffers.buffers);
   util_blitter_save_vertex_elements(i965->blitter, i965->vertex_elements);
   util_blitter_save_vertex_shader(i965->blitter, i965->vs);
   util_blitter_save_geometry_shader(i965->blitter, i965->gs);
   util_blitter_save_so_targets(i965->blitter,
         i965->stream_output_targets.num_targets,
         i965->stream_output_targets.targets);

   util_blitter_save_fragment_shader(i965->blitter, i965->fs);
   util_blitter_save_depth_stencil_alpha(i965->blitter,
         i965->depth_stencil_alpha);
   util_blitter_save_blend(i965->blitter, i965->blend);

   /* undocumented? */
   util_blitter_save_viewport(i965->blitter, &i965->viewport);
   util_blitter_save_stencil_ref(i965->blitter, &i965->stencil_ref);
   util_blitter_save_sample_mask(i965->blitter, i965->sample_mask);

   switch (op) {
   case I965_BLITTER_CLEAR:
      util_blitter_save_rasterizer(i965->blitter, i965->rasterizer);
      break;
   case I965_BLITTER_CLEAR_SURFACE:
      util_blitter_save_framebuffer(i965->blitter, &i965->framebuffer);
      break;
   case I965_BLITTER_BLIT:
      util_blitter_save_rasterizer(i965->blitter, i965->rasterizer);
      util_blitter_save_framebuffer(i965->blitter, &i965->framebuffer);

      util_blitter_save_fragment_sampler_states(i965->blitter,
            i965->samplers[PIPE_SHADER_FRAGMENT].num_samplers,
            (void **) i965->samplers[PIPE_SHADER_FRAGMENT].samplers);

      util_blitter_save_fragment_sampler_views(i965->blitter,
            i965->sampler_views[PIPE_SHADER_FRAGMENT].num_views,
            i965->sampler_views[PIPE_SHADER_FRAGMENT].views);

      /* disable render condition? */
      break;
   default:
      break;
   }
}

static void
i965_blitter_end(struct i965_context *i965)
{
}

static void
i965_clear(struct pipe_context *pipe,
           unsigned buffers,
           const union pipe_color_union *color,
           double depth,
           unsigned stencil)
{
   struct i965_context *i965 = i965_context(pipe);

   /* TODO we should pause/resume some queries */
   i965_blitter_begin(i965, I965_BLITTER_CLEAR);

   util_blitter_clear(i965->blitter,
         i965->framebuffer.width, i965->framebuffer.height,
         i965->framebuffer.nr_cbufs, buffers,
         (i965->framebuffer.nr_cbufs) ? i965->framebuffer.cbufs[0]->format :
                                        PIPE_FORMAT_NONE,
         color, depth, stencil);

   i965_blitter_end(i965);
}

static void
i965_clear_render_target(struct pipe_context *pipe,
                         struct pipe_surface *dst,
                         const union pipe_color_union *color,
                         unsigned dstx, unsigned dsty,
                         unsigned width, unsigned height)
{
   struct i965_context *i965 = i965_context(pipe);
   union util_color packed;

   if (!width || !height || dstx >= dst->width || dsty >= dst->height)
      return;

   if (dstx + width > dst->width)
      width = dst->width - dstx;
   if (dsty + height > dst->height)
      height = dst->height - dsty;

   util_pack_color(color->f, dst->format, &packed);

   /* try HW blit first */
   if (blitter_xy_color_blt(pipe, dst->texture,
                            dstx, dsty,
                            dstx + width, dsty + height,
                            packed.ui))
      return;

   i965_blitter_begin(i965, I965_BLITTER_CLEAR_SURFACE);
   util_blitter_clear_render_target(i965->blitter,
         dst, color, dstx, dsty, width, height);
   i965_blitter_end(i965);
}

static void
i965_clear_depth_stencil(struct pipe_context *pipe,
                         struct pipe_surface *dst,
                         unsigned clear_flags,
                         double depth,
                         unsigned stencil,
                         unsigned dstx, unsigned dsty,
                         unsigned width, unsigned height)
{
   struct i965_context *i965 = i965_context(pipe);

   /*
    * The PRM claims that HW blit supports Y-tiling since GEN6, but it does
    * not tell us how to program it.  Since depth buffers are always Y-tiled,
    * HW blit will not work.
    */
   i965_blitter_begin(i965, I965_BLITTER_CLEAR_SURFACE);
   util_blitter_clear_depth_stencil(i965->blitter,
         dst, clear_flags, depth, stencil, dstx, dsty, width, height);
   i965_blitter_end(i965);
}

static void
i965_blit(struct pipe_context *pipe, const struct pipe_blit_info *info)
{
   struct i965_context *i965 = i965_context(pipe);

   if (util_try_blit_via_copy_region(pipe, info))
      return;

   if (util_blitter_is_blit_supported(i965->blitter, info)) {
      i965_blitter_begin(i965, I965_BLITTER_BLIT);
      util_blitter_blit(i965->blitter, info);
      i965_blitter_end(i965);
   }
}

/**
 * Initialize blit-related functions.
 */
void
i965_init_blit_functions(struct i965_context *i965)
{
   i965->base.resource_copy_region = util_resource_copy_region;
   i965->base.blit = i965_blit;

   i965->base.clear = i965_clear;
   i965->base.clear_render_target = i965_clear_render_target;
   i965->base.clear_depth_stencil = i965_clear_depth_stencil;
}
