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

#include "vl/vl_video_buffer.h"
#include "i965_common.h"
#include "i965_screen.h"
#include "i965_translate.h"
#include "i965_format.h"

/* steal from classic i965 */
struct surface_format_info {
   bool exists;
   int sampling;
   int filtering;
   int shadow_compare;
   int chroma_key;
   int render_target;
   int alpha_blend;
   int input_vb;
   int streamed_output_vb;
   int color_processing;
};

/* This macro allows us to write the table almost as it appears in the PRM,
 * while restructuring it to turn it into the C code we want.
 */
#define SF(sampl, filt, shad, ck, rt, ab, vb, so, color, sf) \
   [sf] = { true, sampl, filt, shad, ck, rt, ab, vb, so, color },

#define Y 0
#define x 999
/**
 * This is the table of support for surface (texture, renderbuffer, and vertex
 * buffer, but not depthbuffer) formats across the various hardware generations.
 *
 * The table is formatted to match the documentation, except that the docs have
 * this ridiculous mapping of Y[*+~^#&] for "supported on DevWhatever".  To put
 * it in our table, here's the mapping:
 *
 * Y*: 45
 * Y+: 45 (g45/gm45)
 * Y~: 50 (gen5)
 * Y^: 60 (gen6)
 * Y#: 70 (gen7)
 *
 * The abbreviations in the header below are:
 * smpl  - Sampling Engine
 * filt  - Sampling Engine Filtering
 * shad  - Sampling Engine Shadow Map
 * CK    - Sampling Engine Chroma Key
 * RT    - Render Target
 * AB    - Alpha Blend Render Target
 * VB    - Input Vertex Buffer
 * SO    - Steamed Output Vertex Buffers (transform feedback)
 * color - Color Processing
 *
 * See page 88 of the Sandybridge PRM VOL4_Part1 PDF.
 */
static const struct surface_format_info surface_formats[] = {
/* smpl filt shad CK  RT  AB  VB  SO  color */
   SF( Y, 50,  x,  x,  Y,  Y,  Y,  Y,  x, BRW_SURFACEFORMAT_R32G32B32A32_FLOAT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x, BRW_SURFACEFORMAT_R32G32B32A32_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x, BRW_SURFACEFORMAT_R32G32B32A32_UINT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32B32A32_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32B32A32_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R64G64_FLOAT)
   SF( Y, 50,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_R32G32B32X32_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32B32A32_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32B32A32_USCALED)
   SF( Y, 50,  x,  x,  x,  x,  Y,  Y,  x, BRW_SURFACEFORMAT_R32G32B32_FLOAT)
   SF( Y,  x,  x,  x,  x,  x,  Y,  Y,  x, BRW_SURFACEFORMAT_R32G32B32_SINT)
   SF( Y,  x,  x,  x,  x,  x,  Y,  Y,  x, BRW_SURFACEFORMAT_R32G32B32_UINT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32B32_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32B32_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32B32_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32B32_USCALED)
   SF( Y,  Y,  x,  x,  Y, 45,  Y,  x, 60, BRW_SURFACEFORMAT_R16G16B16A16_UNORM)
   SF( Y,  Y,  x,  x,  Y, 60,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16B16A16_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16B16A16_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16B16A16_UINT)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16B16A16_FLOAT)
   SF( Y, 50,  x,  x,  Y,  Y,  Y,  Y,  x, BRW_SURFACEFORMAT_R32G32_FLOAT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x, BRW_SURFACEFORMAT_R32G32_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x, BRW_SURFACEFORMAT_R32G32_UINT)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_R32_FLOAT_X8X24_TYPELESS)
   SF( Y,  x,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_X32_TYPELESS_G8X24_UINT)
   SF( Y, 50,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L32A32_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R64_FLOAT)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_R16G16B16X16_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_R16G16B16X16_FLOAT)
   SF( Y, 50,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_A32X32_FLOAT)
   SF( Y, 50,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L32X32_FLOAT)
   SF( Y, 50,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_I32X32_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16B16A16_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16B16A16_USCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32G32_USCALED)
   SF( Y,  Y,  x,  Y,  Y,  Y,  Y,  x, 60, BRW_SURFACEFORMAT_B8G8R8A8_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x,  x, BRW_SURFACEFORMAT_B8G8R8A8_UNORM_SRGB)
/* smpl filt shad CK  RT  AB  VB  SO  color */
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x, 60, BRW_SURFACEFORMAT_R10G10B10A2_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x, 60, BRW_SURFACEFORMAT_R10G10B10A2_UNORM_SRGB)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R10G10B10A2_UINT)
   SF( Y,  Y,  x,  x,  x,  Y,  Y,  x,  x, BRW_SURFACEFORMAT_R10G10B10_SNORM_A2_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x, 60, BRW_SURFACEFORMAT_R8G8B8A8_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x, 60, BRW_SURFACEFORMAT_R8G8B8A8_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  Y, 60,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8B8A8_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8B8A8_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8B8A8_UINT)
   SF( Y,  Y,  x,  x,  Y, 45,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16_UNORM)
   SF( Y,  Y,  x,  x,  Y, 60,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16_UINT)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16_FLOAT)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x, 60, BRW_SURFACEFORMAT_B10G10R10A2_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x, 60, BRW_SURFACEFORMAT_B10G10R10A2_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x,  x, BRW_SURFACEFORMAT_R11G11B10_FLOAT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x, BRW_SURFACEFORMAT_R32_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x, BRW_SURFACEFORMAT_R32_UINT)
   SF( Y, 50,  Y,  x,  Y,  Y,  Y,  Y,  x, BRW_SURFACEFORMAT_R32_FLOAT)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_R24_UNORM_X8_TYPELESS)
   SF( Y,  x,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_X24_TYPELESS_G8_UINT)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L16A16_UNORM)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_I24X8_UNORM)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L24X8_UNORM)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_A24X8_UNORM)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_I32_FLOAT)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L32_FLOAT)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_A32_FLOAT)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x, 60, BRW_SURFACEFORMAT_B8G8R8X8_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_B8G8R8X8_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_R8G8B8X8_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_R8G8B8X8_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_R9G9B9E5_SHAREDEXP)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_B10G10R10X2_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L16A16_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32_SNORM)
/* smpl filt shad CK  RT  AB  VB  SO  color */
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R10G10B10X2_USCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8B8A8_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8B8A8_USCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16_USCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R32_USCALED)
   SF( Y,  Y,  x,  Y,  Y,  Y,  x,  x,  x, BRW_SURFACEFORMAT_B5G6R5_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x,  x, BRW_SURFACEFORMAT_B5G6R5_UNORM_SRGB)
   SF( Y,  Y,  x,  Y,  Y,  Y,  x,  x,  x, BRW_SURFACEFORMAT_B5G5R5A1_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x,  x, BRW_SURFACEFORMAT_B5G5R5A1_UNORM_SRGB)
   SF( Y,  Y,  x,  Y,  Y,  Y,  x,  x,  x, BRW_SURFACEFORMAT_B4G4R4A4_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x,  x, BRW_SURFACEFORMAT_B4G4R4A4_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8_UNORM)
   SF( Y,  Y,  x,  Y,  Y, 60,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8_UINT)
   SF( Y,  Y,  Y,  x,  Y, 45,  Y,  x, 70, BRW_SURFACEFORMAT_R16_UNORM)
   SF( Y,  Y,  x,  x,  Y, 60,  Y,  x,  x, BRW_SURFACEFORMAT_R16_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16_UINT)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x,  x, BRW_SURFACEFORMAT_R16_FLOAT)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_I16_UNORM)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L16_UNORM)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_A16_UNORM)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L8A8_UNORM)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_I16_FLOAT)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L16_FLOAT)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_A16_FLOAT)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L8A8_UNORM_SRGB)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_R5G5_SNORM_B6_UNORM)
   SF( x,  x,  x,  x,  Y,  Y,  x,  x,  x, BRW_SURFACEFORMAT_B5G5R5X1_UNORM)
   SF( x,  x,  x,  x,  Y,  Y,  x,  x,  x, BRW_SURFACEFORMAT_B5G5R5X1_UNORM_SRGB)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8_USCALED)
/* smpl filt shad CK  RT  AB  VB  SO  color */
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16_USCALED)
   SF( Y,  Y,  x, 45,  Y,  Y,  Y,  x,  x, BRW_SURFACEFORMAT_R8_UNORM)
   SF( Y,  Y,  x,  x,  Y, 60,  Y,  x,  x, BRW_SURFACEFORMAT_R8_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8_UINT)
   SF( Y,  Y,  x,  Y,  Y,  Y,  x,  x,  x, BRW_SURFACEFORMAT_A8_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_I8_UNORM)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L8_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_P4A4_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_A4P4_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8_USCALED)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_L8_UNORM_SRGB)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_DXT1_RGB_SRGB)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_R1_UINT)
   SF( Y,  Y,  x,  Y,  Y,  x,  x,  x, 60, BRW_SURFACEFORMAT_YCRCB_NORMAL)
   SF( Y,  Y,  x,  Y,  Y,  x,  x,  x, 60, BRW_SURFACEFORMAT_YCRCB_SWAPUVY)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_BC1_UNORM)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_BC2_UNORM)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_BC3_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_BC4_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_BC5_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_BC1_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_BC2_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_BC3_UNORM_SRGB)
   SF( Y,  x,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_MONO8)
   SF( Y,  Y,  x,  x,  Y,  x,  x,  x, 60, BRW_SURFACEFORMAT_YCRCB_SWAPUV)
   SF( Y,  Y,  x,  x,  Y,  x,  x,  x, 60, BRW_SURFACEFORMAT_YCRCB_SWAPY)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_DXT1_RGB)
/* smpl filt shad CK  RT  AB  VB  SO  color */
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_FXT1)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8B8_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8B8_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8B8_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R8G8B8_USCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R64G64B64A64_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R64G64B64_FLOAT)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_BC4_SNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x, BRW_SURFACEFORMAT_BC5_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16B16_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16B16_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16B16_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x, BRW_SURFACEFORMAT_R16G16B16_USCALED)
};
#undef x
#undef Y

static boolean
i965_is_format_supported(struct pipe_screen *screen,
                         enum pipe_format format,
                         enum pipe_texture_target target,
                         unsigned sample_count,
                         unsigned bindings)
{
   struct i965_screen *is = i965_screen(screen);
   const struct surface_format_info *info;
   int sfmt, gen;

   if (!util_format_is_supported(format, bindings))
      return FALSE;

   /* no MSAA support yet */
   if (sample_count > 1)
      return FALSE;

   if (util_format_is_depth_or_stencil(format)) {
      if (bindings & ~(PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_SAMPLER_VIEW))
         return FALSE;

      if (bindings & PIPE_BIND_DEPTH_STENCIL) {
         if (i965_translate_depth_format(format) < 0)
            return FALSE;
      }
      if (bindings & PIPE_BIND_SAMPLER_VIEW) {
         if (i965_translate_texture_format(format) < 0)
            return FALSE;
      }

      return TRUE;
   }

   sfmt = i965_translate_color_format(format);
   if (sfmt < 0)
      return FALSE;

   gen = is->gen * 10;

   info = &surface_formats[sfmt];

   if (bindings & PIPE_BIND_SAMPLER_VIEW) {
      if (gen < info->sampling || gen < info->filtering)
         return FALSE;
   }

   if (bindings & PIPE_BIND_RENDER_TARGET) {
      switch (sfmt) {
      case BRW_SURFACEFORMAT_B8G8R8X8_UNORM:
         /* we will treat it as BRW_SURFACEFORMAT_B8G8R8A8_UNORM */
         break;
      default:
         if (gen < info->render_target || gen < info->alpha_blend)
            return FALSE;
         break;
      }
   }

   if (bindings & PIPE_BIND_VERTEX_BUFFER) {
      if (gen < info->input_vb)
         return FALSE;
   }

   return TRUE;
}

static boolean
i965_is_video_format_supported(struct pipe_screen *screen,
                               enum pipe_format format,
                               enum pipe_video_profile profile)
{
   return vl_video_buffer_is_format_supported(screen, format, profile);
}

/**
 * Initialize format-related functions.
 */
void
i965_init_format_functions(struct i965_screen *is)
{
   is->base.is_format_supported = i965_is_format_supported;
   is->base.is_video_format_supported = i965_is_video_format_supported;
}
