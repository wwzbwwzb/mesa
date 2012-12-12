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

#include <string.h>
#include <errno.h>

#include <xf86drm.h>
#include <i915_drm.h>
#include <intel_bufmgr.h>

#include "state_tracker/drm_driver.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_debug.h"
#include "intel_drm_public.h"
#include "intel_winsys.h"

#define BATCH_SZ (8192 * sizeof(uint32_t))

struct intel_drm_winsys {
   struct intel_winsys base;

   int fd;
   drm_intel_bufmgr *bufmgr;

   struct intel_info info;

   drm_intel_bo **bo_array;
   int array_size;
};

struct intel_drm_bo {
   struct intel_bo base;

   struct pipe_reference reference;
   drm_intel_bo *bo;
   unsigned long pitch;
};

static INLINE struct intel_drm_winsys *
intel_drm_winsys(struct intel_winsys *ws)
{
   return (struct intel_drm_winsys *) ws;
}

static INLINE struct intel_drm_bo *
intel_drm_bo(struct intel_bo *bo)
{
   return (struct intel_drm_bo *) bo;
}

static int
intel_drm_bo_exec(struct intel_bo *bo, int used,
                  enum intel_ring_type ring,
                  struct intel_context *ctx)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   uint32_t flags;

   switch (ring) {
   case INTEL_RING_RENDER:
      flags = I915_EXEC_RENDER;
      break;
   case INTEL_RING_BSD:
      flags = I915_EXEC_BSD;
      break;
   case INTEL_RING_BLT:
      flags = I915_EXEC_BLT;
      break;
   default:
      return -EINVAL;
   }

   if (ctx) {
      return drm_intel_gem_bo_context_exec(drm_bo->bo,
            (drm_intel_context *) ctx, used, flags);
   }
   else {
      return drm_intel_bo_mrb_exec(drm_bo->bo, used, NULL, 0, 0, flags);
   }
}

static int
intel_drm_bo_emit_reloc(struct intel_bo *bo, uint32_t offset,
                        struct intel_bo *target_bo, uint32_t target_offset,
                        uint32_t read_domains, uint32_t write_domain)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   struct intel_drm_bo *target = intel_drm_bo(target_bo);

   return drm_intel_bo_emit_reloc(drm_bo->bo, offset,
         target->bo, target_offset, read_domains, write_domain);
}

static void
intel_drm_bo_clear_relocs(struct intel_bo *bo, int start)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_intel_gem_bo_clear_relocs(drm_bo->bo, start);
}

static int
intel_drm_bo_get_reloc_count(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_intel_gem_bo_get_reloc_count(drm_bo->bo);
}

static int
intel_drm_bo_get_handle(struct intel_bo *bo, struct winsys_handle *handle)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   int err = 0;

   switch (handle->type) {
   case DRM_API_HANDLE_TYPE_SHARED:
      {
         uint32_t name;

         err = drm_intel_bo_flink(drm_bo->bo, &name);
         if (!err)
            handle->handle = name;
      }
      break;
   case DRM_API_HANDLE_TYPE_KMS:
      handle->handle = drm_bo->bo->handle;
      break;
   default:
      err = -EINVAL;
      break;
   }

   if (err)
      return err;

   handle->stride = drm_bo->pitch;

   return 0;
}

static int
intel_drm_bo_get_tiling(struct intel_bo *bo,
                        enum intel_tiling_mode *tiling_mode)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   uint32_t tiling, swizzle;
   int err;

   err = drm_intel_bo_get_tiling(drm_bo->bo, &tiling, &swizzle);

   *tiling_mode = tiling;

   return err;
}

static int
intel_drm_bo_busy(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_intel_bo_busy(drm_bo->bo);
}

static void
intel_drm_bo_wait_rendering(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   drm_intel_bo_wait_rendering(drm_bo->bo);
}

static int
intel_drm_bo_get_subdata(struct intel_bo *bo, unsigned long offset,
                         unsigned long size, void *data)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_intel_bo_get_subdata(drm_bo->bo, offset, size, data);
}

static int
intel_drm_bo_subdata(struct intel_bo *bo, unsigned long offset,
                     unsigned long size, const void *data)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_intel_bo_subdata(drm_bo->bo, offset, size, data);
}

static int
intel_drm_bo_map(struct intel_bo *bo, int write_enable)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_intel_bo_map(drm_bo->bo, write_enable);
}

static int
intel_drm_bo_map_unsynchronized(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_intel_gem_bo_map_unsynchronized(drm_bo->bo);
}

static int
intel_drm_bo_map_gtt(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_intel_gem_bo_map_gtt(drm_bo->bo);
}

static int
intel_drm_bo_unmap(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_intel_bo_unmap(drm_bo->bo);
}

static int
intel_drm_bo_references(struct intel_bo *bo, struct intel_bo *target_bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   struct intel_drm_bo *target = intel_drm_bo(target_bo);

   return drm_intel_bo_references(drm_bo->bo, target->bo);
}

static unsigned long
intel_drm_bo_get_size(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_bo->bo->size;
}

static unsigned long
intel_drm_bo_get_offset(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_bo->bo->offset;
}

static void *
intel_drm_bo_get_virtual(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_bo->bo->virtual;
}

static int
intel_drm_bo_get_gem_handle(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   return drm_bo->bo->handle;
}

static void
intel_drm_bo_reference(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);

   pipe_reference(NULL, &drm_bo->reference);
}

static void
intel_drm_bo_unreference(struct intel_bo *bo)
{
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);

   if (pipe_reference(&drm_bo->reference, NULL)) {
      drm_intel_bo_unreference(drm_bo->bo);
      FREE(drm_bo);
   }
}

static struct intel_drm_bo *
create_bo(void)
{
   struct intel_drm_bo *drm_bo;

   drm_bo = CALLOC_STRUCT(intel_drm_bo);
   if (!drm_bo)
      return NULL;

   pipe_reference_init(&drm_bo->reference, 1);

   drm_bo->base.reference = intel_drm_bo_reference;
   drm_bo->base.unreference = intel_drm_bo_unreference;

   drm_bo->base.get_size = intel_drm_bo_get_size;
   drm_bo->base.get_offset = intel_drm_bo_get_offset;
   drm_bo->base.get_virtual = intel_drm_bo_get_virtual;
   drm_bo->base.get_gem_handle = intel_drm_bo_get_gem_handle;

   drm_bo->base.references = intel_drm_bo_references;

   drm_bo->base.map = intel_drm_bo_map;
   drm_bo->base.unmap = intel_drm_bo_unmap;

   drm_bo->base.map_unsynchronized = intel_drm_bo_map_unsynchronized;
   drm_bo->base.map_gtt = intel_drm_bo_map_gtt;

   drm_bo->base.subdata = intel_drm_bo_subdata;
   drm_bo->base.get_subdata = intel_drm_bo_get_subdata;

   drm_bo->base.wait_rendering = intel_drm_bo_wait_rendering;

   drm_bo->base.get_tiling = intel_drm_bo_get_tiling;
   drm_bo->base.get_handle = intel_drm_bo_get_handle;

   drm_bo->base.busy = intel_drm_bo_busy;

   drm_bo->base.get_reloc_count = intel_drm_bo_get_reloc_count;
   drm_bo->base.clear_relocs = intel_drm_bo_clear_relocs;

   drm_bo->base.emit_reloc = intel_drm_bo_emit_reloc;

   drm_bo->base.exec = intel_drm_bo_exec;

   return drm_bo;
}

static struct intel_bo *
intel_drm_winsys_alloc(struct intel_winsys *ws,
                       const char *name,
                       unsigned long size,
                       unsigned int alignment)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);
   struct intel_drm_bo *drm_bo;

   drm_bo = create_bo();
   if (!drm_bo)
      return NULL;

   drm_bo->bo = drm_intel_bo_alloc(drm_ws->bufmgr, name, size, alignment);
   if (!drm_bo->bo) {
      FREE(drm_bo);
      return NULL;
   }

   return &drm_bo->base;
}

static struct intel_bo *
intel_drm_winsys_alloc_tiled(struct intel_winsys *ws,
                             const char *name,
                             int x, int y, int cpp,
                             enum intel_tiling_mode *tiling_mode,
                             unsigned long *pitch,
                             boolean for_render)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);
   struct intel_drm_bo *drm_bo;
   unsigned long flags = (for_render) ? BO_ALLOC_FOR_RENDER : 0;
   uint32_t tiling = *tiling_mode;

   drm_bo = create_bo();
   if (!drm_bo)
      return NULL;

   drm_bo->bo = drm_intel_bo_alloc_tiled(drm_ws->bufmgr, name,
         x, y, cpp, &tiling, &drm_bo->pitch, flags);
   if (!drm_bo->bo) {
      FREE(drm_bo);
      return NULL;
   }

   *tiling_mode = tiling;
   *pitch = drm_bo->pitch;

   return &drm_bo->base;
}

static struct intel_bo *
intel_drm_winsys_alloc_from_handle(struct intel_winsys *ws,
                                   const char *name,
                                   struct winsys_handle *handle,
                                   enum intel_tiling_mode *tiling_mode,
                                   unsigned long *pitch)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);
   struct intel_drm_bo *drm_bo;
   uint32_t tiling, swizzle;
   int err;

   drm_bo = create_bo();
   if (!drm_bo)
      return NULL;

   drm_bo->bo = drm_intel_bo_gem_create_from_name(drm_ws->bufmgr,
         name, handle->handle);
   if (!drm_bo->bo) {
      FREE(drm_bo);
      return NULL;
   }

   drm_bo->pitch = handle->stride;

   err = drm_intel_bo_get_tiling(drm_bo->bo, &tiling, &swizzle);
   if (err) {
      drm_intel_bo_unreference(drm_bo->bo);
      FREE(drm_bo);
      return NULL;
   }

   *tiling_mode = tiling;
   *pitch = drm_bo->pitch;

   return &drm_bo->base;
}

static int
intel_drm_winsys_read_reg(struct intel_winsys *ws,
                          uint32_t reg, uint64_t *val)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);

   return drm_intel_reg_read(drm_ws->bufmgr, reg, val);
}

static void
intel_drm_winsys_decode_batch(struct intel_winsys *ws,
                              struct intel_bo *bo, int used)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);
   struct intel_drm_bo *drm_bo = intel_drm_bo(bo);
   struct drm_intel_decode *decode;
   int ret;

   decode = drm_intel_decode_context_alloc(drm_ws->info.devid);
   if (!decode)
      return;

   ret = drm_intel_bo_map(drm_bo->bo, FALSE);
   if (ret) {
      fprintf(stderr, "failed to map buffer for decoding: %s",
            strerror(ret));
   }
   else {
      /* in dwords */
      used /= 4;

      drm_intel_decode_set_batch_pointer(decode,
            drm_bo->bo->virtual,
            drm_bo->bo->offset,
            used);

      drm_intel_decode(decode);
   }

   drm_intel_decode_context_free(decode);

   if (ret == 0)
      drm_intel_bo_unmap(drm_bo->bo);
}

static int
intel_drm_winsys_check_aperture_space(struct intel_winsys *ws,
                                      struct intel_bo **bo_array,
                                      int count)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);
   int i;

   /* resize bo array if necessary */
   if (drm_ws->array_size < count) {
      void *tmp = MALLOC(count * sizeof(*drm_ws->bo_array));

      if (!tmp)
         return 0;

      FREE(drm_ws->bo_array);
      drm_ws->bo_array = tmp;
      drm_ws->array_size = count;
   }

   for (i = 0; i < count; i++)
      drm_ws->bo_array[i] = ((struct intel_drm_bo *) bo_array[i])->bo;

   return drm_intel_bufmgr_check_aperture_space(drm_ws->bo_array, count);
}

static void
intel_drm_winsys_enable_reuse(struct intel_winsys *ws)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);
   drm_intel_bufmgr_gem_enable_reuse(drm_ws->bufmgr);
}

static void
intel_drm_winsys_enable_fenced_relocs(struct intel_winsys *ws)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);
   drm_intel_bufmgr_gem_enable_fenced_relocs(drm_ws->bufmgr);
}

static void
intel_drm_winsys_destroy_context(struct intel_winsys *ws,
                                 struct intel_context *ctx)
{
   drm_intel_gem_context_destroy((drm_intel_context *) ctx);
}

static struct intel_context *
intel_drm_winsys_create_context(struct intel_winsys *ws)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);

   return (struct intel_context *)
      drm_intel_gem_context_create(drm_ws->bufmgr);
}

static const struct intel_info *
intel_drm_winsys_get_info(struct intel_winsys *ws)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);
   return &drm_ws->info;
}

static void
intel_drm_winsys_destroy(struct intel_winsys *ws)
{
   struct intel_drm_winsys *drm_ws = intel_drm_winsys(ws);

   drm_intel_bufmgr_destroy(drm_ws->bufmgr);
   FREE(drm_ws->bo_array);
   FREE(drm_ws);
}

static boolean
get_param(struct intel_drm_winsys *drm_ws, int param, int *value)
{
   struct drm_i915_getparam gp;
   int ret;

   memset(&gp, 0, sizeof(gp));
   gp.param = param;
   gp.value = value;

   ret = drmCommandWriteRead(drm_ws->fd, DRM_I915_GETPARAM, &gp, sizeof(gp));

   return (ret == 0);
}

static boolean
init_info(struct intel_drm_winsys *drm_ws)
{
   struct intel_info *info = &drm_ws->info;

   info->devid = drm_intel_bufmgr_gem_get_devid(drm_ws->bufmgr);

   if (!get_param(drm_ws, I915_PARAM_NUM_FENCES_AVAIL,
            &info->num_fences_avail)) {
      debug_printf("failed to get I915_PARAM_NUM_FENCES_AVAIL\n");
      return FALSE;
   }

   return TRUE;
}

struct intel_winsys *
intel_drm_winsys_create(int fd)
{
   struct intel_drm_winsys *drm_ws;

   drm_ws = CALLOC_STRUCT(intel_drm_winsys);
   if (!drm_ws)
      return NULL;

   drm_ws->fd = fd;

   drm_ws->bufmgr = drm_intel_bufmgr_gem_init(drm_ws->fd, BATCH_SZ);
   if (!drm_ws->bufmgr) {
      debug_printf("failed to create GEM buffer manager\n");
      FREE(drm_ws);
      return NULL;
   }

   if (!init_info(drm_ws)) {
      drm_intel_bufmgr_destroy(drm_ws->bufmgr);
      FREE(drm_ws);
      return NULL;
   }

   drm_ws->base.destroy = intel_drm_winsys_destroy;
   drm_ws->base.enable_reuse = intel_drm_winsys_enable_reuse;
   drm_ws->base.enable_fenced_relocs = intel_drm_winsys_enable_fenced_relocs;
   drm_ws->base.get_info = intel_drm_winsys_get_info;
   drm_ws->base.create_context = intel_drm_winsys_create_context;
   drm_ws->base.destroy_context = intel_drm_winsys_destroy_context;
   drm_ws->base.check_aperture_space = intel_drm_winsys_check_aperture_space;
   drm_ws->base.alloc = intel_drm_winsys_alloc;
   drm_ws->base.alloc_tiled = intel_drm_winsys_alloc_tiled;
   drm_ws->base.alloc_from_handle = intel_drm_winsys_alloc_from_handle;
   drm_ws->base.read_reg = intel_drm_winsys_read_reg;
   drm_ws->base.decode_batch = intel_drm_winsys_decode_batch;

   return &drm_ws->base;
}
