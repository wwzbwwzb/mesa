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

#ifndef I965_CONTEXT_H
#define I965_CONTEXT_H

#include "pipe/p_context.h"
#include "i965_common.h"

struct intel_winsys;
struct i965_screen;

struct i965_context {
   struct pipe_context base;

   struct intel_winsys *winsys;
   int devid;
   int gen;
   int gt;

   int max_vs_threads;
   int max_gs_threads;
   int max_wm_threads;
   struct {
      int size;
      int max_vs_entries;
      int max_gs_entries;
   } urb;
};

static INLINE struct i965_context *
i965_context(struct pipe_context *pipe)
{
   return (struct i965_context *) pipe;
}

void
i965_init_context_functions(struct i965_screen *is);

#endif /* I965_CONTEXT_H */
