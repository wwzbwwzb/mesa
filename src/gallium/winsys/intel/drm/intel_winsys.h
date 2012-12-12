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

#ifndef INTEL_WINSYS_H
#define INTEL_WINSYS_H

#include "pipe/p_compiler.h"

enum intel_ring_type {
   INTEL_RING_RENDER,
   INTEL_RING_BSD,
   INTEL_RING_BLT,
};

/* this is compatible with i915_drm.h's definitions */
enum intel_tiling_mode {
   INTEL_TILING_NONE = 0,
   INTEL_TILING_X    = 1,
   INTEL_TILING_Y    = 2,
};

/* this is compatible with i915_drm.h's definitions */
enum intel_domain_flag {
   INTEL_DOMAIN_CPU           = 0x00000001,
   INTEL_DOMAIN_RENDER        = 0x00000002,
   INTEL_DOMAIN_SAMPLER       = 0x00000004,
   INTEL_DOMAIN_COMMAND	      = 0x00000008,
   INTEL_DOMAIN_INSTRUCTION   = 0x00000010,
   INTEL_DOMAIN_VERTEX        = 0x00000020,
   INTEL_DOMAIN_GTT           = 0x00000040,
};

struct intel_info {
   int devid;
   int num_fences_avail;
};

struct winsys_handle;
struct intel_context;

struct intel_bo {
   void (*reference)(struct intel_bo *bo);
   void (*unreference)(struct intel_bo *bo);

   unsigned long (*get_size)(struct intel_bo *bo);
   unsigned long (*get_offset)(struct intel_bo *bo);
   void *(*get_virtual)(struct intel_bo *bo);
   int (*get_gem_handle)(struct intel_bo *bo);

   int (*references)(struct intel_bo *bo, struct intel_bo *target_bo);

   int (*map)(struct intel_bo *bo, int write_enable);
   int (*map_unsynchronized)(struct intel_bo *bo);
   int (*map_gtt)(struct intel_bo *bo);
   int (*unmap)(struct intel_bo *bo);

   int (*subdata)(struct intel_bo *bo, unsigned long offset,
                  unsigned long size, const void *data);
   int (*get_subdata)(struct intel_bo *bo, unsigned long offset,
                      unsigned long size, void *data);

   void (*wait_rendering)(struct intel_bo *bo);
   int (*busy)(struct intel_bo *bo);

   int (*get_tiling)(struct intel_bo *bo, enum intel_tiling_mode *tiling_mode);
   int (*get_handle)(struct intel_bo *bo, struct winsys_handle *handle);

   int (*get_reloc_count)(struct intel_bo *bo);
   void (*clear_relocs)(struct intel_bo *bo, int start);

   int (*emit_reloc)(struct intel_bo *bo, uint32_t offset,
                     struct intel_bo *target_bo, uint32_t target_offset,
                     uint32_t read_domains, uint32_t write_domain);

   int (*exec)(struct intel_bo *bo, int used,
               enum intel_ring_type ring,
               struct intel_context *ctx);
};

/*
 * Interface to OS functions.  This allows the pipe drivers to be OS agnostic.
 *
 * Check libdrm_intel for documentation.
 */
struct intel_winsys {
   void (*destroy)(struct intel_winsys *ws);

   void (*enable_reuse)(struct intel_winsys *ws);
   void (*enable_fenced_relocs)(struct intel_winsys *ws);

   const struct intel_info *(*get_info)(struct intel_winsys *ws);

   struct intel_context *(*create_context)(struct intel_winsys *ws);
   void (*destroy_context)(struct intel_winsys *ws,
                           struct intel_context *ctx);

   struct intel_bo *(*alloc)(struct intel_winsys *ws,
                             const char *name,
                             unsigned long size,
                             unsigned int alignment);

   struct intel_bo *(*alloc_tiled)(struct intel_winsys *ws,
                                   const char *name,
                                   int x, int y, int cpp,
                                   enum intel_tiling_mode *tiling_mode,
                                   unsigned long *pitch,
                                   boolean for_render);

   struct intel_bo *(*alloc_from_handle)(struct intel_winsys *ws,
                                         const char *name,
                                         struct winsys_handle *handle,
                                         enum intel_tiling_mode *tiling_mode,
                                         unsigned long *pitch);

   int (*check_aperture_space)(struct intel_winsys *ws,
                               struct intel_bo **bo_array,
                               int count);

   int (*read_reg)(struct intel_winsys *ws, uint32_t reg, uint64_t *val);

   void (*decode_batch)(struct intel_winsys *ws,
                        struct intel_bo *bo, int used);
};

#endif /* INTEL_WINSYS_H */
