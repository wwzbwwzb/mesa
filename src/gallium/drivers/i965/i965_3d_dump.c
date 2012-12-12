/*
 * Copyright © 2007 Intel Corporation
 * Copyright (C) 2012 LunarG Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Chia-I Wu <olv@lunarg.com>
 */

typedef short GLshort;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned int GLuint;
typedef float GLfloat;
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include "brw_structs.h"
#include "brw_defines.h"

#include "util/u_debug.h"
#include "intel_winsys.h"
#include "i965_cp.h"
#include "i965_3d_gen6.h"
#include "i965_3d.h"

#define brw_context i965_3d

static void
batch_out(struct brw_context *brw, const char *name, uint32_t offset,
	  int index, char *fmt, ...) _util_printf_format(5, 6);

static void
batch_out(struct brw_context *brw, const char *name, uint32_t offset,
	  int index, char *fmt, ...)
{
   uint32_t *data = brw->cp->bo->get_virtual(brw->cp->bo) + offset;
   va_list va;

   fprintf(stderr, "0x%08x:      0x%08x: %8s: ",
	   offset + index * 4, data[index], name);
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}

static const char *
get_965_surfacetype(unsigned int surfacetype)
{
    switch (surfacetype) {
    case 0: return "1D";
    case 1: return "2D";
    case 2: return "3D";
    case 3: return "CUBE";
    case 4: return "BUFFER";
    case 7: return "NULL";
    default: return "unknown";
    }
}

static const char *
get_965_surface_format(unsigned int surface_format)
{
    switch (surface_format) {
    case 0x000: return "r32g32b32a32_float";
    case 0x0c1: return "b8g8r8a8_unorm";
    case 0x100: return "b5g6r5_unorm";
    case 0x102: return "b5g5r5a1_unorm";
    case 0x104: return "b4g4r4a4_unorm";
    default: return "unknown";
    }
}

static void dump_surface_state(struct brw_context *brw, uint32_t offset)
{
   const char *name = "SURF";
   uint32_t *surf = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   batch_out(brw, name, offset, 0, "%s %s\n",
	     get_965_surfacetype(GET_FIELD(surf[0], BRW_SURFACE_TYPE)),
	     get_965_surface_format(GET_FIELD(surf[0], BRW_SURFACE_FORMAT)));
   batch_out(brw, name, offset, 1, "offset\n");
   batch_out(brw, name, offset, 2, "%dx%d size, %d mips\n",
	     GET_FIELD(surf[2], BRW_SURFACE_WIDTH) + 1,
	     GET_FIELD(surf[2], BRW_SURFACE_HEIGHT) + 1,
	     GET_FIELD(surf[2], BRW_SURFACE_LOD));
   batch_out(brw, name, offset, 3, "pitch %d, %s tiled\n",
	     GET_FIELD(surf[3], BRW_SURFACE_PITCH) + 1,
	     (surf[3] & BRW_SURFACE_TILED) ?
	     ((surf[3] & BRW_SURFACE_TILED_Y) ? "Y" : "X") : "not");
   batch_out(brw, name, offset, 4, "mip base %d\n",
	     GET_FIELD(surf[4], BRW_SURFACE_MIN_LOD));
   batch_out(brw, name, offset, 5, "x,y offset: %d,%d\n",
	     GET_FIELD(surf[5], BRW_SURFACE_X_OFFSET),
	     GET_FIELD(surf[5], BRW_SURFACE_Y_OFFSET));
}

static void dump_gen7_surface_state(struct brw_context *brw, uint32_t offset)
{
   const char *name = "SURF";
   struct gen7_surface_state *surf = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   batch_out(brw, name, offset, 0, "%s %s\n",
	     get_965_surfacetype(surf->ss0.surface_type),
	     get_965_surface_format(surf->ss0.surface_format));
   batch_out(brw, name, offset, 1, "offset\n");
   batch_out(brw, name, offset, 2, "%dx%d size, %d mips\n",
	     surf->ss2.width + 1, surf->ss2.height + 1, surf->ss5.mip_count);
   batch_out(brw, name, offset, 3, "pitch %d, %stiled\n",
	     surf->ss3.pitch + 1, surf->ss0.tiled_surface ? "" : "not ");
   batch_out(brw, name, offset, 4, "mip base %d\n",
	     surf->ss5.min_lod);
   batch_out(brw, name, offset, 5, "x,y offset: %d,%d\n",
	     surf->ss5.x_offset, surf->ss5.y_offset);
}

static void
dump_sdc(struct brw_context *brw, uint32_t offset)
{
   const char *name = "SDC";

   if (brw->gen >= 5 && brw->gen <= 6) {
      struct gen5_sampler_default_color *sdc = (brw->cp->bo->get_virtual(brw->cp->bo) +
						offset);
      batch_out(brw, name, offset, 0, "unorm rgba\n");
      batch_out(brw, name, offset, 1, "r %f\n", sdc->f[0]);
      batch_out(brw, name, offset, 2, "b %f\n", sdc->f[1]);
      batch_out(brw, name, offset, 3, "g %f\n", sdc->f[2]);
      batch_out(brw, name, offset, 4, "a %f\n", sdc->f[3]);
      batch_out(brw, name, offset, 5, "half float rg\n");
      batch_out(brw, name, offset, 6, "half float ba\n");
      batch_out(brw, name, offset, 7, "u16 rg\n");
      batch_out(brw, name, offset, 8, "u16 ba\n");
      batch_out(brw, name, offset, 9, "s16 rg\n");
      batch_out(brw, name, offset, 10, "s16 ba\n");
      batch_out(brw, name, offset, 11, "s8 rgba\n");
   } else {
      struct brw_sampler_default_color *sdc = (brw->cp->bo->get_virtual(brw->cp->bo) +
					       offset);
      batch_out(brw, name, offset, 0, "r %f\n", sdc->color[0]);
      batch_out(brw, name, offset, 1, "g %f\n", sdc->color[1]);
      batch_out(brw, name, offset, 2, "b %f\n", sdc->color[2]);
      batch_out(brw, name, offset, 3, "a %f\n", sdc->color[3]);
   }
}

static void dump_sampler_state(struct brw_context *brw,
			       uint32_t offset, uint32_t size)
{
   int i;
   struct brw_sampler_state *samp = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   assert(brw->gen < 7);

   for (i = 0; i < size / sizeof(*samp); i++) {
      char name[20];

      sprintf(name, "WM SAMP%d", i);
      batch_out(brw, name, offset, 0, "filtering\n");
      batch_out(brw, name, offset, 1, "wrapping, lod\n");
      batch_out(brw, name, offset, 2, "default color pointer\n");
      batch_out(brw, name, offset, 3, "chroma key, aniso\n");

      samp++;
      offset += sizeof(*samp);
   }
}

static void dump_gen7_sampler_state(struct brw_context *brw,
				    uint32_t offset, uint32_t size)
{
   struct gen7_sampler_state *samp = brw->cp->bo->get_virtual(brw->cp->bo) + offset;
   int i;

   assert(brw->gen >= 7);

   for (i = 0; i < size / sizeof(*samp); i++) {
      char name[20];

      sprintf(name, "WM SAMP%d", i);
      batch_out(brw, name, offset, 0, "filtering\n");
      batch_out(brw, name, offset, 1, "wrapping, lod\n");
      batch_out(brw, name, offset, 2, "default color pointer\n");
      batch_out(brw, name, offset, 3, "chroma key, aniso\n");

      samp++;
      offset += sizeof(*samp);
   }
}


static void dump_sf_viewport_state(struct brw_context *brw,
				   uint32_t offset)
{
   const char *name = "SF VP";
   struct brw_sf_viewport *vp = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   assert(brw->gen < 7);

   batch_out(brw, name, offset, 0, "m00 = %f\n", vp->viewport.m00);
   batch_out(brw, name, offset, 1, "m11 = %f\n", vp->viewport.m11);
   batch_out(brw, name, offset, 2, "m22 = %f\n", vp->viewport.m22);
   batch_out(brw, name, offset, 3, "m30 = %f\n", vp->viewport.m30);
   batch_out(brw, name, offset, 4, "m31 = %f\n", vp->viewport.m31);
   batch_out(brw, name, offset, 5, "m32 = %f\n", vp->viewport.m32);

   batch_out(brw, name, offset, 6, "top left = %d,%d\n",
	     vp->scissor.xmin, vp->scissor.ymin);
   batch_out(brw, name, offset, 7, "bottom right = %d,%d\n",
	     vp->scissor.xmax, vp->scissor.ymax);
}

static void dump_clip_viewport_state(struct brw_context *brw,
				     uint32_t offset)
{
   const char *name = "CLIP VP";
   struct brw_clipper_viewport *vp = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   assert(brw->gen < 7);

   batch_out(brw, name, offset, 0, "xmin = %f\n", vp->xmin);
   batch_out(brw, name, offset, 1, "xmax = %f\n", vp->xmax);
   batch_out(brw, name, offset, 2, "ymin = %f\n", vp->ymin);
   batch_out(brw, name, offset, 3, "ymax = %f\n", vp->ymax);
}

static void dump_sf_clip_viewport_state(struct brw_context *brw,
					uint32_t offset)
{
   const char *name = "SF_CLIP VP";
   struct gen7_sf_clip_viewport *vp = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   assert(brw->gen >= 7);

   batch_out(brw, name, offset, 0, "m00 = %f\n", vp->viewport.m00);
   batch_out(brw, name, offset, 1, "m11 = %f\n", vp->viewport.m11);
   batch_out(brw, name, offset, 2, "m22 = %f\n", vp->viewport.m22);
   batch_out(brw, name, offset, 3, "m30 = %f\n", vp->viewport.m30);
   batch_out(brw, name, offset, 4, "m31 = %f\n", vp->viewport.m31);
   batch_out(brw, name, offset, 5, "m32 = %f\n", vp->viewport.m32);
   batch_out(brw, name, offset, 6, "guardband xmin = %f\n", vp->guardband.xmin);
   batch_out(brw, name, offset, 7, "guardband xmax = %f\n", vp->guardband.xmax);
   batch_out(brw, name, offset, 8, "guardband ymin = %f\n", vp->guardband.ymin);
   batch_out(brw, name, offset, 9, "guardband ymax = %f\n", vp->guardband.ymax);
}


static void dump_cc_viewport_state(struct brw_context *brw, uint32_t offset)
{
   const char *name = "CC VP";
   struct brw_cc_viewport *vp = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   batch_out(brw, name, offset, 0, "min_depth = %f\n", vp->min_depth);
   batch_out(brw, name, offset, 1, "max_depth = %f\n", vp->max_depth);
}

static void dump_depth_stencil_state(struct brw_context *brw, uint32_t offset)
{
   const char *name = "D_S";
   struct gen6_depth_stencil_state *ds = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   batch_out(brw, name, offset, 0,
	     "stencil %sable, func %d, write %sable\n",
	     ds->ds0.stencil_enable ? "en" : "dis",
	     ds->ds0.stencil_func,
	     ds->ds0.stencil_write_enable ? "en" : "dis");
   batch_out(brw, name, offset, 1,
	     "stencil test mask 0x%x, write mask 0x%x\n",
	     ds->ds1.stencil_test_mask, ds->ds1.stencil_write_mask);
   batch_out(brw, name, offset, 2,
	     "depth test %sable, func %d, write %sable\n",
	     ds->ds2.depth_test_enable ? "en" : "dis",
	     ds->ds2.depth_test_func,
	     ds->ds2.depth_write_enable ? "en" : "dis");
}

static void dump_cc_state_gen6(struct brw_context *brw, uint32_t offset)
{
   const char *name = "CC";
   struct gen6_color_calc_state *cc = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   batch_out(brw, name, offset, 0,
	     "alpha test format %s, round disable %d, stencil ref %d, "
	     "bf stencil ref %d\n",
	     cc->cc0.alpha_test_format ? "FLOAT32" : "UNORM8",
	     cc->cc0.round_disable,
	     cc->cc0.stencil_ref,
	     cc->cc0.bf_stencil_ref);
   batch_out(brw, name, offset, 1, "\n");
   batch_out(brw, name, offset, 2, "constant red %f\n", cc->constant_r);
   batch_out(brw, name, offset, 3, "constant green %f\n", cc->constant_g);
   batch_out(brw, name, offset, 4, "constant blue %f\n", cc->constant_b);
   batch_out(brw, name, offset, 5, "constant alpha %f\n", cc->constant_a);
}

static void dump_blend_state(struct brw_context *brw, uint32_t offset)
{
   const char *name = "BLEND";

   batch_out(brw, name, offset, 0, "\n");
   batch_out(brw, name, offset, 1, "\n");
}

static void
dump_scissor(struct brw_context *brw, uint32_t offset)
{
   const char *name = "SCISSOR";
   struct gen6_scissor_rect *scissor = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   batch_out(brw, name, offset, 0, "xmin %d, ymin %d\n",
	     scissor->xmin, scissor->ymin);
   batch_out(brw, name, offset, 1, "xmax %d, ymax %d\n",
	     scissor->xmax, scissor->ymax);
}

static void
dump_vs_constants(struct brw_context *brw, uint32_t offset, uint32_t size)
{
   const char *name = "VS_CONST";
   uint32_t *as_uint = brw->cp->bo->get_virtual(brw->cp->bo) + offset;
   float *as_float = brw->cp->bo->get_virtual(brw->cp->bo) + offset;
   int i;

   for (i = 0; i < size / 4; i += 4) {
      batch_out(brw, name, offset, i, "%3d: (% f % f % f % f) (0x%08x 0x%08x 0x%08x 0x%08x)\n",
		i / 4,
		as_float[i], as_float[i + 1], as_float[i + 2], as_float[i + 3],
		as_uint[i], as_uint[i + 1], as_uint[i + 2], as_uint[i + 3]);
   }
}

static void
dump_wm_constants(struct brw_context *brw, uint32_t offset, uint32_t size)
{
   const char *name = "WM_CONST";
   uint32_t *as_uint = brw->cp->bo->get_virtual(brw->cp->bo) + offset;
   float *as_float = brw->cp->bo->get_virtual(brw->cp->bo) + offset;
   int i;

   for (i = 0; i < size / 4; i += 4) {
      batch_out(brw, name, offset, i, "%3d: (% f % f % f % f) (0x%08x 0x%08x 0x%08x 0x%08x)\n",
		i / 4,
		as_float[i], as_float[i + 1], as_float[i + 2], as_float[i + 3],
		as_uint[i], as_uint[i + 1], as_uint[i + 2], as_uint[i + 3]);
   }
}

static void dump_binding_table(struct brw_context *brw, uint32_t offset,
			       uint32_t size)
{
   char name[20];
   int i;
   uint32_t *data = brw->cp->bo->get_virtual(brw->cp->bo) + offset;

   for (i = 0; i < size / 4; i++) {
      if (data[i] == 0)
	 continue;

      sprintf(name, "BIND%d", i);
      batch_out(brw, name, offset, i, "surface state address\n");
   }
}

static void
dump_3d_state(struct i965_3d *hw3d)
{
   int num_states, i;

   dump_clip_viewport_state(hw3d, hw3d->gen6.CLIP_VIEWPORT);

   if (hw3d->gen >= 7)
      dump_sf_clip_viewport_state(hw3d, hw3d->gen6.SF_VIEWPORT);
   else
      dump_sf_viewport_state(hw3d, hw3d->gen6.SF_VIEWPORT);

   dump_cc_viewport_state(hw3d, hw3d->gen6.CC_VIEWPORT);

   dump_blend_state(hw3d, hw3d->gen6.BLEND_STATE);
   dump_cc_state_gen6(hw3d, hw3d->gen6.COLOR_CALC_STATE);
   dump_depth_stencil_state(hw3d, hw3d->gen6.DEPTH_STENCIL_STATE);

   /* VS */
   num_states = 0;
   for (i = 0; i < Elements(hw3d->gen6.vs.SURFACE_STATE); i++) {
      if (!hw3d->gen6.vs.SURFACE_STATE[i])
         continue;

      if (hw3d->gen < 7)
         dump_surface_state(hw3d, hw3d->gen6.vs.SURFACE_STATE[i]);
      else
         dump_gen7_surface_state(hw3d, hw3d->gen6.vs.SURFACE_STATE[i]);
      num_states++;
   }
   dump_binding_table(hw3d, hw3d->gen6.vs.BINDING_TABLE_STATE, num_states * 4);

   num_states = 0;
   for (i = 0; i < Elements(hw3d->gen6.vs.SAMPLER_BORDER_COLOR_STATE); i++) {
      if (!hw3d->gen6.vs.SAMPLER_BORDER_COLOR_STATE[i])
         continue;

      dump_sdc(hw3d, hw3d->gen6.vs.SAMPLER_BORDER_COLOR_STATE[i]);
      num_states++;
   }
   if (hw3d->gen < 7)
      dump_sampler_state(hw3d, hw3d->gen6.vs.SAMPLER_STATE, num_states * 16);
   else
      dump_gen7_sampler_state(hw3d, hw3d->gen6.vs.SAMPLER_STATE, num_states * 16);

   /* GS */
   num_states = 0;
   for (i = 0; i < Elements(hw3d->gen6.gs.SURFACE_STATE); i++) {
      if (!hw3d->gen6.gs.SURFACE_STATE[i])
         continue;

      if (hw3d->gen < 7)
         dump_surface_state(hw3d, hw3d->gen6.gs.SURFACE_STATE[i]);
      else
         dump_gen7_surface_state(hw3d, hw3d->gen6.gs.SURFACE_STATE[i]);
      num_states++;
   }
   dump_binding_table(hw3d, hw3d->gen6.gs.BINDING_TABLE_STATE, num_states * 4);

   /* WM */
   num_states = 0;
   for (i = 0; i < Elements(hw3d->gen6.wm.SURFACE_STATE); i++) {
      if (!hw3d->gen6.wm.SURFACE_STATE[i])
         continue;

      if (hw3d->gen < 7)
         dump_surface_state(hw3d, hw3d->gen6.wm.SURFACE_STATE[i]);
      else
         dump_gen7_surface_state(hw3d, hw3d->gen6.wm.SURFACE_STATE[i]);
      num_states++;
   }
   dump_binding_table(hw3d, hw3d->gen6.wm.BINDING_TABLE_STATE, num_states * 4);

   num_states = 0;
   for (i = 0; i < Elements(hw3d->gen6.wm.SAMPLER_BORDER_COLOR_STATE); i++) {
      if (!hw3d->gen6.wm.SAMPLER_BORDER_COLOR_STATE[i])
         continue;

      dump_sdc(hw3d, hw3d->gen6.wm.SAMPLER_BORDER_COLOR_STATE[i]);
      num_states++;
   }
   if (hw3d->gen < 7)
      dump_sampler_state(hw3d, hw3d->gen6.wm.SAMPLER_STATE, num_states * 16);
   else
      dump_gen7_sampler_state(hw3d, hw3d->gen6.wm.SAMPLER_STATE, num_states * 16);

   dump_scissor(hw3d, hw3d->gen6.SCISSOR_RECT);

   (void) dump_vs_constants;
   (void) dump_wm_constants;
}

void
i965_3d_dump_gen6(struct i965_3d *hw3d)
{
   int err;

   if (hw3d->new_batch)
      return;

   err = hw3d->cp->bo->map(hw3d->cp->bo, false);
   if (!err) {
      dump_3d_state(hw3d);
      hw3d->cp->bo->unmap(hw3d->cp->bo);
   }
}
