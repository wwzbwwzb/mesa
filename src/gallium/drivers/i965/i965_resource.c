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

#include "util/u_transfer.h"
#include "i965_common.h"
#include "i965_screen.h"
#include "i965_context.h"
#include "i965_cp.h"
#include "i965_resource.h"

static boolean
realloc_bo(struct i965_resource *res)
{
   struct i965_screen *is = i965_screen(res->base.screen);
   struct intel_bo *old_bo = res->bo;
   enum intel_tiling_mode tiling;
   unsigned long stride;
   const char *name;

   /* a shared bo cannot be reallocated */
   if (old_bo && res->handle)
      return FALSE;

   if (res->base.target == PIPE_BUFFER) {
      /* 64 should be enough for all buffer usages */
      const int i965_buffer_alignment = 64;
      const unsigned size = res->bo_width;

      switch (res->base.bind) {
      case PIPE_BIND_VERTEX_BUFFER:
         name = "vertex buffer";
         break;
      case PIPE_BIND_INDEX_BUFFER:
         name = "index buffer";
         break;
      case PIPE_BIND_CONSTANT_BUFFER:
         name = "constant buffer";
         break;
      case PIPE_BIND_STREAM_OUTPUT:
         name = "stream output";
         break;
      default:
         name = "unknown buffer";
         break;
      }

      if (res->handle) {
         res->bo = is->winsys->alloc_from_handle(is->winsys, name,
               res->handle, &tiling, &stride);
         assert(tiling == INTEL_TILING_NONE);
         assert(stride == 0);
         assert(res->bo->get_size(res->bo) == size);
      }
      else {
         res->bo = is->winsys->alloc(is->winsys, name,
               size, i965_buffer_alignment);
         tiling = INTEL_TILING_NONE;
         stride = 0;
      }
   }
   else {
      switch (res->base.target) {
      case PIPE_TEXTURE_1D:
         name = "1D texture";
         break;
      case PIPE_TEXTURE_2D:
         name = "2D texture";
         break;
      case PIPE_TEXTURE_3D:
         name = "3D texture";
         break;
      case PIPE_TEXTURE_CUBE:
         name = "cube texture";
         break;
      case PIPE_TEXTURE_RECT:
         name = "rectangle texture";
         break;
      case PIPE_TEXTURE_1D_ARRAY:
         name = "1D array texture";
         break;
      case PIPE_TEXTURE_2D_ARRAY:
         name = "2D array texture";
         break;
      case PIPE_TEXTURE_CUBE_ARRAY:
         name = "cube array texture";
         break;
      default:
         name ="unknown texture";
         break;
      }

      if (res->handle) {
         res->bo = is->winsys->alloc_from_handle(is->winsys, name,
               res->handle, &tiling, &stride);
      }
      else {
         const boolean for_render =
            !!(res->base.bind & (PIPE_BIND_DEPTH_STENCIL |
                                 PIPE_BIND_RENDER_TARGET));

         tiling = res->tiling;
         res->bo = is->winsys->alloc_tiled(is->winsys, name,
               res->bo_width, res->bo_height, res->bo_cpp,
               &tiling, &stride, for_render);
      }
   }

   if (res->bo) {
      res->tiling = tiling;
      res->bo_stride = stride;

      if (old_bo)
         old_bo->unreference(old_bo);
      return TRUE;
   }
   else {
      res->bo = old_bo;
      return FALSE;
   }
}

/**
 * \see intel_bufferobj_subdata()
 */
static void
i965_transfer_inline_write(struct pipe_context *pipe,
                           struct pipe_resource *r,
                           unsigned level,
                           unsigned usage,
                           const struct pipe_box *box,
                           const void *data,
                           unsigned stride,
                           unsigned layer_stride)
{
   struct i965_context *i965 = i965_context(pipe);
   struct i965_resource *res = i965_resource(r);

   if (res->base.target == PIPE_BUFFER) {
      if (i965->cp->bo->references(i965->cp->bo, res->bo))
         i965_cp_flush(i965->cp);
      res->bo->subdata(res->bo, box->x, box->width, data);
   }
   else {
      u_default_transfer_inline_write(pipe, r,
            level, usage, box, data, stride, layer_stride);
   }
}

/**
 * \see intel_bufferobj_unmap()
 */
static void
i965_transfer_unmap(struct pipe_context *pipe,
                    struct pipe_transfer *transfer)
{
   struct i965_resource *res = i965_resource(transfer->resource);

   res->bo->unmap(res->bo);

   pipe_resource_reference(&transfer->resource, NULL);
   FREE(transfer);
}

/**
 * \see intel_bufferobj_flush_mapped_range()
 */
static void
i965_transfer_flush_region(struct pipe_context *pipe,
                           struct pipe_transfer *transfer,
                           const struct pipe_box *box)
{
}

/**
 * \see intel_miptree_map()
 * \see intel_bufferobj_map_range()
 */
static void *
i965_transfer_map(struct pipe_context *pipe,
                  struct pipe_resource *r,
                  unsigned level,
                  unsigned usage,
                  const struct pipe_box *box,
                  struct pipe_transfer **transfer)
{
   struct i965_context *i965 = i965_context(pipe);
   struct i965_resource *res = i965_resource(r);
   struct pipe_transfer *xfer;
   void *ptr;
   int x, y, err;

   xfer = MALLOC_STRUCT(pipe_transfer);
   if (!xfer)
      return NULL;

   /* sync access by flushing or reallocation */
   if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
      boolean can_discard =
         !!(usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE);

      if (i965->cp->bo->references(i965->cp->bo, res->bo)) {
         if (!can_discard || !realloc_bo(res))
            i965_cp_flush(i965->cp);
      }
      else if (can_discard && res->bo->busy(res->bo)) {
         realloc_bo(res);
      }
   }

   /* mapping a discardable subrange of a busy bo */
   if ((usage & PIPE_TRANSFER_DISCARD_RANGE) &&
       res->bo->busy(res->bo)) {
      /* TODO allocate a scratch bo or system buffer, and blit on unmapping */
   }

   if (res->tiling == INTEL_TILING_NONE) {
      if (usage & PIPE_TRANSFER_UNSYNCHRONIZED)
         err = res->bo->map_unsynchronized(res->bo);
      else if (usage & PIPE_TRANSFER_READ)
         err = res->bo->map(res->bo, !!(usage & PIPE_TRANSFER_WRITE));
      else
         err = res->bo->map_gtt(res->bo);
   }
   else {
      err = res->bo->map_gtt(res->bo);
   }

   /* init transfer */
   if (!err) {
      xfer->resource = NULL;
      pipe_resource_reference(&xfer->resource, &res->base);

      xfer->level = level;
      xfer->usage = usage;
      xfer->box = *box;

      xfer->stride = res->bo_stride;

      if (res->base.array_size > 1) {
         const unsigned qpitch =
            res->slice_offsets[level][1].y - res->slice_offsets[level][0].y;

         xfer->layer_stride = qpitch * xfer->stride;
      }
      else {
         xfer->layer_stride = 0;
      }

      *transfer = xfer;
   }
   else {
      FREE(xfer);
      return NULL;
   }

   x = res->slice_offsets[level][box->z].x;
   y = res->slice_offsets[level][box->z].y;

   x += box->x;
   y += box->y;

   /* in blocks */
   assert(x % res->block_width == 0 && y % res->block_height == 0);
   x /= res->block_width;
   y /= res->block_height;

   ptr = res->bo->get_virtual(res->bo);
   ptr += y * res->bo_stride + x * res->bo_cpp;

   return ptr;
}

static boolean
alloc_slice_offsets(struct i965_resource *res)
{
   int depth, lv;

   depth = 0;
   for (lv = 0; lv <= res->base.last_level; lv++)
      depth += u_minify(res->base.depth0, lv);

   res->slice_offsets[0] =
      CALLOC(depth * res->base.array_size, sizeof(res->slice_offsets[0][0]));
   if (!res->slice_offsets[0])
      return FALSE;

   depth = 0;
   for (lv = 1; lv <= res->base.last_level; lv++) {
      depth += u_minify(res->base.depth0, lv - 1);
      res->slice_offsets[lv] =
         res->slice_offsets[0] + depth * res->base.array_size;
   }

   return TRUE;
}

static void
free_slice_offsets(struct i965_resource *res)
{
   int lv;

   FREE(res->slice_offsets[0]);
   for (lv = 0; lv <= res->base.last_level; lv++)
      res->slice_offsets[lv] = NULL;
}

struct layout_tex_info {
   boolean compressed;
   int block_width, block_height;
   int align_i, align_j;
   int qpitch;

   struct {
      int w, h, d;
   } sizes[PIPE_MAX_TEXTURE_LEVELS];
};

/**
 * Prepare for texture layout.
 */
static void
layout_tex_init(const struct i965_resource *res, struct layout_tex_info *info)
{
   struct i965_screen *is = i965_screen(res->base.screen);
   const struct pipe_resource *templ = &res->base;
   int last_level, lv;

   memset(info, 0, sizeof(*info));

   info->compressed = util_format_is_compressed(templ->format);
   info->block_width = util_format_get_blockwidth(templ->format);
   info->block_height = util_format_get_blockheight(templ->format);

   /*
    * From the Sandy Bridge PRM, volume 1 part 1, page 113:
    *
    *     "format                   align_i     align_j
    *      YUV 4:2:2 formats        4           *see below
    *      BC1-5                    4           4
    *      FXT1                     8           4
    *      all other formats        4           *see below
    *
    *      * align_j = 4 for any depth buffer
    *      * align_j = 2 for separate stencil buffer
    *      * align_j = 4 for any render target surface is multisampled (4x)
    *      * align_j = 4 for any render target surface with Surface Vertical
    *        Alignment = VALIGN_4
    *      * align_j = 2 for any render target surface with Surface Vertical
    *        Alignment = VALIGN_2
    *      * align_j = 2 for all other render target surface
    *      * align_j = 2 for any sampling engine surface with Surface Vertical
    *        Alignment = VALIGN_2
    *      * align_j = 4 for any sampling engine surface with Surface Vertical
    *        Alignment = VALIGN_4
    */
   if (info->compressed) {
      /* this happens to be the case */
      info->align_i = info->block_width;
      info->align_j = info->block_height;
   }
   else {
      if (is->gen >= 7 && templ->format == PIPE_FORMAT_Z16_UNORM)
         info->align_i = 8;
      else
         info->align_i = 4;

      if (util_format_is_depth_or_stencil(templ->format))
         info->align_j = 4;
      else
         info->align_j = 2;
   }

   /* aligned w/h should be padded to block boundaries */
   assert(info->align_i % info->block_width == 0);
   assert(info->align_j % info->block_height == 0);

   /* need level 1 for qpitch */
   if (templ->array_size > 1 && templ->last_level < 1)
      last_level = 1;
   else
      last_level = templ->last_level;

   for (lv = 0; lv <= last_level; lv++) {
      int w, h, d;

      w = u_minify(templ->width0, lv);
      h = u_minify(templ->height0, lv);
      d = u_minify(templ->depth0, lv);

      /*
       * From the Sandy Bridge PRM, volume 1 part 1, page 114:
       *
       *     "Then, if necessary, they are padded out to compression
       *      block boundaries."
       */
      w = align(w, info->block_width);
      h = align(h, info->block_height);

      /*
       * From the Sandy Bridge PRM, volume 1 part 1, page 111:
       *
       *     "If the surface is multisampled (4x), these values must be
       *      adjusted as follows before proceeding:
       *
       *        W_L = ceiling(W_L / 2) * 4
       *        H_L = ceiling(H_L / 2) * 4"
       */
      if (templ->nr_samples > 1) {
         w = align(w, 2) * 2;
         h = align(h, 2) * 2;
      }

      info->sizes[lv].w = w;
      info->sizes[lv].h = h;
      info->sizes[lv].d = d;
   }

   if (templ->array_size > 1) {
      info->qpitch = align(info->sizes[0].h, info->align_j) +
                     align(info->sizes[1].h, info->align_j) +
                     ((is->gen >= 7) ? 12 : 11) * info->align_j;

      /*
       * From the Sandy Bridge PRM, volume 1 part 1, page 115:
       *
       *     "[DevSNB] Errata: Sampler MSAA Qpitch will be 4 greater than the
       *      value calculated in the equation above, for every other odd
       *      Surface Height starting from 1 i.e. 1,5,9,13"
       */
      if (is->gen == 6 && templ->nr_samples > 1 && templ->width0 % 4 == 1)
         info->qpitch += 4;
   }
}

/**
 * Layout a 2D texture.
 *
 * \see i945_miptree_layout_2d()
 * \see brw_miptree_layout_texture_array()
 */
static void
layout_tex_2d(struct i965_resource *res, const struct layout_tex_info *info)
{
   const struct pipe_resource *templ = &res->base;
   unsigned int level_x, level_y;
   int lv;

   res->bo_width = 0;
   if (templ->array_size > 1)
      res->bo_height = info->qpitch * templ->array_size;
   else
      res->bo_height = 0;

   level_x = 0;
   level_y = 0;
   for (lv = 0; lv <= templ->last_level; lv++) {
      const unsigned int level_w = info->sizes[lv].w;
      const unsigned int level_h = info->sizes[lv].h;
      int slice;

      for (slice = 0; slice < templ->array_size; slice++) {
         res->slice_offsets[lv][slice].x = level_x;
         res->slice_offsets[lv][slice].y = level_y + info->qpitch * slice;
      }

      if (res->bo_width < level_x + level_w)
         res->bo_width = level_x + level_w;
      if (res->bo_height < level_y + level_h) {
         /* bo_height should have been determined from qpitch */
         assert(templ->array_size == 1);
         res->bo_height = level_y + level_h;
      }

      /* MIPLAYOUT_BELOW */
      if (lv == 1)
         level_x += align(level_w, info->align_i);
      else
         level_y += align(level_h, info->align_j);
   }
}

/**
 * Layout a 3D texture.
 *
 * \see brw_miptree_layout()
 */
static void
layout_tex_3d(struct i965_resource *res, const struct layout_tex_info *info)
{
   const struct pipe_resource *templ = &res->base;
   unsigned int level_y;
   int lv;

   res->bo_width = 0;
   res->bo_height = 0;

   level_y = 0;
   for (lv = 0; lv <= templ->last_level; lv++) {
      const unsigned int level_w = info->sizes[lv].w;
      const unsigned int level_h = info->sizes[lv].h;
      const unsigned int level_d = info->sizes[lv].d;
      const unsigned int slice_pitch = align(level_w, info->align_i);
      const unsigned int slice_qpitch = align(level_h, info->align_j);
      const unsigned int num_slices_per_row = 1 << lv;
      int slice;

      for (slice = 0; slice < level_d; slice += num_slices_per_row) {
         unsigned int level_x = 0, row_width;
         int i;

         for (i = 0; i < num_slices_per_row; i++) {
            res->slice_offsets[lv][slice + i].x = level_x;
            res->slice_offsets[lv][slice + i].y = level_y;
            level_x += slice_pitch;

            if (slice + i + 1 >= level_d)
               break;
         }

         /* update bo_width */
         row_width = level_x - slice_pitch + level_w;
         if (res->bo_width < row_width)
            res->bo_width = row_width;

         level_y += slice_qpitch;
      }

      /* update bo_height */
      if (lv == templ->last_level)
         res->bo_height = level_y - slice_qpitch + level_h;
   }
}

/**
 * \see intel_miptree_create()
 */
static void
init_texture(struct i965_resource *res)
{
   const enum pipe_format format = res->base.format;
   struct layout_tex_info info;

   layout_tex_init(res, &info);

   res->compressed = info.compressed;
   res->block_width = info.block_width;
   res->block_height = info.block_height;
   res->valign_4 = (info.align_j == 4);

   switch (res->base.target) {
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_1D_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE_ARRAY:
      layout_tex_2d(res, &info);
      break;
   case PIPE_TEXTURE_3D:
      layout_tex_3d(res, &info);
      break;
   default:
      assert(!"unknown resource target");
      break;
   }

   /* in blocks */
   assert(res->bo_width % info.block_width == 0);
   assert(res->bo_height % info.block_height == 0);
   res->bo_width /= info.block_width;
   res->bo_height /= info.block_height;
   res->bo_cpp = util_format_get_blocksize(format);

   if (info.compressed)
      res->tiling = INTEL_TILING_NONE;
   else if (util_format_is_depth_or_stencil(format))
      res->tiling = INTEL_TILING_Y;
   else if (res->base.width0 >= 64)
      res->tiling = INTEL_TILING_X;
   else
      res->tiling = INTEL_TILING_NONE;
}

static void
init_buffer(struct i965_resource *res)
{
   res->compressed = FALSE;
   res->block_width = 1;
   res->block_height = 1;
   res->valign_4 = FALSE;

   res->bo_width = res->base.width0;
   res->bo_height = 1;
   res->bo_cpp = 1;
   res->tiling = INTEL_TILING_NONE;
}

static struct pipe_resource *
resource_create(struct pipe_screen *screen,
                const struct pipe_resource *templ,
                struct winsys_handle *handle)
{
   struct i965_resource *res;

   res = CALLOC_STRUCT(i965_resource);
   if (!res)
      return NULL;

   res->base = *templ;
   res->base.screen = screen;
   pipe_reference_init(&res->base.reference, 1);
   res->handle = handle;

   if (!alloc_slice_offsets(res)) {
      FREE(res);
      return NULL;
   }

   if (templ->target == PIPE_BUFFER)
      init_buffer(res);
   else
      init_texture(res);

   if (!realloc_bo(res)) {
      free_slice_offsets(res);
      FREE(res);
      return NULL;
   }

   return &res->base;
}

static struct pipe_resource *
i965_resource_create(struct pipe_screen *screen,
                     const struct pipe_resource *templ)
{
   return resource_create(screen, templ, NULL);
}

static boolean
i965_resource_get_handle(struct pipe_screen *screen,
                         struct pipe_resource *r,
                         struct winsys_handle *handle)
{
   struct i965_resource *res = i965_resource(r);
   int err;

   err = res->bo->get_handle(res->bo, handle);

   return !err;
}

static struct pipe_resource *
i965_resource_from_handle(struct pipe_screen *screen,
                          const struct pipe_resource *templ,
                          struct winsys_handle *handle)
{
   return resource_create(screen, templ, handle);
}

static void
i965_resource_destroy(struct pipe_screen *screen,
                      struct pipe_resource *r)
{
   struct i965_resource *res = i965_resource(r);

   free_slice_offsets(res);
   res->bo->unreference(res->bo);
   FREE(res);
}

/**
 * Initialize resource-related functions.
 */
void
i965_init_resource_functions(struct i965_screen *is)
{
   is->base.resource_create = i965_resource_create;
   is->base.resource_from_handle = i965_resource_from_handle;
   is->base.resource_get_handle = i965_resource_get_handle;
   is->base.resource_destroy = i965_resource_destroy;
}

/**
 * Initialize transfer-related functions.
 */
void
i965_init_transfer_functions(struct i965_context *i965)
{
   i965->base.transfer_map = i965_transfer_map;
   i965->base.transfer_flush_region = i965_transfer_flush_region;
   i965->base.transfer_unmap = i965_transfer_unmap;
   i965->base.transfer_inline_write = i965_transfer_inline_write;
}

/**
 * Return the offset (in bytes) to a slice within the bo.
 *
 * When tile_aligned is true, the offset is to the tile containing the start
 * address of the slice.  x_offset and y_offset are offsets (in pixels) from
 * the tile start to slice start.  x_offset is always a multiple of 4 and
 * y_offset is always a multiple of 2.
 */
unsigned
i965_resource_get_slice_offset(const struct i965_resource *res,
                               int level, int slice, boolean tile_aligned,
                               unsigned *x_offset, unsigned *y_offset)
{
   const unsigned x = res->slice_offsets[level][slice].x / res->block_width;
   const unsigned y = res->slice_offsets[level][slice].y / res->block_height;
   unsigned tile_w, tile_h, tile_size, row_size;
   unsigned slice_offset;

   /* see the Sandy Bridge PRM, volume 1 part 2, page 24 */

   switch (res->tiling) {
   case INTEL_TILING_NONE:
      tile_w = res->bo_cpp;
      tile_h = 1;
      break;
   case INTEL_TILING_X:
      tile_w = 512;
      tile_h = 8;
      break;
   case INTEL_TILING_Y:
      tile_w = 128;
      tile_h = 32;
      break;
   default:
      assert(!"unknown tiling");
      tile_w = res->bo_cpp;
      tile_h = 1;
      break;
   }

   tile_size = tile_w * tile_h;
   row_size = res->bo_stride * tile_h;

   /*
    * for non-tiled resources, this is equivalent to
    *
    *   slice_offset = y * res->bo_stride + x * res->bo_cpp;
    */
   slice_offset =
      row_size * (y / tile_h) + tile_size * (x * res->bo_cpp / tile_w);

   /*
    * Since res->bo_stride is a multiple of tile_w, slice_offset should be
    * aligned at this point.
    */
   assert(slice_offset % tile_size == 0);

   if (tile_aligned) {
      /*
       * because of the possible values of align_i and align_j in
       * layout_tex_init(), x_offset must be a multiple of 4 and y_offset must
       * be a multiple of 2.
       */
      if (x_offset) {
         assert(tile_w % res->bo_cpp == 0);
         *x_offset = (x % (tile_w / res->bo_cpp)) * res->block_width;
         assert(*x_offset % 4 == 0);
      }
      if (y_offset) {
         *y_offset = (y % tile_h) * res->block_height;
         assert(*y_offset % 2 == 0);
      }
   }
   else {
      const unsigned tx = (x * res->bo_cpp) % tile_w;
      const unsigned ty = y % tile_h;

      switch (res->tiling) {
      case INTEL_TILING_NONE:
         assert(tx == 0 && ty == 0);
         break;
      case INTEL_TILING_X:
         slice_offset += tile_w * ty + tx;
         break;
      case INTEL_TILING_Y:
         slice_offset += tile_h * 16 * (tx / 16) + ty * 16 + (tx % 16);
         break;
      }

      if (x_offset)
         *x_offset = 0;
      if (y_offset)
         *y_offset = 0;
   }

   return slice_offset;
}
