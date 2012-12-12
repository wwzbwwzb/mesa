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

#include "util/u_prim.h"
#include "i965_common.h"
#include "i965_context.h"
#include "i965_cp.h"
#include "i965_query.h"
#include "i965_shader.h"
#include "i965_state.h"
#include "i965_3d_gen6.h"
#include "i965_3d.h"

static void
alloc_query_bo(struct i965_3d *hw3d, struct i965_query *q)
{
   const int size = 4096;
   const char *name;

   q->size = size / sizeof(uint64_t);
   q->used = 0;

   /* except for timestamp, we write pairs of the values */
   assert(q->size % 2 == 0);

   /* should we reallocate? */
   if (q->bo)
      return;

   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      name = "occlusion query";
      break;
   case PIPE_QUERY_TIMESTAMP:
      name = "timestamp query";
      break;
   case PIPE_QUERY_TIME_ELAPSED:
      name = "time elapsed query";
      break;
   default:
      name = "unknown query";
      break;
   }

   q->bo = hw3d->cp->winsys->alloc(hw3d->cp->winsys, name, size, 4096);
}

/**
 * Begin a query.
 */
void
i965_3d_begin_query(struct i965_context *i965, struct i965_query *q)
{
   struct i965_3d *hw3d = i965->hw3d;

   i965_cp_set_ring(i965->cp, INTEL_RING_RENDER);

   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      alloc_query_bo(hw3d, q);
      q->result.u64 = 0;

      /* XXX we should check the aperture size */
      q->cp_pre_flush_reserve =
         hw3d->write_depth_count(hw3d, q->bo, q->used++, FALSE);

      /* reserve some space for pausing the query */
      i965_cp_reserve(hw3d->cp, q->cp_pre_flush_reserve);
      list_add(&q->list, &hw3d->occlusion_queries);
      break;
   case PIPE_QUERY_TIMESTAMP:
      /* nop */
      break;
   case PIPE_QUERY_TIME_ELAPSED:
      alloc_query_bo(hw3d, q);
      q->result.u64 = 0;

      /* XXX we should check the aperture size */
      q->cp_pre_flush_reserve =
         hw3d->write_timestamp(hw3d, q->bo, q->used++, FALSE);

      /* reserve some space for pausing the query */
      i965_cp_reserve(hw3d->cp, q->cp_pre_flush_reserve);
      list_add(&q->list, &hw3d->timer_queries);
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      q->result.u64 = 0;
      list_add(&q->list, &hw3d->prim_queries);
      break;
   default:
      assert(!"unknown query type");
      break;
   }
}

/**
 * End a query.
 */
void
i965_3d_end_query(struct i965_context *i965, struct i965_query *q)
{
   struct i965_3d *hw3d = i965->hw3d;

   i965_cp_set_ring(i965->cp, INTEL_RING_RENDER);

   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      hw3d->write_depth_count(hw3d, q->bo, q->used++, FALSE);
      list_del(&q->list);
      i965_cp_reserve(hw3d->cp, -q->cp_pre_flush_reserve);
      break;
   case PIPE_QUERY_TIMESTAMP:
      alloc_query_bo(hw3d, q);
      q->result.u64 = 0;
      hw3d->write_timestamp(hw3d, q->bo, 0, FALSE);
      break;
   case PIPE_QUERY_TIME_ELAPSED:
      hw3d->write_timestamp(hw3d, q->bo, q->used++, FALSE);
      list_del(&q->list);
      i965_cp_reserve(hw3d->cp, -q->cp_pre_flush_reserve);
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      list_del(&q->list);
      break;
   default:
      assert(!"unknown query type");
      break;
   }

   /* flush now so that we can wait on the bo */
   i965_cp_flush(hw3d->cp);
}

static uint64_t
timestamp_to_ns(uint64_t timestamp)
{
   /*
    * From the Sandy Bridge PRM, volume 1 part 3, page 73:
    *
    *     "This register (TIMESTAMP) toggles every 80 ns of time."
    */
   const unsigned scale = 80;

   return timestamp * scale;
}

static void
update_occlusion_counter(struct i965_3d *hw3d, struct i965_query *q)
{
   uint64_t *vals;
   int i;

   assert(q->used % 2 == 0);

   q->bo->map(q->bo, FALSE);
   vals = q->bo->get_virtual(q->bo);

   for (i = 0; i < q->used; i += 2)
      q->result.u64 += vals[i + 1] - vals[i];

   q->bo->unmap(q->bo);

   q->used = 0;
}

static void
update_timestamp(struct i965_3d *hw3d, struct i965_query *q)
{
   uint64_t *vals;

   q->bo->map(q->bo, FALSE);
   vals = q->bo->get_virtual(q->bo);

   q->result.u64 += timestamp_to_ns(vals[0]);

   q->bo->unmap(q->bo);
}

static void
update_time_elapsed(struct i965_3d *hw3d, struct i965_query *q)
{
   uint64_t *vals, elapsed = 0;
   int i;

   assert(q->used % 2 == 0);

   q->bo->map(q->bo, FALSE);
   vals = q->bo->get_virtual(q->bo);

   for (i = 0; i < q->used; i += 2)
      elapsed += vals[i + 1] - vals[i];

   q->result.u64 += timestamp_to_ns(elapsed);

   q->bo->unmap(q->bo);

   q->used = 0;
}

/**
 * Update the query result.
 */
void
i965_3d_update_query_result(struct i965_context *i965, struct i965_query *q)
{
   struct i965_3d *hw3d = i965->hw3d;

   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      update_occlusion_counter(hw3d, q);
      break;
   case PIPE_QUERY_TIMESTAMP:
      update_timestamp(hw3d, q);
      break;
   case PIPE_QUERY_TIME_ELAPSED:
      update_time_elapsed(hw3d, q);
      break;
   default:
      assert(!"unknown query type");
      break;
   }
}

/**
 * Hook for CP new-batch.
 */
void
i965_3d_new_cp_batch(struct i965_3d *hw3d)
{
   struct i965_query *q;

   hw3d->new_batch = TRUE;

   /* resume occlusion queries */
   LIST_FOR_EACH_ENTRY(q, &hw3d->occlusion_queries, list) {
      /* accumulate the result if the bo is alreay full */
      if (q->used >= q->size) {
         update_occlusion_counter(hw3d, q);
         alloc_query_bo(hw3d, q);
      }

      hw3d->write_depth_count(hw3d, q->bo, q->used++, FALSE);
   }

   /* resume timer queries */
   LIST_FOR_EACH_ENTRY(q, &hw3d->timer_queries, list) {
      /* accumulate the result if the bo is alreay full */
      if (q->used >= q->size) {
         update_time_elapsed(hw3d, q);
         alloc_query_bo(hw3d, q);
      }

      hw3d->write_timestamp(hw3d, q->bo, q->used++, FALSE);
   }
}

/**
 * Hook for CP pre-flush.
 */
void
i965_3d_pre_cp_flush(struct i965_3d *hw3d)
{
   struct i965_query *q;

   /* pause occlusion queries */
   LIST_FOR_EACH_ENTRY(q, &hw3d->occlusion_queries, list)
      hw3d->write_depth_count(hw3d, q->bo, q->used++, FALSE);

   /* pause timer queries */
   LIST_FOR_EACH_ENTRY(q, &hw3d->timer_queries, list)
      hw3d->write_timestamp(hw3d, q->bo, q->used++, FALSE);
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

   list_inithead(&hw3d->occlusion_queries);
   list_inithead(&hw3d->timer_queries);
   list_inithead(&hw3d->prim_queries);

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

/* XXX move to u_prim.h */
static unsigned
prim_count(unsigned prim, unsigned num_verts)
{
   unsigned num_prims;

   u_trim_pipe_prim(prim, &num_verts);

   switch (prim) {
   case PIPE_PRIM_POINTS:
      num_prims = num_verts;
      break;
   case PIPE_PRIM_LINES:
      num_prims = num_verts / 2;
      break;
   case PIPE_PRIM_LINE_LOOP:
      num_prims = num_verts;
      break;
   case PIPE_PRIM_LINE_STRIP:
      num_prims = num_verts - 1;
      break;
   case PIPE_PRIM_TRIANGLES:
      num_prims = num_verts / 3;
      break;
   case PIPE_PRIM_TRIANGLE_STRIP:
   case PIPE_PRIM_TRIANGLE_FAN:
      num_prims = num_verts - 2;
      break;
   case PIPE_PRIM_QUADS:
      num_prims = (num_verts / 4) * 2;
      break;
   case PIPE_PRIM_QUAD_STRIP:
      num_prims = (num_verts / 2 - 1) * 2;
      break;
   case PIPE_PRIM_POLYGON:
      num_prims = num_verts - 2;
      break;
   case PIPE_PRIM_LINES_ADJACENCY:
      num_prims = num_verts / 4;
      break;
   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      num_prims = num_verts - 3;
      break;
   case PIPE_PRIM_TRIANGLES_ADJACENCY:
      /* u_trim_pipe_prim is wrong? */
      num_verts += 1;

      num_prims = num_verts / 6;
      break;
   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      /* u_trim_pipe_prim is wrong? */
      if (num_verts >= 6)
         num_verts -= (num_verts % 2);
      else
         num_verts = 0;

      num_prims = (num_verts / 2 - 2);
      break;
   default:
      assert(!"unknown pipe prim");
      num_prims = 0;
      break;
   }

   return num_prims;
}

static void
update_prim_queries(struct i965_3d *hw3d, const struct pipe_draw_info *info)
{
   struct i965_query *q;

   LIST_FOR_EACH_ENTRY(q, &hw3d->prim_queries, list) {
      switch (q->type) {
      case PIPE_QUERY_PRIMITIVES_GENERATED:
         q->result.u64 += prim_count(info->mode, info->count);
         break;
      default:
         assert(!"unknown query type");
         break;
      }
   }
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

   update_prim_queries(hw3d, info);
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
