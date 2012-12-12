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

#include "intel_reg.h"
#include "intel_winsys.h"

#include "i965_common.h"
#include "i965_context.h"
#include "i965_state.h"
#include "i965_cp.h"

/* the size of the private space */
static const int i965_cp_private = 2;

/**
 * Dump the contents of the parser bo.  This must be called in a post-flush
 * hook.
 */
void
i965_cp_dump(struct i965_cp *cp)
{
   if (cp->used) {
      debug_printf("dumping %d bytes\n", cp->used * 4);
      cp->winsys->decode_batch(cp->winsys, cp->bo, cp->used * 4);
   }
}

/**
 * Save the command parser state for rewind.
 *
 * Note that this cannot rewind a flush, and the caller must make sure
 * there is no flushing.
 */
void
i965_cp_setjmp(struct i965_cp *cp, struct i965_cp_jmp_buf *jmp)
{
   jmp->id = (void *) cp->bo;

   jmp->size = cp->size;
   jmp->used = cp->used;
   jmp->stolen = cp->stolen;
   jmp->reserved = cp->reserved;
   jmp->reloc_count = cp->bo->get_reloc_count(cp->bo);
}

/**
 * Rewind to the saved state.
 */
void
i965_cp_longjmp(struct i965_cp *cp, const struct i965_cp_jmp_buf *jmp)
{
   if (jmp->id != (void *) cp->bo) {
      assert(!"invalid use of CP longjmp");
      return;
   }

   cp->size = jmp->size;
   cp->used = jmp->used;
   cp->stolen = jmp->stolen;
   cp->reserved = jmp->reserved;
   cp->bo->clear_relocs(cp->bo, jmp->reloc_count);
}

static void
i965_cp_call_hook(struct i965_cp *cp, enum i965_cp_hook hook)
{
   const boolean no_implicit_flush = cp->no_implicit_flush;

   if (!cp->hooks[hook].func)
      return;

   cp->no_implicit_flush = TRUE;
   cp->hooks[hook].func(cp, cp->hooks[hook].data);
   cp->no_implicit_flush = no_implicit_flush;
}

/**
 * Allocate a new bo and empty the parser buffer.
 */
static void
i965_cp_reset(struct i965_cp *cp, boolean realloc)
{
   /* cp->reserved is not reset */
   cp->stolen = 0;

   cp->size = Elements(cp->buf) -
      (cp->reserved + i965_cp_private + cp->stolen);
   cp->used = 0;

   cp->cmd_cur = 0;
   cp->cmd_end = 0;

   if (realloc) {
      struct intel_bo *bo;

      /*
       * allocate the new bo before unreferencing the old one so that they
       * won't point at the same address, which is needed for jmpbuf
       */
      bo = cp->winsys->alloc(cp->winsys, "batch buffer",
            sizeof(cp->buf), 4096);
      if (likely(bo && cp->bo)) {
         cp->bo->unreference(cp->bo);
         cp->bo = bo;
      }
      else {
         /* the first call */
         if (bo)
            cp->bo = bo;

         /* OOM */
         if (!cp->bo)
            return;
      }
   }

   i965_cp_call_hook(cp, I965_CP_HOOK_NEW_BATCH);
}

static void
i965_cp_batch_buffer_end(struct i965_cp *cp)
{
   assert(cp->used + 2 <= cp->size);

   cp->buf[cp->used++] = MI_BATCH_BUFFER_END;

   /*
    * From the Sandy Bridge PRM, volume 1 part 1, page 107:
    *
    *     "The batch buffer must be QWord aligned and a multiple of QWords in
    *      length."
    */
   if (cp->used & 1)
      cp->buf[cp->used++] = MI_NOOP;
}

/**
 * Flush the command parser and execute the commands.
 */
void
i965_cp_flush(struct i965_cp *cp)
{
   const boolean do_exec = !(i965_debug & I965_DEBUG_NOHW);
   int err;

   /* sanity check */
   assert(Elements(cp->buf) == cp->size +
         cp->reserved + i965_cp_private + cp->stolen);

   /* make the reserved space available temporarily */
   cp->size += cp->reserved;
   i965_cp_call_hook(cp, I965_CP_HOOK_PRE_FLUSH);

   /* nothing to flush */
   if (!cp->used) {
      i965_cp_reset(cp, FALSE);
      return;
   }

   /* use the private space to end the batch buffer */
   cp->size += i965_cp_private;
   i965_cp_batch_buffer_end(cp);

   /* submit data */
   err = cp->bo->subdata(cp->bo, 0, cp->used * 4, cp->buf);
   if (!err && cp->stolen) {
      const int offset = Elements(cp->buf) - cp->stolen;
      err = cp->bo->subdata(cp->bo, offset * 4,
            cp->stolen * 4, &cp->buf[offset]);
   }

   /* execute the batch buffer */
   if (likely(!err && do_exec))
      err = cp->bo->exec(cp->bo, cp->used * 4, cp->ring, cp->hw_ctx);

   if (!err) {
      i965_cp_call_hook(cp, I965_CP_HOOK_POST_FLUSH);
      i965_cp_reset(cp, TRUE);
   }
   else {
      i965_cp_reset(cp, FALSE);
   }
}

/**
 * Destroy the command parser.
 */
void
i965_cp_destroy(struct i965_cp *cp)
{
   if (cp->bo)
      cp->bo->unreference(cp->bo);
   if (cp->hw_ctx)
      cp->winsys->destroy_context(cp->winsys, cp->hw_ctx);

   FREE(cp);
}

/**
 * Create a command parser.
 */
struct i965_cp *
i965_cp_create(struct intel_winsys *winsys)
{
   struct i965_cp *cp;

   cp = MALLOC_STRUCT(i965_cp);
   if (!cp)
      return NULL;

   cp->winsys = winsys;
   cp->hw_ctx = winsys->create_context(winsys);

   cp->ring = INTEL_RING_RENDER;
   cp->no_implicit_flush = FALSE;

   memset(cp->hooks, 0, sizeof(cp->hooks));

   cp->bo = NULL;
   cp->reserved = 0;

   i965_cp_reset(cp, TRUE);
   if (!cp->bo) {
      FREE(cp);
      return NULL;
   }

   return cp;
}
