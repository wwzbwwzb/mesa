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

#include "intel_chipset.h"

#include "vl/vl_decoder.h"
#include "vl/vl_video_buffer.h"
#include "intel_winsys.h"
#include "i965_common.h"
#include "i965_format.h"
#include "i965_context.h"
#include "i965_resource.h"
#include "i965_screen.h"

#ifdef DEBUG
int i965_debug;
#endif

static const struct debug_named_value i965_debug_flags[] = {
   { "nohw",      I965_DEBUG_NOHW,     "Do not send commands to HW" },
   { "nocache",   I965_DEBUG_NOCACHE,  "Always invalidate HW caches" },
   { "3d",        I965_DEBUG_3D,       "Dump 3D commands and states" },
   { "vs",        I965_DEBUG_VS,       "Dump vertex shaders" },
   { "fs",        I965_DEBUG_FS,       "Dump fragment shaders" },
   DEBUG_NAMED_VALUE_END
};

static float
i965_get_paramf(struct pipe_screen *screen, enum pipe_capf param)
{
   switch (param) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
      return 5.0f;
   case PIPE_CAPF_MAX_POINT_WIDTH:
      return 255.0f;
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return 3.0f;
   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return 16.0f;
   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return 14.0f;
   case PIPE_CAPF_GUARD_BAND_LEFT:
   case PIPE_CAPF_GUARD_BAND_TOP:
   case PIPE_CAPF_GUARD_BAND_RIGHT:
   case PIPE_CAPF_GUARD_BAND_BOTTOM:
      return 0.0f;

   default:
      return 0.0f;
   }
}

static int
i965_get_shader_param(struct pipe_screen *screen, unsigned shader,
                      enum pipe_shader_cap param)
{
   switch (shader) {
   case PIPE_SHADER_FRAGMENT:
   case PIPE_SHADER_VERTEX:
   case PIPE_SHADER_COMPUTE:
      break;
   default:
      return 0;
   }

   switch (param) {
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
      return 16 * 1024;
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
      return (shader == PIPE_SHADER_FRAGMENT) ? 16 * 1024 : 0;
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
      return (shader == PIPE_SHADER_FRAGMENT) ? 16 * 1024 : 0;
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
      return (shader == PIPE_SHADER_FRAGMENT) ? 16 * 1024 : 0;
   case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
      return UINT_MAX;
   case PIPE_SHADER_CAP_MAX_INPUTS:
      return (shader == PIPE_SHADER_FRAGMENT) ? 12 : 16;
   case PIPE_SHADER_CAP_MAX_CONSTS:
      return 1024;
   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      return I965_MAX_CONST_BUFFERS;
   case PIPE_SHADER_CAP_MAX_TEMPS:
      return 256;
   case PIPE_SHADER_CAP_MAX_ADDRS:
      return (shader == PIPE_SHADER_FRAGMENT) ? 0 : 1;
   case PIPE_SHADER_CAP_MAX_PREDS:
      return 0;
   case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
      return 1;
   case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
      return 0;
   case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
      return 0;
   case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
      return (shader == PIPE_SHADER_FRAGMENT) ? 0 : 1;
   case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
      return (shader == PIPE_SHADER_FRAGMENT) ? 0 : 1;
   case PIPE_SHADER_CAP_SUBROUTINES:
      return 0;
   case PIPE_SHADER_CAP_INTEGERS:
      return 1;
   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      return I965_MAX_SAMPLERS;
   case PIPE_SHADER_CAP_PREFERRED_IR:
      return PIPE_SHADER_IR_TGSI;

   default:
      return 0;
   }
}

static int
i965_get_video_param(struct pipe_screen *screen,
                     enum pipe_video_profile profile,
                     enum pipe_video_cap param)
{
   switch (param) {
   case PIPE_VIDEO_CAP_SUPPORTED:
      return vl_profile_supported(screen, profile);
   case PIPE_VIDEO_CAP_NPOT_TEXTURES:
      return 1;
   case PIPE_VIDEO_CAP_MAX_WIDTH:
   case PIPE_VIDEO_CAP_MAX_HEIGHT:
      return vl_video_buffer_max_size(screen);
   case PIPE_VIDEO_CAP_PREFERED_FORMAT:
      return PIPE_FORMAT_NV12;
   case PIPE_VIDEO_CAP_PREFERS_INTERLACED:
      return 1;
   case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
      return 1;
   case PIPE_VIDEO_CAP_SUPPORTS_INTERLACED:
      return 0;

   default:
      return 0;
   }
}

static int
i965_get_compute_param(struct pipe_screen *screen,
                       enum pipe_compute_cap param,
                       void *ret)
{
   union {
      const char *ir_target;
      uint64_t grid_dimension;
      uint64_t max_grid_size[3];
      uint64_t max_block_size[3];
      uint64_t max_threads_per_block;
      uint64_t max_global_size;
      uint64_t max_input_size;
      uint64_t max_local_size;
   } val;
   const void *ptr;
   int size;

   /* XXX some randomly chosen values */
   switch (param) {
   case PIPE_COMPUTE_CAP_IR_TARGET:
      val.ir_target = "i965g";

      ptr = val.ir_target;
      size = strlen(val.ir_target) + 1;
      break;
   case PIPE_COMPUTE_CAP_GRID_DIMENSION:
      val.grid_dimension = Elements(val.max_grid_size);

      ptr = &val.grid_dimension;
      size = sizeof(val.grid_dimension);
      break;
   case PIPE_COMPUTE_CAP_MAX_GRID_SIZE:
      val.max_grid_size[0] = 65535;
      val.max_grid_size[1] = 65535;
      val.max_grid_size[2] = 1;

      ptr = &val.max_grid_size;
      size = sizeof(val.max_grid_size);
      break;
   case PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE:
      val.max_block_size[0] = 256;
      val.max_block_size[1] = 256;
      val.max_block_size[2] = 256;

      ptr = &val.max_block_size;
      size = sizeof(val.max_block_size);
      break;

   case PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK:
      val.max_threads_per_block = 256;

      ptr = &val.max_threads_per_block;
      size = sizeof(val.max_threads_per_block);
      break;
   case PIPE_COMPUTE_CAP_MAX_GLOBAL_SIZE:
      val.max_global_size = 1024 * 64;

      ptr = &val.max_global_size;
      size = sizeof(val.max_global_size);
      break;
   case PIPE_COMPUTE_CAP_MAX_INPUT_SIZE:
      val.max_input_size = 1024;

      ptr = &val.max_input_size;
      size = sizeof(val.max_input_size);
      break;
   case PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE:
      val.max_local_size = 32768;

      ptr = &val.max_local_size;
      size = sizeof(val.max_local_size);
      break;
   default:
      ptr = NULL;
      size = 0;
      break;
   }

   if (ret)
      memcpy(ret, ptr, size);

   return size;
}

static int
i965_get_param(struct pipe_screen *screen, enum pipe_cap param)
{
   struct i965_screen *is = i965_screen(screen);

   assert(is->gen >= 6);

   switch (param) {
#define TODO return 0
   /* supported features */
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_TWO_SIDED_STENCIL:
   case PIPE_CAP_ANISOTROPIC_FILTER:
   case PIPE_CAP_POINT_SPRITE:
   case PIPE_CAP_OCCLUSION_QUERY:
   case PIPE_CAP_TIMER_QUERY:
   case PIPE_CAP_TEXTURE_SHADOW_MAP:
   /* also needed for depth textures (see apply_depthmode() of st/mesa) */
   case PIPE_CAP_TEXTURE_SWIZZLE:
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_SM3:
      return 1;
   case PIPE_CAP_PRIMITIVE_RESTART:
      TODO;
   case PIPE_CAP_INDEP_BLEND_ENABLE:
   case PIPE_CAP_INDEP_BLEND_FUNC:
   case PIPE_CAP_DEPTHSTENCIL_CLEAR_SEPARATE:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
   case PIPE_CAP_DEPTH_CLIP_DISABLE:
      return 1;
   case PIPE_CAP_TGSI_INSTANCEID:
   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
      TODO;
   case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
   case PIPE_CAP_SEAMLESS_CUBE_MAP:
   case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
   case PIPE_CAP_SCALED_RESOLVE:
   case PIPE_CAP_CONDITIONAL_RENDER:
   case PIPE_CAP_TEXTURE_BARRIER:
      return 1;
   case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
      TODO;
   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
      return 1;
   case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
      /* no limitation */
      return 0;
   case PIPE_CAP_COMPUTE:
      TODO;
   case PIPE_CAP_USER_CONSTANT_BUFFERS:
      /* use push constants */
      TODO;
   case PIPE_CAP_START_INSTANCE:
      return 1;
   case PIPE_CAP_QUERY_TIMESTAMP:
      /* use winsys->read_reg() */
   case PIPE_CAP_TEXTURE_MULTISAMPLE:
   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
   case PIPE_CAP_CUBE_MAP_ARRAY:
   case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
      TODO;
#undef TODO
   /* unsupported features */
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
   case PIPE_CAP_SHADER_STENCIL_EXPORT:
   case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
   case PIPE_CAP_TGSI_CAN_COMPACT_VARYINGS:
   case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS:
   case PIPE_CAP_VERTEX_COLOR_CLAMPED:
   case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
   case PIPE_CAP_USER_VERTEX_BUFFERS:
   case PIPE_CAP_USER_INDEX_BUFFERS:
      return 0;

   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
      return 1;
   case PIPE_CAP_MAX_RENDER_TARGETS:
      return I965_MAX_DRAW_BUFFERS;
   case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
      return 14;
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return 9;
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return 12;
   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
      return I965_MAX_SO_BUFFERS;
   case PIPE_CAP_MAX_COMBINED_SAMPLERS:
      return I965_MAX_SAMPLERS * 2;
   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
      return (is->gen >= 7) ? 2048 : 512;
   case PIPE_CAP_MIN_TEXEL_OFFSET:
      return -8;
   case PIPE_CAP_MAX_TEXEL_OFFSET:
      return 7;
   case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
      return I965_MAX_SO_BINDINGS / I965_MAX_SO_BUFFERS;
   case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
      return I965_MAX_SO_BINDINGS;
   case PIPE_CAP_GLSL_FEATURE_LEVEL:
      return 130;
   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return 16;

   default:
      return 0;
   }
}

static const char *
i965_get_vendor(struct pipe_screen *screen)
{
   return "We Love Gallium3D";
}

static const char *
i965_get_name(struct pipe_screen *screen)
{
   struct i965_screen *is = i965_screen(screen);
   const char *chipset;

   /* steal from classic i965 */
   switch (is->devid) {
   case PCI_CHIP_845_G:
      chipset = "Intel(R) 845G";
      break;
   case PCI_CHIP_I830_M:
      chipset = "Intel(R) 830M";
      break;
   case PCI_CHIP_I855_GM:
      chipset = "Intel(R) 852GM/855GM";
      break;
   case PCI_CHIP_I865_G:
      chipset = "Intel(R) 865G";
      break;
   case PCI_CHIP_I915_G:
      chipset = "Intel(R) 915G";
      break;
   case PCI_CHIP_E7221_G:
      chipset = "Intel (R) E7221G (i915)";
      break;
   case PCI_CHIP_I915_GM:
      chipset = "Intel(R) 915GM";
      break;
   case PCI_CHIP_I945_G:
      chipset = "Intel(R) 945G";
      break;
   case PCI_CHIP_I945_GM:
      chipset = "Intel(R) 945GM";
      break;
   case PCI_CHIP_I945_GME:
      chipset = "Intel(R) 945GME";
      break;
   case PCI_CHIP_G33_G:
      chipset = "Intel(R) G33";
      break;
   case PCI_CHIP_Q35_G:
      chipset = "Intel(R) Q35";
      break;
   case PCI_CHIP_Q33_G:
      chipset = "Intel(R) Q33";
      break;
   case PCI_CHIP_IGD_GM:
   case PCI_CHIP_IGD_G:
      chipset = "Intel(R) IGD";
      break;
   case PCI_CHIP_I965_Q:
      chipset = "Intel(R) 965Q";
      break;
   case PCI_CHIP_I965_G:
   case PCI_CHIP_I965_G_1:
      chipset = "Intel(R) 965G";
      break;
   case PCI_CHIP_I946_GZ:
      chipset = "Intel(R) 946GZ";
      break;
   case PCI_CHIP_I965_GM:
      chipset = "Intel(R) 965GM";
      break;
   case PCI_CHIP_I965_GME:
      chipset = "Intel(R) 965GME/GLE";
      break;
   case PCI_CHIP_GM45_GM:
      chipset = "Mobile Intel® GM45 Express Chipset";
      break; 
   case PCI_CHIP_IGD_E_G:
      chipset = "Intel(R) Integrated Graphics Device";
      break;
   case PCI_CHIP_G45_G:
      chipset = "Intel(R) G45/G43";
      break;
   case PCI_CHIP_Q45_G:
      chipset = "Intel(R) Q45/Q43";
      break;
   case PCI_CHIP_G41_G:
      chipset = "Intel(R) G41";
      break;
   case PCI_CHIP_B43_G:
   case PCI_CHIP_B43_G1:
      chipset = "Intel(R) B43";
      break;
   case PCI_CHIP_ILD_G:
      chipset = "Intel(R) Ironlake Desktop";
      break;
   case PCI_CHIP_ILM_G:
      chipset = "Intel(R) Ironlake Mobile";
      break;
   case PCI_CHIP_SANDYBRIDGE_GT1:
   case PCI_CHIP_SANDYBRIDGE_GT2:
   case PCI_CHIP_SANDYBRIDGE_GT2_PLUS:
      chipset = "Intel(R) Sandybridge Desktop";
      break;
   case PCI_CHIP_SANDYBRIDGE_M_GT1:
   case PCI_CHIP_SANDYBRIDGE_M_GT2:
   case PCI_CHIP_SANDYBRIDGE_M_GT2_PLUS:
      chipset = "Intel(R) Sandybridge Mobile";
      break;
   case PCI_CHIP_SANDYBRIDGE_S:
      chipset = "Intel(R) Sandybridge Server";
      break;
   case PCI_CHIP_IVYBRIDGE_GT1:
   case PCI_CHIP_IVYBRIDGE_GT2:
      chipset = "Intel(R) Ivybridge Desktop";
      break;
   case PCI_CHIP_IVYBRIDGE_M_GT1:
   case PCI_CHIP_IVYBRIDGE_M_GT2:
      chipset = "Intel(R) Ivybridge Mobile";
      break;
   case PCI_CHIP_IVYBRIDGE_S_GT1:
   case PCI_CHIP_IVYBRIDGE_S_GT2:
      chipset = "Intel(R) Ivybridge Server";
      break;
   case PCI_CHIP_HASWELL_GT1:
   case PCI_CHIP_HASWELL_GT2:
   case PCI_CHIP_HASWELL_GT2_PLUS:
   case PCI_CHIP_HASWELL_SDV_GT1:
   case PCI_CHIP_HASWELL_SDV_GT2:
   case PCI_CHIP_HASWELL_SDV_GT2_PLUS:
   case PCI_CHIP_HASWELL_ULT_GT1:
   case PCI_CHIP_HASWELL_ULT_GT2:
   case PCI_CHIP_HASWELL_ULT_GT2_PLUS:
   case PCI_CHIP_HASWELL_CRW_GT1:
   case PCI_CHIP_HASWELL_CRW_GT2:
   case PCI_CHIP_HASWELL_CRW_GT2_PLUS:
      chipset = "Intel(R) Haswell Desktop";
      break;
   case PCI_CHIP_HASWELL_M_GT1:
   case PCI_CHIP_HASWELL_M_GT2:
   case PCI_CHIP_HASWELL_M_GT2_PLUS:
   case PCI_CHIP_HASWELL_SDV_M_GT1:
   case PCI_CHIP_HASWELL_SDV_M_GT2:
   case PCI_CHIP_HASWELL_SDV_M_GT2_PLUS:
   case PCI_CHIP_HASWELL_ULT_M_GT1:
   case PCI_CHIP_HASWELL_ULT_M_GT2:
   case PCI_CHIP_HASWELL_ULT_M_GT2_PLUS:
   case PCI_CHIP_HASWELL_CRW_M_GT1:
   case PCI_CHIP_HASWELL_CRW_M_GT2:
   case PCI_CHIP_HASWELL_CRW_M_GT2_PLUS:
      chipset = "Intel(R) Haswell Mobile";
      break;
   case PCI_CHIP_HASWELL_S_GT1:
   case PCI_CHIP_HASWELL_S_GT2:
   case PCI_CHIP_HASWELL_S_GT2_PLUS:
   case PCI_CHIP_HASWELL_SDV_S_GT1:
   case PCI_CHIP_HASWELL_SDV_S_GT2:
   case PCI_CHIP_HASWELL_SDV_S_GT2_PLUS:
   case PCI_CHIP_HASWELL_ULT_S_GT1:
   case PCI_CHIP_HASWELL_ULT_S_GT2:
   case PCI_CHIP_HASWELL_ULT_S_GT2_PLUS:
   case PCI_CHIP_HASWELL_CRW_S_GT1:
   case PCI_CHIP_HASWELL_CRW_S_GT2:
   case PCI_CHIP_HASWELL_CRW_S_GT2_PLUS:
      chipset = "Intel(R) Haswell Server";
      break;
   default:
      chipset = "Unknown Intel Chipset";
      break;
   }

   return chipset;
}

static void
i965_screen_destroy(struct pipe_screen *screen)
{
   struct i965_screen *is = i965_screen(screen);

   FREE(is);
}

static void
i965_fence_reference(struct pipe_screen *screen,
                     struct pipe_fence_handle **p,
                     struct pipe_fence_handle *f)
{
   struct i965_fence **ptr = (struct i965_fence **) p;
   struct i965_fence *fence = i965_fence(f);

   if (!ptr) {
      if (fence)
         pipe_reference(NULL, &fence->reference);
      return;
   }

   if (*ptr && pipe_reference(&(*ptr)->reference, &fence->reference)) {
      struct i965_fence *old = *ptr;

      if (old->bo)
         old->bo->unreference(old->bo);
      FREE(old);
   }

   *ptr = fence;
}

static boolean
i965_fence_signalled(struct pipe_screen *screen,
                     struct pipe_fence_handle *f)
{
   struct i965_fence *fence = i965_fence(f);

   if (fence->bo && !fence->bo->busy(fence->bo)) {
      fence->bo->unreference(fence->bo);
      fence->bo = NULL;
   }

   return (fence->bo == NULL);
}

static boolean
i965_fence_finish(struct pipe_screen *screen,
                  struct pipe_fence_handle *f,
                  uint64_t timeout)
{
   struct i965_fence *fence = i965_fence(f);

   if (fence->bo) {
      /* how about timeout? */
      fence->bo->wait_rendering(fence->bo);
      fence->bo->unreference(fence->bo);
      fence->bo = NULL;
   }

   return TRUE;
}

struct pipe_screen *
i965_screen_create(struct intel_winsys *ws)
{
   struct i965_screen *is;
   const struct intel_info *info;

#ifdef DEBUG
   i965_debug = debug_get_flags_option("I965_DEBUG", i965_debug_flags, 0);
#endif

   is = CALLOC_STRUCT(i965_screen);
   if (!is)
      return NULL;

   is->winsys = ws;

   info = is->winsys->get_info(is->winsys);

   /* require fences */
   if (!info->num_fences_avail) {
      FREE(is);
      return NULL;
   }

   is->winsys->enable_fenced_relocs(is->winsys);

   is->devid = info->devid;
   if (IS_GEN6(info->devid)) {
      is->gen = 6;
   }
   else {
      /* only GEN6 is supported */
      FREE(is);
      return NULL;
   }

   is->base.destroy = i965_screen_destroy;
   is->base.get_name = i965_get_name;
   is->base.get_vendor = i965_get_vendor;
   is->base.get_param = i965_get_param;
   is->base.get_paramf = i965_get_paramf;
   is->base.get_shader_param = i965_get_shader_param;
   is->base.get_video_param = i965_get_video_param;
   is->base.get_compute_param = i965_get_compute_param;

   is->base.flush_frontbuffer = NULL;
   is->base.fence_reference = i965_fence_reference;
   is->base.fence_signalled = i965_fence_signalled;
   is->base.fence_finish = i965_fence_finish;

   i965_init_format_functions(is);
   i965_init_context_functions(is);
   i965_init_resource_functions(is);

   return &is->base;
}
