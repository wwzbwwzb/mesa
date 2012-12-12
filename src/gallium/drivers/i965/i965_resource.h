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

#ifndef I965_RESOURCE_H
#define I965_RESOURCE_H

#include "pipe/p_state.h"
#include "intel_winsys.h"

struct i965_screen;
struct i965_context;
struct winsys_handle;

struct i965_resource {
   struct pipe_resource base;
   struct winsys_handle *handle;

   boolean compressed;
   unsigned block_width;
   unsigned block_height;
   boolean valign_4;

   struct intel_bo *bo;
   /* in blocks */
   int bo_width, bo_height, bo_cpp, bo_stride;
   enum intel_tiling_mode tiling;

   /* 2D offsets into a layer/slice/face */
   struct {
      unsigned x;
      unsigned y;
   } *slice_offsets[PIPE_MAX_TEXTURE_LEVELS];
};

static INLINE struct i965_resource *
i965_resource(struct pipe_resource *res)
{
   return (struct i965_resource *) res;
}

void
i965_init_resource_functions(struct i965_screen *is);

void
i965_init_transfer_functions(struct i965_context *i965);

unsigned
i965_resource_get_slice_offset(const struct i965_resource *res,
                               int level, int slice, boolean tile_aligned,
                               unsigned *x_offset, unsigned *y_offset);

#endif /* I965_RESOURCE_H */
