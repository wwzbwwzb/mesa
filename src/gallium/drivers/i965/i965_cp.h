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

#ifndef I965_CP_H
#define I965_CP_H

#include "intel_winsys.h"

#include "i965_common.h"

struct i965_context;
struct i965_cp;

enum i965_cp_hook {
   I965_CP_HOOK_NEW_BATCH,
   I965_CP_HOOK_PRE_FLUSH,
   I965_CP_HOOK_POST_FLUSH,

   I965_CP_HOOK_COUNT,
};

typedef void (*i965_cp_hook_func)(struct i965_cp *cp, void *data);

/**
 * Command parser.
 */
struct i965_cp {
   struct intel_winsys *winsys;
   struct intel_context *hw_ctx;

   enum intel_ring_type ring;
   boolean no_implicit_flush;

   struct {
      i965_cp_hook_func func;
      void *data;
   } hooks[I965_CP_HOOK_COUNT];

   struct intel_bo *bo;

   uint32_t buf[8192];
   int size, used;
   int stolen, reserved;
   int cmd_cur, cmd_end;
};

/**
 * Jump buffer to save command parser state for rewind.
 */
struct i965_cp_jmp_buf {
   void *id;
   int size, used;
   int stolen, reserved;
   int reloc_count;
};

struct i965_cp *
i965_cp_create(struct intel_winsys *winsys);

void
i965_cp_destroy(struct i965_cp *cp);

void
i965_cp_flush(struct i965_cp *cp);

void
i965_cp_dump(struct i965_cp *cp);

void
i965_cp_setjmp(struct i965_cp *cp, struct i965_cp_jmp_buf *jmp);

void
i965_cp_longjmp(struct i965_cp *cp, const struct i965_cp_jmp_buf *jmp);

/**
 * Return TRUE if the parser buffer is empty.
 */
static INLINE boolean
i965_cp_empty(struct i965_cp *cp)
{
   return !cp->used;
}

/**
 * Return the remaining space (in dwords) in the parser buffer.
 */
static INLINE int
i965_cp_space(struct i965_cp *cp)
{
   return cp->size - cp->used;
}

/**
 * Set the ring buffer.
 */
static INLINE void
i965_cp_set_ring(struct i965_cp *cp, enum intel_ring_type ring)
{
   if (cp->ring != ring) {
      i965_cp_flush(cp);
      cp->ring = ring;
   }
}

/**
 * Assert that i965_cp_begin(), i965_cp_steal(), and i965_cp_reserve() do not
 * flush implicitly.
 */
static INLINE void
i965_cp_assert_no_implicit_flush(struct i965_cp *cp, boolean enable)
{
   cp->no_implicit_flush = enable;
}

/**
 * Set a command parser hook.
 */
static INLINE void
i965_cp_set_hook(struct i965_cp *cp, enum i965_cp_hook hook,
                 i965_cp_hook_func func, void *data)
{
   cp->hooks[hook].func = func;
   cp->hooks[hook].data = data;
}

/**
 * Begin writing a command.
 */
static INLINE void
i965_cp_begin(struct i965_cp *cp, int cmd_size)
{
   if (cp->used + cmd_size > cp->size) {
      if (cp->no_implicit_flush) {
         assert(!"unexpected command parser flush");
         cp->used = 0;
      }
      i965_cp_flush(cp);

      assert(cp->used + cmd_size <= cp->size);
   }

   assert(cp->cmd_cur == cp->cmd_end);
   cp->cmd_cur = cp->used;
   cp->cmd_end = cp->cmd_cur + cmd_size;
   cp->used = cp->cmd_end;
}

/**
 * Begin writing data to a space stolen from the top of the parser buffer.
 *
 * \param desc informative description of the data to be written
 * \param data_size in dwords
 * \param align in dwords
 * \param bo_offset in bytes to the stolen space
 */
static INLINE void
i965_cp_steal(struct i965_cp *cp, const char *desc,
              int data_size, int align, uint32_t *bo_offset)
{
   int pad;

   if (!align)
      align = 1;

   pad = (Elements(cp->buf) - cp->stolen - data_size) % align;

   /* flush if there is not enough space after stealing */
   if (cp->used > cp->size - data_size - pad) {
      if (cp->no_implicit_flush) {
         assert(!"unexpected command parser flush");
         cp->used = 0;
      }
      i965_cp_flush(cp);

      pad = (Elements(cp->buf) - cp->stolen - data_size) % align;
      assert(cp->used <= cp->size - data_size - pad);
   }

   assert(cp->cmd_cur == cp->cmd_end);
   cp->cmd_cur = Elements(cp->buf) - cp->stolen - data_size - pad;
   cp->cmd_end = cp->cmd_cur + data_size;

   cp->stolen += data_size + pad;
   /* shrink size */
   cp->size -= data_size + pad;

   /* offset in cp->bo */
   if (bo_offset)
      *bo_offset = cp->cmd_cur * 4;
}

/**
 * Write a dword to the parser buffer.  This function must be enclosed by
 * i965_cp_begin()/i965_cp_steal() and i965_cp_end().
 */
static INLINE void
i965_cp_write(struct i965_cp *cp, uint32_t val)
{
   assert(cp->cmd_cur < cp->cmd_end);
   cp->buf[cp->cmd_cur++] = val;
}

/**
 * Write multiple dwords to the parser buffer.
 */
static INLINE void
i965_cp_write_multi(struct i965_cp *cp, uint32_t *vals, int num_vals)
{
   assert(cp->cmd_cur + num_vals <= cp->cmd_end);
   memcpy(cp->buf + cp->cmd_cur, vals, num_vals * 4);
   cp->cmd_cur += num_vals;
}

/**
 * Write a bo to the parser buffer.  In addition to writing the offset of the
 * bo to the buffer, it also emits a relocation.
 */
static INLINE void
i965_cp_write_bo(struct i965_cp *cp, struct intel_bo *bo,
                 uint32_t read_domains, uint32_t write_domain,
                 uint32_t offset)
{
   int err;

   if (!bo) {
      i965_cp_write(cp, offset);
      return;
   }

   err = cp->bo->emit_reloc(cp->bo, cp->cmd_cur * 4,
         bo, offset, read_domains, write_domain);
   if (!err)
      i965_cp_write(cp, bo->get_offset(bo) + offset);
   else
      i965_cp_write(cp, 0);
}

/**
 * End a command.  Every i965_cp_begin() or i965_cp_steal() must have a
 * matching i965_cp_end().
 */
static INLINE void
i965_cp_end(struct i965_cp *cp)
{
   assert(cp->cmd_cur == cp->cmd_end);
}

/**
 * Reserve the given size of space from the parser buffer.  The reserved space
 * will be made available temporarily just before the pre-flush hook, and will
 * become reserved again after post-flush hook.
 */
static INLINE void
i965_cp_reserve(struct i965_cp *cp, int reserved)
{
   assert(cp->reserved + reserved >= 0);
   assert(cp->size - reserved >= 0);

   if (cp->used > cp->size - reserved) {
      if (cp->no_implicit_flush) {
         assert(!"unexpected command parser flush");
         cp->used = 0;
      }
      i965_cp_flush(cp);

      assert(cp->used <= cp->size - reserved);
   }

   cp->reserved += reserved;
   cp->size -= reserved;
}

#endif /* I965_CP_H */
