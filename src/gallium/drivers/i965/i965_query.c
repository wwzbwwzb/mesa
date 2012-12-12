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
#include "i965_3d.h"
#include "i965_query.h"

static INLINE struct i965_query *
i965_query(struct pipe_query *query)
{
   return (struct i965_query *) query;
}

typedef void (*dispatch_query)(struct i965_context *i965,
                               struct i965_query *q);

static const dispatch_query dispatch_begin_query[PIPE_QUERY_TYPES] = {
   [PIPE_QUERY_OCCLUSION_COUNTER]      = i965_3d_begin_query,
   [PIPE_QUERY_OCCLUSION_PREDICATE]    = NULL,
   [PIPE_QUERY_TIMESTAMP]              = i965_3d_begin_query,
   [PIPE_QUERY_TIMESTAMP_DISJOINT]     = NULL,
   [PIPE_QUERY_TIME_ELAPSED]           = i965_3d_begin_query,
   [PIPE_QUERY_PRIMITIVES_GENERATED]   = i965_3d_begin_query,
   [PIPE_QUERY_PRIMITIVES_EMITTED]     = NULL,
   [PIPE_QUERY_SO_STATISTICS]          = NULL,
   [PIPE_QUERY_SO_OVERFLOW_PREDICATE]  = NULL,
   [PIPE_QUERY_GPU_FINISHED]           = NULL,
   [PIPE_QUERY_PIPELINE_STATISTICS]    = NULL,
};

static const dispatch_query dispatch_end_query[PIPE_QUERY_TYPES] = {
   [PIPE_QUERY_OCCLUSION_COUNTER]      = i965_3d_end_query,
   [PIPE_QUERY_OCCLUSION_PREDICATE]    = NULL,
   [PIPE_QUERY_TIMESTAMP]              = i965_3d_end_query,
   [PIPE_QUERY_TIMESTAMP_DISJOINT]     = NULL,
   [PIPE_QUERY_TIME_ELAPSED]           = i965_3d_end_query,
   [PIPE_QUERY_PRIMITIVES_GENERATED]   = i965_3d_end_query,
   [PIPE_QUERY_PRIMITIVES_EMITTED]     = NULL,
   [PIPE_QUERY_SO_STATISTICS]          = NULL,
   [PIPE_QUERY_SO_OVERFLOW_PREDICATE]  = NULL,
   [PIPE_QUERY_GPU_FINISHED]           = NULL,
   [PIPE_QUERY_PIPELINE_STATISTICS]    = NULL,
};

static const dispatch_query dispatch_update_query_result[PIPE_QUERY_TYPES] = {
   [PIPE_QUERY_OCCLUSION_COUNTER]      = i965_3d_update_query_result,
   [PIPE_QUERY_OCCLUSION_PREDICATE]    = NULL,
   [PIPE_QUERY_TIMESTAMP]              = i965_3d_update_query_result,
   [PIPE_QUERY_TIMESTAMP_DISJOINT]     = NULL,
   [PIPE_QUERY_TIME_ELAPSED]           = i965_3d_update_query_result,
   [PIPE_QUERY_PRIMITIVES_GENERATED]   = NULL,
   [PIPE_QUERY_PRIMITIVES_EMITTED]     = NULL,
   [PIPE_QUERY_SO_STATISTICS]          = NULL,
   [PIPE_QUERY_SO_OVERFLOW_PREDICATE]  = NULL,
   [PIPE_QUERY_GPU_FINISHED]           = NULL,
   [PIPE_QUERY_PIPELINE_STATISTICS]    = NULL,
};

static struct pipe_query *
i965_create_query(struct pipe_context *pipe, unsigned query_type)
{
   struct i965_query *q;

   switch (query_type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      break;
   default:
      return NULL;
   }

   q = CALLOC_STRUCT(i965_query);
   if (!q)
      return NULL;

   q->type = query_type;
   list_inithead(&q->list);

   return (struct pipe_query *) q;
}

static void
i965_destroy_query(struct pipe_context *pipe, struct pipe_query *query)
{
   struct i965_query *q = i965_query(query);

   if (q->bo)
      q->bo->unreference(q->bo);

   FREE(q);
}

static void
i965_begin_query(struct pipe_context *pipe, struct pipe_query *query)
{
   struct i965_context *i965 = i965_context(pipe);
   struct i965_query *q = i965_query(query);

   assert(dispatch_begin_query[q->type]);
   dispatch_begin_query[q->type](i965, q);
}

static void
i965_end_query(struct pipe_context *pipe, struct pipe_query *query)
{
   struct i965_context *i965 = i965_context(pipe);
   struct i965_query *q = i965_query(query);

   assert(dispatch_end_query[q->type]);
   dispatch_end_query[q->type](i965, q);
}

static boolean
i965_get_query_result(struct pipe_context *pipe, struct pipe_query *query,
                      boolean wait, union pipe_query_result *result)
{
   struct i965_context *i965 = i965_context(pipe);
   struct i965_query *q = i965_query(query);

   if (q->bo) {
      if (!wait && q->bo->busy(q->bo))
         return FALSE;

      assert(dispatch_update_query_result[q->type]);
      dispatch_update_query_result[q->type](i965, q);
   }

   if (result) {
      /* st_WaitQuery() passed uint64_t instead of pipe_query_result here */
      uint64_t *r = (uint64_t *) result;
      *r = q->result.u64;
   }

   return TRUE;
}

/**
 * Initialize query-related functions.
 */
void
i965_init_query_functions(struct i965_context *i965)
{
   i965->base.create_query = i965_create_query;
   i965->base.destroy_query = i965_destroy_query;
   i965->base.begin_query = i965_begin_query;
   i965->base.end_query = i965_end_query;
   i965->base.get_query_result = i965_get_query_result;
}
