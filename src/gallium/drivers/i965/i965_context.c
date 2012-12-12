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

#include "util/u_blitter.h"
#include "i965_shader.h"
#include "i965_common.h"
#include "i965_screen.h"
#include "i965_cp.h"
#include "i965_blit.h"
#include "i965_resource.h"
#include "i965_3d.h"
#include "i965_gpgpu.h"
#include "i965_query.h"
#include "i965_state.h"
#include "i965_video.h"
#include "i965_context.h"

static void
i965_context_new_cp_batch(struct i965_cp *cp, void *data)
{
}

static void
i965_context_pre_cp_flush(struct i965_cp *cp, void *data)
{
}

static void
i965_context_post_cp_flush(struct i965_cp *cp, void *data)
{
   struct i965_context *i965 = i965_context(data);

   if (i965->last_cp_bo)
      i965->last_cp_bo->unreference(i965->last_cp_bo);

   /* remember the just flushed bo, which fences could wait on */
   i965->last_cp_bo = cp->bo;
   i965->last_cp_bo->reference(i965->last_cp_bo);
}

static void
i965_flush(struct pipe_context *pipe,
           struct pipe_fence_handle **f)
{
   struct i965_context *i965 = i965_context(pipe);

   if (f) {
      struct i965_fence *fence;

      fence = CALLOC_STRUCT(i965_fence);
      if (fence) {
         pipe_reference_init(&fence->reference, 1);

         /* reference the batch bo that we want to wait on */
         if (i965_cp_empty(i965->cp))
            fence->bo = i965->last_cp_bo;
         else
            fence->bo = i965->cp->bo;

         if (fence->bo)
            fence->bo->reference(fence->bo);
      }

      *f = (struct pipe_fence_handle *) fence;
   }

   i965_cp_flush(i965->cp);
}

static void
i965_context_destroy(struct pipe_context *pipe)
{
   struct i965_context *i965 = i965_context(pipe);

   if (i965->last_cp_bo)
      i965->last_cp_bo->unreference(i965->last_cp_bo);

   if (i965->blitter)
      util_blitter_destroy(i965->blitter);
   if (i965->shader_cache)
      i965_shader_cache_destroy(i965->shader_cache);
   if (i965->cp)
      i965_cp_destroy(i965->cp);

   FREE(i965);
}

/**
 * \see brwCreateContext()
 */
static struct pipe_context *
i965_context_create(struct pipe_screen *screen, void *priv)
{
   struct i965_screen *is = i965_screen(screen);
   struct i965_context *i965;

   i965 = CALLOC_STRUCT(i965_context);
   if (!i965)
      return NULL;

   i965->winsys = is->winsys;
   i965->devid = is->devid;
   i965->gen = is->gen;

   if (IS_SNB_GT1(i965->devid) ||
       IS_IVB_GT1(i965->devid) ||
       IS_HSW_GT1(i965->devid))
      i965->gt = 1;
   else if (IS_SNB_GT2(i965->devid) ||
            IS_IVB_GT2(i965->devid) ||
            IS_HSW_GT2(i965->devid))
      i965->gt = 2;
   else
      i965->gt = 0;

   /* steal from classic i965 */
   /* WM maximum threads is number of EUs times number of threads per EU. */
   if (i965->gen >= 7) {
      if (i965->gt == 1) {
	 i965->max_wm_threads = 48;
	 i965->max_vs_threads = 36;
	 i965->max_gs_threads = 36;
	 i965->urb.size = 128;
	 i965->urb.max_vs_entries = 512;
	 i965->urb.max_gs_entries = 192;
      } else if (i965->gt == 2) {
	 i965->max_wm_threads = 172;
	 i965->max_vs_threads = 128;
	 i965->max_gs_threads = 128;
	 i965->urb.size = 256;
	 i965->urb.max_vs_entries = 704;
	 i965->urb.max_gs_entries = 320;
      } else {
	 assert(!"Unknown gen7 device.");
      }
   } else if (i965->gen == 6) {
      if (i965->gt == 2) {
	 i965->max_wm_threads = 80;
	 i965->max_vs_threads = 60;
	 i965->max_gs_threads = 60;
	 i965->urb.size = 64;            /* volume 5c.5 section 5.1 */
	 i965->urb.max_vs_entries = 256; /* volume 2a (see 3DSTATE_URB) */
	 i965->urb.max_gs_entries = 256;
      } else {
	 i965->max_wm_threads = 40;
	 i965->max_vs_threads = 24;
	 i965->max_gs_threads = 21; /* conservative; 24 if rendering disabled */
	 i965->urb.size = 32;            /* volume 5c.5 section 5.1 */
	 i965->urb.max_vs_entries = 256; /* volume 2a (see 3DSTATE_URB) */
	 i965->urb.max_gs_entries = 256;
      }
   }

   i965->cp = i965_cp_create(i965->winsys);
   i965->shader_cache = i965_shader_cache_create(i965->winsys);

   if (!i965->cp || !i965->shader_cache) {
      i965_context_destroy(&i965->base);
      return NULL;
   }

   i965_cp_set_hook(i965->cp, I965_CP_HOOK_NEW_BATCH,
         i965_context_new_cp_batch, (void *) i965);
   i965_cp_set_hook(i965->cp, I965_CP_HOOK_PRE_FLUSH,
         i965_context_pre_cp_flush, (void *) i965);
   i965_cp_set_hook(i965->cp, I965_CP_HOOK_POST_FLUSH,
         i965_context_post_cp_flush, (void *) i965);

   i965->dirty = I965_DIRTY_ALL;

   i965->base.screen = screen;
   i965->base.priv = priv;

   i965->base.destroy = i965_context_destroy;
   i965->base.flush = i965_flush;

   i965_init_3d_functions(i965);
   i965_init_query_functions(i965);
   i965_init_state_functions(i965);
   i965_init_blit_functions(i965);
   i965_init_transfer_functions(i965);
   i965_init_video_functions(i965);
   i965_init_gpgpu_functions(i965);

   /* this must be called last as u_blitter is a client of the pipe context */
   i965->blitter = util_blitter_create(&i965->base);
   if (!i965->blitter) {
      i965_context_destroy(&i965->base);
      return NULL;
   }

   return &i965->base;
}

/**
 * Initialize context-related functions.
 */
void
i965_init_context_functions(struct i965_screen *is)
{
   is->base.context_create = i965_context_create;
}
