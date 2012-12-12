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
#include "intel_winsys.h"

#include "i965_common.h"
#include "i965_translate.h"

/**
 * Translate winsys tiling to hardware tiling.
 */
int
i965_translate_winsys_tiling(enum intel_tiling_mode tiling)
{
   switch (tiling) {
   case INTEL_TILING_NONE:
      return 0;
   case INTEL_TILING_X:
      return BRW_SURFACE_TILED;
   case INTEL_TILING_Y:
      return BRW_SURFACE_TILED | BRW_SURFACE_TILED_Y;
   default:
      assert(!"unknown tiling");
      return 0;
   }
}

/**
 * Translate a color (non-depth/stencil) pipe format to the matching hardware
 * format.  Return -1 on errors.
 */
int
i965_translate_color_format(enum pipe_format format)
{
   static const int format_mapping[PIPE_FORMAT_COUNT] = {
      [PIPE_FORMAT_NONE]                    = 0,
      [PIPE_FORMAT_B8G8R8A8_UNORM]          = BRW_SURFACEFORMAT_B8G8R8A8_UNORM,
      [PIPE_FORMAT_B8G8R8X8_UNORM]          = BRW_SURFACEFORMAT_B8G8R8X8_UNORM,
      [PIPE_FORMAT_A8R8G8B8_UNORM]          = 0,
      [PIPE_FORMAT_X8R8G8B8_UNORM]          = 0,
      [PIPE_FORMAT_B5G5R5A1_UNORM]          = BRW_SURFACEFORMAT_B5G5R5A1_UNORM,
      [PIPE_FORMAT_B4G4R4A4_UNORM]          = BRW_SURFACEFORMAT_B4G4R4A4_UNORM,
      [PIPE_FORMAT_B5G6R5_UNORM]            = BRW_SURFACEFORMAT_B5G6R5_UNORM,
      [PIPE_FORMAT_R10G10B10A2_UNORM]       = BRW_SURFACEFORMAT_R10G10B10A2_UNORM,
      [PIPE_FORMAT_L8_UNORM]                = BRW_SURFACEFORMAT_L8_UNORM,
      [PIPE_FORMAT_A8_UNORM]                = BRW_SURFACEFORMAT_A8_UNORM,
      [PIPE_FORMAT_I8_UNORM]                = BRW_SURFACEFORMAT_I8_UNORM,
      [PIPE_FORMAT_L8A8_UNORM]              = BRW_SURFACEFORMAT_L8A8_UNORM,
      [PIPE_FORMAT_L16_UNORM]               = BRW_SURFACEFORMAT_L16_UNORM,
      [PIPE_FORMAT_UYVY]                    = BRW_SURFACEFORMAT_YCRCB_SWAPUVY,
      [PIPE_FORMAT_YUYV]                    = BRW_SURFACEFORMAT_YCRCB_NORMAL,
      [PIPE_FORMAT_Z16_UNORM]               = 0,
      [PIPE_FORMAT_Z32_UNORM]               = 0,
      [PIPE_FORMAT_Z32_FLOAT]               = 0,
      [PIPE_FORMAT_Z24_UNORM_S8_UINT]       = 0,
      [PIPE_FORMAT_S8_UINT_Z24_UNORM]       = 0,
      [PIPE_FORMAT_Z24X8_UNORM]             = 0,
      [PIPE_FORMAT_X8Z24_UNORM]             = 0,
      [PIPE_FORMAT_S8_UINT]                 = 0,
      [PIPE_FORMAT_R64_FLOAT]               = BRW_SURFACEFORMAT_R64_FLOAT,
      [PIPE_FORMAT_R64G64_FLOAT]            = BRW_SURFACEFORMAT_R64G64_FLOAT,
      [PIPE_FORMAT_R64G64B64_FLOAT]         = BRW_SURFACEFORMAT_R64G64B64_FLOAT,
      [PIPE_FORMAT_R64G64B64A64_FLOAT]      = BRW_SURFACEFORMAT_R64G64B64A64_FLOAT,
      [PIPE_FORMAT_R32_FLOAT]               = BRW_SURFACEFORMAT_R32_FLOAT,
      [PIPE_FORMAT_R32G32_FLOAT]            = BRW_SURFACEFORMAT_R32G32_FLOAT,
      [PIPE_FORMAT_R32G32B32_FLOAT]         = BRW_SURFACEFORMAT_R32G32B32_FLOAT,
      [PIPE_FORMAT_R32G32B32A32_FLOAT]      = BRW_SURFACEFORMAT_R32G32B32A32_FLOAT,
      [PIPE_FORMAT_R32_UNORM]               = BRW_SURFACEFORMAT_R32_UNORM,
      [PIPE_FORMAT_R32G32_UNORM]            = BRW_SURFACEFORMAT_R32G32_UNORM,
      [PIPE_FORMAT_R32G32B32_UNORM]         = BRW_SURFACEFORMAT_R32G32B32_UNORM,
      [PIPE_FORMAT_R32G32B32A32_UNORM]      = BRW_SURFACEFORMAT_R32G32B32A32_UNORM,
      [PIPE_FORMAT_R32_USCALED]             = BRW_SURFACEFORMAT_R32_USCALED,
      [PIPE_FORMAT_R32G32_USCALED]          = BRW_SURFACEFORMAT_R32G32_USCALED,
      [PIPE_FORMAT_R32G32B32_USCALED]       = BRW_SURFACEFORMAT_R32G32B32_USCALED,
      [PIPE_FORMAT_R32G32B32A32_USCALED]    = BRW_SURFACEFORMAT_R32G32B32A32_USCALED,
      [PIPE_FORMAT_R32_SNORM]               = BRW_SURFACEFORMAT_R32_SNORM,
      [PIPE_FORMAT_R32G32_SNORM]            = BRW_SURFACEFORMAT_R32G32_SNORM,
      [PIPE_FORMAT_R32G32B32_SNORM]         = BRW_SURFACEFORMAT_R32G32B32_SNORM,
      [PIPE_FORMAT_R32G32B32A32_SNORM]      = BRW_SURFACEFORMAT_R32G32B32A32_SNORM,
      [PIPE_FORMAT_R32_SSCALED]             = BRW_SURFACEFORMAT_R32_SSCALED,
      [PIPE_FORMAT_R32G32_SSCALED]          = BRW_SURFACEFORMAT_R32G32_SSCALED,
      [PIPE_FORMAT_R32G32B32_SSCALED]       = BRW_SURFACEFORMAT_R32G32B32_SSCALED,
      [PIPE_FORMAT_R32G32B32A32_SSCALED]    = BRW_SURFACEFORMAT_R32G32B32A32_SSCALED,
      [PIPE_FORMAT_R16_UNORM]               = BRW_SURFACEFORMAT_R16_UNORM,
      [PIPE_FORMAT_R16G16_UNORM]            = BRW_SURFACEFORMAT_R16G16_UNORM,
      [PIPE_FORMAT_R16G16B16_UNORM]         = BRW_SURFACEFORMAT_R16G16B16_UNORM,
      [PIPE_FORMAT_R16G16B16A16_UNORM]      = BRW_SURFACEFORMAT_R16G16B16A16_UNORM,
      [PIPE_FORMAT_R16_USCALED]             = BRW_SURFACEFORMAT_R16_USCALED,
      [PIPE_FORMAT_R16G16_USCALED]          = BRW_SURFACEFORMAT_R16G16_USCALED,
      [PIPE_FORMAT_R16G16B16_USCALED]       = BRW_SURFACEFORMAT_R16G16B16_USCALED,
      [PIPE_FORMAT_R16G16B16A16_USCALED]    = BRW_SURFACEFORMAT_R16G16B16A16_USCALED,
      [PIPE_FORMAT_R16_SNORM]               = BRW_SURFACEFORMAT_R16_SNORM,
      [PIPE_FORMAT_R16G16_SNORM]            = BRW_SURFACEFORMAT_R16G16_SNORM,
      [PIPE_FORMAT_R16G16B16_SNORM]         = BRW_SURFACEFORMAT_R16G16B16_SNORM,
      [PIPE_FORMAT_R16G16B16A16_SNORM]      = BRW_SURFACEFORMAT_R16G16B16A16_SNORM,
      [PIPE_FORMAT_R16_SSCALED]             = BRW_SURFACEFORMAT_R16_SSCALED,
      [PIPE_FORMAT_R16G16_SSCALED]          = BRW_SURFACEFORMAT_R16G16_SSCALED,
      [PIPE_FORMAT_R16G16B16_SSCALED]       = BRW_SURFACEFORMAT_R16G16B16_SSCALED,
      [PIPE_FORMAT_R16G16B16A16_SSCALED]    = BRW_SURFACEFORMAT_R16G16B16A16_SSCALED,
      [PIPE_FORMAT_R8_UNORM]                = BRW_SURFACEFORMAT_R8_UNORM,
      [PIPE_FORMAT_R8G8_UNORM]              = BRW_SURFACEFORMAT_R8G8_UNORM,
      [PIPE_FORMAT_R8G8B8_UNORM]            = BRW_SURFACEFORMAT_R8G8B8_UNORM,
      [PIPE_FORMAT_R8G8B8A8_UNORM]          = BRW_SURFACEFORMAT_R8G8B8A8_UNORM,
      [PIPE_FORMAT_X8B8G8R8_UNORM]          = 0,
      [PIPE_FORMAT_R8_USCALED]              = BRW_SURFACEFORMAT_R8_USCALED,
      [PIPE_FORMAT_R8G8_USCALED]            = BRW_SURFACEFORMAT_R8G8_USCALED,
      [PIPE_FORMAT_R8G8B8_USCALED]          = BRW_SURFACEFORMAT_R8G8B8_USCALED,
      [PIPE_FORMAT_R8G8B8A8_USCALED]        = BRW_SURFACEFORMAT_R8G8B8A8_USCALED,
      [PIPE_FORMAT_R8_SNORM]                = BRW_SURFACEFORMAT_R8_SNORM,
      [PIPE_FORMAT_R8G8_SNORM]              = BRW_SURFACEFORMAT_R8G8_SNORM,
      [PIPE_FORMAT_R8G8B8_SNORM]            = BRW_SURFACEFORMAT_R8G8B8_SNORM,
      [PIPE_FORMAT_R8G8B8A8_SNORM]          = BRW_SURFACEFORMAT_R8G8B8A8_SNORM,
      [PIPE_FORMAT_R8_SSCALED]              = BRW_SURFACEFORMAT_R8_SSCALED,
      [PIPE_FORMAT_R8G8_SSCALED]            = BRW_SURFACEFORMAT_R8G8_SSCALED,
      [PIPE_FORMAT_R8G8B8_SSCALED]          = BRW_SURFACEFORMAT_R8G8B8_SSCALED,
      [PIPE_FORMAT_R8G8B8A8_SSCALED]        = BRW_SURFACEFORMAT_R8G8B8A8_SSCALED,
      [PIPE_FORMAT_R32_FIXED]               = 0,
      [PIPE_FORMAT_R32G32_FIXED]            = 0,
      [PIPE_FORMAT_R32G32B32_FIXED]         = 0,
      [PIPE_FORMAT_R32G32B32A32_FIXED]      = 0,
      [PIPE_FORMAT_R16_FLOAT]               = BRW_SURFACEFORMAT_R16_FLOAT,
      [PIPE_FORMAT_R16G16_FLOAT]            = BRW_SURFACEFORMAT_R16G16_FLOAT,
      [PIPE_FORMAT_R16G16B16_FLOAT]         = 0,
      [PIPE_FORMAT_R16G16B16A16_FLOAT]      = BRW_SURFACEFORMAT_R16G16B16A16_FLOAT,
      [PIPE_FORMAT_L8_SRGB]                 = BRW_SURFACEFORMAT_L8_UNORM_SRGB,
      [PIPE_FORMAT_L8A8_SRGB]               = BRW_SURFACEFORMAT_L8A8_UNORM_SRGB,
      [PIPE_FORMAT_R8G8B8_SRGB]             = 0,
      [PIPE_FORMAT_A8B8G8R8_SRGB]           = 0,
      [PIPE_FORMAT_X8B8G8R8_SRGB]           = 0,
      [PIPE_FORMAT_B8G8R8A8_SRGB]           = BRW_SURFACEFORMAT_B8G8R8A8_UNORM_SRGB,
      [PIPE_FORMAT_B8G8R8X8_SRGB]           = 0,
      [PIPE_FORMAT_A8R8G8B8_SRGB]           = 0,
      [PIPE_FORMAT_X8R8G8B8_SRGB]           = 0,
      [PIPE_FORMAT_R8G8B8A8_SRGB]           = 0,
      [PIPE_FORMAT_DXT1_RGB]                = BRW_SURFACEFORMAT_DXT1_RGB,
      [PIPE_FORMAT_DXT1_RGBA]               = BRW_SURFACEFORMAT_BC1_UNORM,
      [PIPE_FORMAT_DXT3_RGBA]               = BRW_SURFACEFORMAT_BC2_UNORM,
      [PIPE_FORMAT_DXT5_RGBA]               = BRW_SURFACEFORMAT_BC3_UNORM,
      [PIPE_FORMAT_DXT1_SRGB]               = BRW_SURFACEFORMAT_DXT1_RGB_SRGB,
      [PIPE_FORMAT_DXT1_SRGBA]              = BRW_SURFACEFORMAT_BC1_UNORM_SRGB,
      [PIPE_FORMAT_DXT3_SRGBA]              = BRW_SURFACEFORMAT_BC2_UNORM_SRGB,
      [PIPE_FORMAT_DXT5_SRGBA]              = BRW_SURFACEFORMAT_BC3_UNORM_SRGB,
      [PIPE_FORMAT_RGTC1_UNORM]             = BRW_SURFACEFORMAT_BC4_UNORM,
      [PIPE_FORMAT_RGTC1_SNORM]             = BRW_SURFACEFORMAT_BC4_SNORM,
      [PIPE_FORMAT_RGTC2_UNORM]             = BRW_SURFACEFORMAT_BC5_UNORM,
      [PIPE_FORMAT_RGTC2_SNORM]             = BRW_SURFACEFORMAT_BC5_SNORM,
      [PIPE_FORMAT_R8G8_B8G8_UNORM]         = 0,
      [PIPE_FORMAT_G8R8_G8B8_UNORM]         = 0,
      [PIPE_FORMAT_R8SG8SB8UX8U_NORM]       = 0,
      [PIPE_FORMAT_R5SG5SB6U_NORM]          = 0,
      [PIPE_FORMAT_A8B8G8R8_UNORM]          = 0,
      [PIPE_FORMAT_B5G5R5X1_UNORM]          = BRW_SURFACEFORMAT_B5G5R5X1_UNORM,
      [PIPE_FORMAT_R10G10B10A2_USCALED]     = 0,
      [PIPE_FORMAT_R11G11B10_FLOAT]         = BRW_SURFACEFORMAT_R11G11B10_FLOAT,
      [PIPE_FORMAT_R9G9B9E5_FLOAT]          = BRW_SURFACEFORMAT_R9G9B9E5_SHAREDEXP,
      [PIPE_FORMAT_Z32_FLOAT_S8X24_UINT]    = 0,
      [PIPE_FORMAT_R1_UNORM]                = 0,
      [PIPE_FORMAT_R10G10B10X2_USCALED]     = BRW_SURFACEFORMAT_R10G10B10X2_USCALED,
      [PIPE_FORMAT_R10G10B10X2_SNORM]       = 0,
      [PIPE_FORMAT_L4A4_UNORM]              = 0,
      [PIPE_FORMAT_B10G10R10A2_UNORM]       = BRW_SURFACEFORMAT_B10G10R10A2_UNORM,
      [PIPE_FORMAT_R10SG10SB10SA2U_NORM]    = 0,
      [PIPE_FORMAT_R8G8Bx_SNORM]            = 0,
      [PIPE_FORMAT_R8G8B8X8_UNORM]          = BRW_SURFACEFORMAT_R8G8B8X8_UNORM,
      [PIPE_FORMAT_B4G4R4X4_UNORM]          = 0,
      [PIPE_FORMAT_X24S8_UINT]              = 0,
      [PIPE_FORMAT_S8X24_UINT]              = 0,
      [PIPE_FORMAT_X32_S8X24_UINT]          = 0,
      [PIPE_FORMAT_B2G3R3_UNORM]            = 0,
      [PIPE_FORMAT_L16A16_UNORM]            = BRW_SURFACEFORMAT_L16A16_UNORM,
      [PIPE_FORMAT_A16_UNORM]               = BRW_SURFACEFORMAT_A16_UNORM,
      [PIPE_FORMAT_I16_UNORM]               = BRW_SURFACEFORMAT_I16_UNORM,
      [PIPE_FORMAT_LATC1_UNORM]             = 0,
      [PIPE_FORMAT_LATC1_SNORM]             = 0,
      [PIPE_FORMAT_LATC2_UNORM]             = 0,
      [PIPE_FORMAT_LATC2_SNORM]             = 0,
      [PIPE_FORMAT_A8_SNORM]                = 0,
      [PIPE_FORMAT_L8_SNORM]                = 0,
      [PIPE_FORMAT_L8A8_SNORM]              = 0,
      [PIPE_FORMAT_I8_SNORM]                = 0,
      [PIPE_FORMAT_A16_SNORM]               = 0,
      [PIPE_FORMAT_L16_SNORM]               = 0,
      [PIPE_FORMAT_L16A16_SNORM]            = 0,
      [PIPE_FORMAT_I16_SNORM]               = 0,
      [PIPE_FORMAT_A16_FLOAT]               = BRW_SURFACEFORMAT_A16_FLOAT,
      [PIPE_FORMAT_L16_FLOAT]               = BRW_SURFACEFORMAT_L16_FLOAT,
      [PIPE_FORMAT_L16A16_FLOAT]            = BRW_SURFACEFORMAT_L16A16_FLOAT,
      [PIPE_FORMAT_I16_FLOAT]               = BRW_SURFACEFORMAT_I16_FLOAT,
      [PIPE_FORMAT_A32_FLOAT]               = BRW_SURFACEFORMAT_A32_FLOAT,
      [PIPE_FORMAT_L32_FLOAT]               = BRW_SURFACEFORMAT_L32_FLOAT,
      [PIPE_FORMAT_L32A32_FLOAT]            = BRW_SURFACEFORMAT_L32A32_FLOAT,
      [PIPE_FORMAT_I32_FLOAT]               = BRW_SURFACEFORMAT_I32_FLOAT,
      [PIPE_FORMAT_YV12]                    = 0,
      [PIPE_FORMAT_YV16]                    = 0,
      [PIPE_FORMAT_IYUV]                    = 0,
      [PIPE_FORMAT_NV12]                    = 0,
      [PIPE_FORMAT_NV21]                    = 0,
      [PIPE_FORMAT_R4A4_UNORM]              = 0,
      [PIPE_FORMAT_A4R4_UNORM]              = 0,
      [PIPE_FORMAT_R8A8_UNORM]              = 0,
      [PIPE_FORMAT_A8R8_UNORM]              = 0,
      [PIPE_FORMAT_R10G10B10A2_SSCALED]     = 0,
      [PIPE_FORMAT_R10G10B10A2_SNORM]       = 0,
      [PIPE_FORMAT_B10G10R10A2_USCALED]     = 0,
      [PIPE_FORMAT_B10G10R10A2_SSCALED]     = 0,
      [PIPE_FORMAT_B10G10R10A2_SNORM]       = 0,
      [PIPE_FORMAT_R8_UINT]                 = BRW_SURFACEFORMAT_R8_UINT,
      [PIPE_FORMAT_R8G8_UINT]               = BRW_SURFACEFORMAT_R8G8_UINT,
      [PIPE_FORMAT_R8G8B8_UINT]             = 0,
      [PIPE_FORMAT_R8G8B8A8_UINT]           = BRW_SURFACEFORMAT_R8G8B8A8_UINT,
      [PIPE_FORMAT_R8_SINT]                 = BRW_SURFACEFORMAT_R8_SINT,
      [PIPE_FORMAT_R8G8_SINT]               = BRW_SURFACEFORMAT_R8G8_SINT,
      [PIPE_FORMAT_R8G8B8_SINT]             = 0,
      [PIPE_FORMAT_R8G8B8A8_SINT]           = BRW_SURFACEFORMAT_R8G8B8A8_SINT,
      [PIPE_FORMAT_R16_UINT]                = BRW_SURFACEFORMAT_R16_UINT,
      [PIPE_FORMAT_R16G16_UINT]             = BRW_SURFACEFORMAT_R16G16_UINT,
      [PIPE_FORMAT_R16G16B16_UINT]          = 0,
      [PIPE_FORMAT_R16G16B16A16_UINT]       = BRW_SURFACEFORMAT_R16G16B16A16_UINT,
      [PIPE_FORMAT_R16_SINT]                = BRW_SURFACEFORMAT_R16_SINT,
      [PIPE_FORMAT_R16G16_SINT]             = BRW_SURFACEFORMAT_R16G16_SINT,
      [PIPE_FORMAT_R16G16B16_SINT]          = 0,
      [PIPE_FORMAT_R16G16B16A16_SINT]       = BRW_SURFACEFORMAT_R16G16B16A16_SINT,
      [PIPE_FORMAT_R32_UINT]                = BRW_SURFACEFORMAT_R32_UINT,
      [PIPE_FORMAT_R32G32_UINT]             = BRW_SURFACEFORMAT_R32G32_UINT,
      [PIPE_FORMAT_R32G32B32_UINT]          = BRW_SURFACEFORMAT_R32G32B32_UINT,
      [PIPE_FORMAT_R32G32B32A32_UINT]       = BRW_SURFACEFORMAT_R32G32B32A32_UINT,
      [PIPE_FORMAT_R32_SINT]                = BRW_SURFACEFORMAT_R32_SINT,
      [PIPE_FORMAT_R32G32_SINT]             = BRW_SURFACEFORMAT_R32G32_SINT,
      [PIPE_FORMAT_R32G32B32_SINT]          = BRW_SURFACEFORMAT_R32G32B32_SINT,
      [PIPE_FORMAT_R32G32B32A32_SINT]       = BRW_SURFACEFORMAT_R32G32B32A32_SINT,
      [PIPE_FORMAT_A8_UINT]                 = 0,
      [PIPE_FORMAT_I8_UINT]                 = 0,
      [PIPE_FORMAT_L8_UINT]                 = 0,
      [PIPE_FORMAT_L8A8_UINT]               = 0,
      [PIPE_FORMAT_A8_SINT]                 = 0,
      [PIPE_FORMAT_I8_SINT]                 = 0,
      [PIPE_FORMAT_L8_SINT]                 = 0,
      [PIPE_FORMAT_L8A8_SINT]               = 0,
      [PIPE_FORMAT_A16_UINT]                = 0,
      [PIPE_FORMAT_I16_UINT]                = 0,
      [PIPE_FORMAT_L16_UINT]                = 0,
      [PIPE_FORMAT_L16A16_UINT]             = 0,
      [PIPE_FORMAT_A16_SINT]                = 0,
      [PIPE_FORMAT_I16_SINT]                = 0,
      [PIPE_FORMAT_L16_SINT]                = 0,
      [PIPE_FORMAT_L16A16_SINT]             = 0,
      [PIPE_FORMAT_A32_UINT]                = 0,
      [PIPE_FORMAT_I32_UINT]                = 0,
      [PIPE_FORMAT_L32_UINT]                = 0,
      [PIPE_FORMAT_L32A32_UINT]             = 0,
      [PIPE_FORMAT_A32_SINT]                = 0,
      [PIPE_FORMAT_I32_SINT]                = 0,
      [PIPE_FORMAT_L32_SINT]                = 0,
      [PIPE_FORMAT_L32A32_SINT]             = 0,
      [PIPE_FORMAT_B10G10R10A2_UINT]        = 0,
      [PIPE_FORMAT_ETC1_RGB8]               = 0,
      [PIPE_FORMAT_R8G8_R8B8_UNORM]         = 0,
      [PIPE_FORMAT_G8R8_B8R8_UNORM]         = 0,
   };
   int sfmt = format_mapping[format];

   /* BRW_SURFACEFORMAT_R32G32B32A32_FLOAT happens to be 0 */
   if (!sfmt && format != PIPE_FORMAT_R32G32B32A32_FLOAT)
      sfmt = -1;

   return sfmt;
}

/**
 * Translate a depth/stencil pipe format to the matching hardware
 * format.  Return -1 on errors.
 */
int
i965_translate_depth_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_Z16_UNORM:
      return BRW_DEPTHFORMAT_D16_UNORM;
   case PIPE_FORMAT_Z32_FLOAT:
      return BRW_DEPTHFORMAT_D32_FLOAT;
   case PIPE_FORMAT_Z24X8_UNORM:
      return BRW_DEPTHFORMAT_D24_UNORM_X8_UINT;
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      return BRW_DEPTHFORMAT_D24_UNORM_S8_UINT;
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      return BRW_DEPTHFORMAT_D32_FLOAT_S8X24_UINT;
   default:
      return -1;
   }
}

/**
 * Translate a color pipe format to a hardware surface format suitable for
 * rendering.  Return -1 on errors.
 */
int
i965_translate_render_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_B8G8R8X8_UNORM:
      return BRW_SURFACEFORMAT_B8G8R8A8_UNORM;
   default:
      return i965_translate_color_format(format);
   }
}

/**
 * Translate a pipe format to a hardware surface format suitable for
 * texturing.  Return -1 on errors.
 */
int
i965_translate_texture_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_Z16_UNORM:
      return BRW_SURFACEFORMAT_I16_UNORM;
   case PIPE_FORMAT_Z32_FLOAT:
      return BRW_SURFACEFORMAT_I32_FLOAT;
   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      return BRW_SURFACEFORMAT_I24X8_UNORM;
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      return BRW_SURFACEFORMAT_R32G32_FLOAT;
   default:
      return i965_translate_color_format(format);
   }
}

/**
 * Translate a pipe format to a hardware surface format suitable for
 * use with vertex elements.  Return -1 on errors.
 */
int
i965_translate_vertex_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R16G16B16_FLOAT:
      return BRW_SURFACEFORMAT_R16G16B16A16_FLOAT;
   case PIPE_FORMAT_R16G16B16_UINT:
      return BRW_SURFACEFORMAT_R16G16B16A16_UINT;
   case PIPE_FORMAT_R16G16B16_SINT:
      return BRW_SURFACEFORMAT_R16G16B16A16_SINT;
   case PIPE_FORMAT_R8G8B8_UINT:
      return BRW_SURFACEFORMAT_R8G8B8A8_UINT;
   case PIPE_FORMAT_R8G8B8_SINT:
      return BRW_SURFACEFORMAT_R8G8B8A8_SINT;
   default:
      return i965_translate_color_format(format);
   }
}

/**
 * Translate a pipe primitive type to the matching hardware primitive type.
 */
int
i965_translate_pipe_prim(unsigned prim)
{
   static const int prim_mapping[PIPE_PRIM_MAX] = {
      [PIPE_PRIM_POINTS]                     = _3DPRIM_POINTLIST,
      [PIPE_PRIM_LINES]                      = _3DPRIM_LINELIST,
      [PIPE_PRIM_LINE_LOOP]                  = _3DPRIM_LINELOOP,
      [PIPE_PRIM_LINE_STRIP]                 = _3DPRIM_LINESTRIP,
      [PIPE_PRIM_TRIANGLES]                  = _3DPRIM_TRILIST,
      [PIPE_PRIM_TRIANGLE_STRIP]             = _3DPRIM_TRISTRIP,
      [PIPE_PRIM_TRIANGLE_FAN]               = _3DPRIM_TRIFAN,
      [PIPE_PRIM_QUADS]                      = _3DPRIM_QUADLIST,
      [PIPE_PRIM_QUAD_STRIP]                 = _3DPRIM_QUADSTRIP,
      [PIPE_PRIM_POLYGON]                    = _3DPRIM_POLYGON,
      [PIPE_PRIM_LINES_ADJACENCY]            = _3DPRIM_LINELIST_ADJ,
      [PIPE_PRIM_LINE_STRIP_ADJACENCY]       = _3DPRIM_LINESTRIP_ADJ,
      [PIPE_PRIM_TRIANGLES_ADJACENCY]        = _3DPRIM_TRILIST_ADJ,
      [PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY]   = _3DPRIM_TRISTRIP_ADJ,
   };

   assert(prim_mapping[prim]);

   return prim_mapping[prim];
}

/**
 * Translate a pipe logicop to the matching hardware logicop.
 */
int
i965_translate_pipe_logicop(unsigned logicop)
{
   switch (logicop) {
   case PIPE_LOGICOP_CLEAR:         return BRW_LOGICOPFUNCTION_CLEAR;
   case PIPE_LOGICOP_NOR:           return BRW_LOGICOPFUNCTION_NOR;
   case PIPE_LOGICOP_AND_INVERTED:  return BRW_LOGICOPFUNCTION_AND_INVERTED;
   case PIPE_LOGICOP_COPY_INVERTED: return BRW_LOGICOPFUNCTION_COPY_INVERTED;
   case PIPE_LOGICOP_AND_REVERSE:   return BRW_LOGICOPFUNCTION_AND_REVERSE;
   case PIPE_LOGICOP_INVERT:        return BRW_LOGICOPFUNCTION_INVERT;
   case PIPE_LOGICOP_XOR:           return BRW_LOGICOPFUNCTION_XOR;
   case PIPE_LOGICOP_NAND:          return BRW_LOGICOPFUNCTION_NAND;
   case PIPE_LOGICOP_AND:           return BRW_LOGICOPFUNCTION_AND;
   case PIPE_LOGICOP_EQUIV:         return BRW_LOGICOPFUNCTION_EQUIV;
   case PIPE_LOGICOP_NOOP:          return BRW_LOGICOPFUNCTION_NOOP;
   case PIPE_LOGICOP_OR_INVERTED:   return BRW_LOGICOPFUNCTION_OR_INVERTED;
   case PIPE_LOGICOP_COPY:          return BRW_LOGICOPFUNCTION_COPY;
   case PIPE_LOGICOP_OR_REVERSE:    return BRW_LOGICOPFUNCTION_OR_REVERSE;
   case PIPE_LOGICOP_OR:            return BRW_LOGICOPFUNCTION_OR;
   case PIPE_LOGICOP_SET:           return BRW_LOGICOPFUNCTION_SET;
   default:
      assert(!"unknown logicop function");
      return BRW_LOGICOPFUNCTION_CLEAR;
   }
}

/**
 * Translate a pipe blend function to the matching hardware blend function.
 */
int
i965_translate_pipe_blend(unsigned blend)
{
   switch (blend) {
   case PIPE_BLEND_ADD:                return BRW_BLENDFUNCTION_ADD;
   case PIPE_BLEND_SUBTRACT:           return BRW_BLENDFUNCTION_SUBTRACT;
   case PIPE_BLEND_REVERSE_SUBTRACT:   return BRW_BLENDFUNCTION_REVERSE_SUBTRACT;
   case PIPE_BLEND_MIN:                return BRW_BLENDFUNCTION_MIN;
   case PIPE_BLEND_MAX:                return BRW_BLENDFUNCTION_MAX;
   default:
      assert(!"unknown blend function");
      return BRW_BLENDFUNCTION_ADD;
   };
}

/**
 * Translate a pipe blend factor to the matching hardware blend factor.
 */
int
i965_translate_pipe_blendfactor(unsigned blendfactor)
{
   switch (blendfactor) {
   case PIPE_BLENDFACTOR_ONE:                return BRW_BLENDFACTOR_ONE;
   case PIPE_BLENDFACTOR_SRC_COLOR:          return BRW_BLENDFACTOR_SRC_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA:          return BRW_BLENDFACTOR_SRC_ALPHA;
   case PIPE_BLENDFACTOR_DST_ALPHA:          return BRW_BLENDFACTOR_DST_ALPHA;
   case PIPE_BLENDFACTOR_DST_COLOR:          return BRW_BLENDFACTOR_DST_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE: return BRW_BLENDFACTOR_SRC_ALPHA_SATURATE;
   case PIPE_BLENDFACTOR_CONST_COLOR:        return BRW_BLENDFACTOR_CONST_COLOR;
   case PIPE_BLENDFACTOR_CONST_ALPHA:        return BRW_BLENDFACTOR_CONST_ALPHA;
   case PIPE_BLENDFACTOR_SRC1_COLOR:         return BRW_BLENDFACTOR_SRC1_COLOR;
   case PIPE_BLENDFACTOR_SRC1_ALPHA:         return BRW_BLENDFACTOR_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_ZERO:               return BRW_BLENDFACTOR_ZERO;
   case PIPE_BLENDFACTOR_INV_SRC_COLOR:      return BRW_BLENDFACTOR_INV_SRC_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA:      return BRW_BLENDFACTOR_INV_SRC_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA:      return BRW_BLENDFACTOR_INV_DST_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_COLOR:      return BRW_BLENDFACTOR_INV_DST_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_COLOR:    return BRW_BLENDFACTOR_INV_CONST_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA:    return BRW_BLENDFACTOR_INV_CONST_ALPHA;
   case PIPE_BLENDFACTOR_INV_SRC1_COLOR:     return BRW_BLENDFACTOR_INV_SRC1_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:     return BRW_BLENDFACTOR_INV_SRC1_ALPHA;
   default:
      assert(!"unknown blend factor");
      return BRW_BLENDFACTOR_ONE;
   };
}

/**
 * Translate a pipe stencil op to the matching hardware stencil op.
 */
int
i965_translate_pipe_stencil_op(unsigned stencil_op)
{
   switch (stencil_op) {
   case PIPE_STENCIL_OP_KEEP:       return BRW_STENCILOP_KEEP;
   case PIPE_STENCIL_OP_ZERO:       return BRW_STENCILOP_ZERO;
   case PIPE_STENCIL_OP_REPLACE:    return BRW_STENCILOP_REPLACE;
   case PIPE_STENCIL_OP_INCR:       return BRW_STENCILOP_INCRSAT;
   case PIPE_STENCIL_OP_DECR:       return BRW_STENCILOP_DECRSAT;
   case PIPE_STENCIL_OP_INCR_WRAP:  return BRW_STENCILOP_INCR;
   case PIPE_STENCIL_OP_DECR_WRAP:  return BRW_STENCILOP_DECR;
   case PIPE_STENCIL_OP_INVERT:     return BRW_STENCILOP_INVERT;
   default:
      assert(!"unknown stencil op");
      return BRW_STENCILOP_KEEP;
   }
}

/**
 * Translate a pipe texture target to the matching hardware surface type.
 */
int
i965_translate_texture(enum pipe_texture_target target)
{
   switch (target) {
   case PIPE_BUFFER:
      return BRW_SURFACE_BUFFER;
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      return BRW_SURFACE_1D;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D_ARRAY:
      return BRW_SURFACE_2D;
   case PIPE_TEXTURE_3D:
      return BRW_SURFACE_3D;
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return BRW_SURFACE_CUBE;
   default:
      assert(!"unknown texture target");
      return BRW_SURFACE_BUFFER;
   }
}

/**
 * Translate a pipe texture mipfilter to the matching hardware mipfilter.
 */
int
i965_translate_tex_mipfilter(unsigned filter)
{
   switch (filter) {
   case PIPE_TEX_MIPFILTER_NEAREST: return BRW_MIPFILTER_NEAREST;
   case PIPE_TEX_MIPFILTER_LINEAR:  return BRW_MIPFILTER_LINEAR;
   case PIPE_TEX_MIPFILTER_NONE:    return BRW_MIPFILTER_NONE;
   default:
      assert(!"unknown mipfilter");
      return BRW_MIPFILTER_NONE;
   }
}

/**
 * Translate a pipe texture filter to the matching hardware mapfilter.
 */
int
i965_translate_tex_filter(unsigned filter)
{
   switch (filter) {
   case PIPE_TEX_FILTER_NEAREST: return BRW_MAPFILTER_NEAREST;
   case PIPE_TEX_FILTER_LINEAR:  return BRW_MAPFILTER_LINEAR;
   default:
      assert(!"unknown sampler filter");
      return BRW_MAPFILTER_NEAREST;
   }
}

/**
 * Translate a pipe texture coordinate wrapping mode to the matching hardware
 * wrapping mode.
 */
int
i965_translate_tex_wrap(unsigned wrap, boolean clamp_to_edge)
{
   /* clamp to edge or border? */
   if (wrap == PIPE_TEX_WRAP_CLAMP) {
      wrap = (clamp_to_edge) ?
         PIPE_TEX_WRAP_CLAMP_TO_EDGE : PIPE_TEX_WRAP_CLAMP_TO_BORDER;
   }

   switch (wrap) {
   case PIPE_TEX_WRAP_REPEAT:             return BRW_TEXCOORDMODE_WRAP;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:      return BRW_TEXCOORDMODE_CLAMP;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:    return BRW_TEXCOORDMODE_CLAMP_BORDER;
   case PIPE_TEX_WRAP_MIRROR_REPEAT:      return BRW_TEXCOORDMODE_MIRROR;
   case PIPE_TEX_WRAP_CLAMP:
   case PIPE_TEX_WRAP_MIRROR_CLAMP:
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
   default:
      assert(!"unknown sampler wrap mode");
      return BRW_TEXCOORDMODE_WRAP;
   }
}

/**
 * Translate a pipe DSA test function to the matching hardware compare
 * function.
 */
int
i965_translate_dsa_func(unsigned func)
{
   switch (func) {
   case PIPE_FUNC_NEVER:      return BRW_COMPAREFUNCTION_NEVER;
   case PIPE_FUNC_LESS:       return BRW_COMPAREFUNCTION_LESS;
   case PIPE_FUNC_EQUAL:      return BRW_COMPAREFUNCTION_EQUAL;
   case PIPE_FUNC_LEQUAL:     return BRW_COMPAREFUNCTION_LEQUAL;
   case PIPE_FUNC_GREATER:    return BRW_COMPAREFUNCTION_GREATER;
   case PIPE_FUNC_NOTEQUAL:   return BRW_COMPAREFUNCTION_NOTEQUAL;
   case PIPE_FUNC_GEQUAL:     return BRW_COMPAREFUNCTION_GEQUAL;
   case PIPE_FUNC_ALWAYS:     return BRW_COMPAREFUNCTION_ALWAYS;
   default:
      assert(!"unknown depth/stencil/alpha test function");
      return BRW_COMPAREFUNCTION_NEVER;
   }
}

/**
 * Translate a pipe shadow compare function to the matching hardware shadow
 * function.
 */
int
i965_translate_shadow_func(unsigned func)
{
   /*
    * For PIPE_FUNC_x, the reference value is on the left-hand side of the
    * comparison, and 1.0 is returned when the comparison is true.
    *
    * For BRW_PREFILTER_x, the reference value is on the right-hand side of
    * the comparison, and 0.0 is returned when the comparison is true.
    */
   switch (func) {
   case PIPE_FUNC_NEVER:      return BRW_PREFILTER_ALWAYS;
   case PIPE_FUNC_LESS:       return BRW_PREFILTER_LEQUAL;
   case PIPE_FUNC_EQUAL:      return BRW_PREFILTER_NOTEQUAL;
   case PIPE_FUNC_LEQUAL:     return BRW_PREFILTER_LESS;
   case PIPE_FUNC_GREATER:    return BRW_PREFILTER_GEQUAL;
   case PIPE_FUNC_NOTEQUAL:   return BRW_PREFILTER_EQUAL;
   case PIPE_FUNC_GEQUAL:     return BRW_PREFILTER_GREATER;
   case PIPE_FUNC_ALWAYS:     return BRW_PREFILTER_NEVER;
   default:
      assert(!"unknown shadow compare function");
      return BRW_PREFILTER_NEVER;
   }
}

/**
 * Translate an index size to the matching hardware index format.
 */
int
i965_translate_index_size(int size)
{
   switch (size) {
   case 4: return BRW_INDEX_DWORD;
   case 2: return BRW_INDEX_WORD;
   case 1: return BRW_INDEX_BYTE;
   default:
      assert(!"unknown index size");
      return BRW_INDEX_BYTE;
   }
}
