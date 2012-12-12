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

#include "intel_winsys.h"

#include "i965_common.h"
#include "i965_context.h"
#include "i965_cp.h"
#include "i965_shader.h"
#include "i965_state.h"
#include "i965_3d_gen6.h"
#include "i965_3d.h"

/**
 * Hook for CP new-batch.
 */
void
i965_3d_new_cp_batch(struct i965_3d *hw3d)
{
   hw3d->new_batch = TRUE;
}

/**
 * Hook for CP pre-flush.
 */
void
i965_3d_pre_cp_flush(struct i965_3d *hw3d)
{
}

/**
 * Hook for CP post-flush
 */
void
i965_3d_post_cp_flush(struct i965_3d *hw3d)
{
   if (i965_debug & I965_DEBUG_3D) {
      i965_cp_dump(hw3d->cp);
      hw3d->dump(hw3d);
   }
}

/**
 * Create a 3D context.
 */
struct i965_3d *
i965_3d_create(struct i965_cp *cp, int gen)
{
   struct i965_3d *hw3d;

   hw3d = CALLOC_STRUCT(i965_3d);
   if (!hw3d)
      return NULL;

   hw3d->cp = cp;
   hw3d->gen = gen;
   hw3d->new_batch = TRUE;

   switch (gen) {
   case 6:
      i965_3d_init_gen6(hw3d);
      break;
   default:
      assert(!"unsupported GEN");
      break;
   }

   hw3d->workaround_bo = cp->winsys->alloc(cp->winsys,
         "PIPE_CONTROL workaround", 4096, 4096);
   if (!hw3d->workaround_bo) {
      debug_printf("failed to allocate PIPE_CONTROL workaround bo\n");
      FREE(hw3d);
      return NULL;
   }

   return hw3d;
}

/**
 * Destroy a 3D context.
 */
void
i965_3d_destroy(struct i965_3d *hw3d)
{
   hw3d->workaround_bo->unreference(hw3d->workaround_bo);
   FREE(hw3d);
}

static int
upload_states(struct i965_3d *hw3d, const struct i965_context *i965,
              const struct pipe_draw_info *info, boolean dry_run)
{
   int size = 0;

   /*
    * Without a better tracking mechanism, when the framebuffer changes, we
    * have to assume that the old framebuffer may be sampled from.  If that
    * happens in the middle of a batch buffer, we need to insert manual
    * flushes.
    */
   if (!hw3d->new_batch) {
      if (i965->dirty & I965_DIRTY_FRAMEBUFFER)
         size += hw3d->flush(hw3d, dry_run);
   }

   size += hw3d->upload_context(hw3d, i965, dry_run);
   size += hw3d->draw(hw3d, info, dry_run);

   return size;
}

static boolean
draw_vbo(struct i965_3d *hw3d, const struct i965_context *i965,
         const struct pipe_draw_info *info)
{
   boolean success;
   int max_len;

   i965_cp_set_ring(hw3d->cp, INTEL_RING_RENDER);

   /* make sure there is enough room first */
   max_len = upload_states(hw3d, i965, info, TRUE);
   if (max_len > i965_cp_space(hw3d->cp)) {
      i965_cp_flush(hw3d->cp);
      assert(max_len <= i965_cp_space(hw3d->cp));
   }

   while (TRUE) {
      struct i965_cp_jmp_buf jmp;
      int err;

      /* we will rewind if aperture check below fails */
      i965_cp_setjmp(hw3d->cp, &jmp);

      /* draw! */
      i965_cp_assert_no_implicit_flush(hw3d->cp, TRUE);
      upload_states(hw3d, i965, info, FALSE);
      i965_cp_assert_no_implicit_flush(hw3d->cp, FALSE);

      err = i965->winsys->check_aperture_space(i965->winsys, &hw3d->cp->bo, 1);
      if (!err) {
         success = TRUE;
         break;
      }

      /* rewind */
      i965_cp_longjmp(hw3d->cp, &jmp);

      if (i965_cp_empty(hw3d->cp)) {
         success = FALSE;
         break;
      }
      else {
         /* flush and try again */
         i965_cp_flush(hw3d->cp);
      }
   }

   return success;
}

static boolean
pass_render_condition(struct i965_3d *hw3d, struct pipe_context *pipe)
{
   uint64_t result;
   boolean wait;

   if (!hw3d->render_condition.query)
      return TRUE;

   switch (hw3d->render_condition.mode) {
   case PIPE_RENDER_COND_WAIT:
   case PIPE_RENDER_COND_BY_REGION_WAIT:
      wait = TRUE;
      break;
   case PIPE_RENDER_COND_NO_WAIT:
   case PIPE_RENDER_COND_BY_REGION_NO_WAIT:
   default:
      wait = FALSE;
      break;
   }

   if (pipe->get_query_result(pipe, hw3d->render_condition.query,
            wait, (union pipe_query_result *) &result)) {
      return (result > 0);
   }
   else {
      return TRUE;
   }
}

static void
i965_draw_vbo(struct pipe_context *pipe, const struct pipe_draw_info *info)
{
   struct i965_context *i965 = i965_context(pipe);
   struct i965_3d *hw3d = i965->hw3d;

   if (!pass_render_condition(hw3d, pipe))
      return;

   /* assume the cache is still in use by the previous batch */
   if (hw3d->new_batch)
      i965_shader_cache_mark_busy(i965->shader_cache);

   i965_finalize_states(i965);

   if (!draw_vbo(hw3d, i965, info))
      return;

   /* clear dirty status */
   i965->dirty = 0x0;
   hw3d->new_batch = FALSE;
   hw3d->shader_cache_seqno = i965->shader_cache->seqno;

   if (i965_debug & I965_DEBUG_NOCACHE)
      hw3d->flush(hw3d, FALSE);
}

static void
i965_render_condition(struct pipe_context *pipe,
                      struct pipe_query *query,
                      uint mode)
{
   struct i965_context *i965 = i965_context(pipe);
   struct i965_3d *hw3d = i965->hw3d;

   /* reference count? */
   hw3d->render_condition.query = query;
   hw3d->render_condition.mode = mode;
}

static void
i965_texture_barrier(struct pipe_context *pipe)
{
   struct i965_context *i965 = i965_context(pipe);
   struct i965_3d *hw3d = i965->hw3d;

   if (i965->cp->ring != INTEL_RING_RENDER)
      return;

   hw3d->flush(hw3d, FALSE);
}

/**
 * Initialize 3D-related functions.
 */
void
i965_init_3d_functions(struct i965_context *i965)
{
   i965->base.draw_vbo = i965_draw_vbo;
   i965->base.render_condition = i965_render_condition;
   i965->base.texture_barrier = i965_texture_barrier;
}
