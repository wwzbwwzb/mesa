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

#ifndef I965_SHADER_H
#define I965_SHADER_H

#include "pipe/p_state.h"
#include "i965_context.h"
#include "i965_common.h"

/**
 * A shader variant.  It consists of non-orthogonal states of the pipe context
 * affecting the compilation of a shader.
 */
struct i965_shader_variant {
   union {
      struct {
         int dummy;
      } vs;

      struct {
         int dummy;
      } gs;

      struct {
         boolean flatshade;
         int fb_height;
      } fs;
   } u;

   int num_sampler_views;
   struct {
      unsigned r:3;
      unsigned g:3;
      unsigned b:3;
      unsigned a:3;
   } sampler_view_swizzles[I965_MAX_SAMPLER_VIEWS];

   uint32_t saturate_tex_coords[3];
};

/**
 * A compiled shader.
 */
struct i965_shader {
   struct i965_shader_variant variant;
   /* hash of the shader variant for quicker lookup */
   unsigned hash;

   struct {
      int semantic_names[PIPE_MAX_SHADER_INPUTS];
      int semantic_indices[PIPE_MAX_SHADER_INPUTS];
      int interp[PIPE_MAX_SHADER_INPUTS];
      boolean centroid[PIPE_MAX_SHADER_INPUTS];
      int count;

      int start_grf;
      boolean has_pos;
      boolean has_linear_interp;
      int barycentric_interpolation_mode;
   } in;

   struct {
      int semantic_names[PIPE_MAX_SHADER_OUTPUTS];
      int semantic_indices[PIPE_MAX_SHADER_OUTPUTS];
      int count;

      boolean has_pos;
   } out;

   boolean has_kill;

   void *kernel;
   int kernel_size;

   struct list_head list;

   uint32_t cache_seqno;
   uint32_t cache_offset;
};

/**
 * Information about a shader state.
 */
struct i965_shader_info {
   int type;
   int gen;

   const struct tgsi_token *tokens;

   struct pipe_stream_output_info stream_output;
   struct {
      unsigned req_local_mem;
      unsigned req_private_mem;
      unsigned req_input_mem;
   } compute;

   boolean has_color_interp;
   boolean has_pos;

   uint32_t shadow_samplers;
   int num_samplers;
};

/**
 * A shader state.
 */
struct i965_shader_state {
   struct i965_shader_info info;

   struct list_head variants;
   int num_variants, total_size;

   struct i965_shader *shader;
};

struct i965_shader_cache {
   struct intel_winsys *winsys;
   struct intel_bo *bo;
   int cur, size;
   boolean busy;

   /* starting from 1, incremented whenever a new bo is allocated */
   uint32_t seqno;
};

void
i965_shader_variant_init(struct i965_shader_variant *variant,
                         const struct i965_shader_info *info,
                         const struct i965_context *i965);

struct i965_shader_state *
i965_shader_state_create(const struct i965_context *i965,
                         int type, const void *templ);

void
i965_shader_state_destroy(struct i965_shader_state *state);

struct i965_shader *
i965_shader_state_add_variant(struct i965_shader_state *state,
                              const struct i965_shader_variant *variant);

boolean
i965_shader_state_use_variant(struct i965_shader_state *state,
                              const struct i965_shader_variant *variant);

struct i965_shader_cache *
i965_shader_cache_create(struct intel_winsys *winsys);

void
i965_shader_cache_destroy(struct i965_shader_cache *shc);

void
i965_shader_cache_set(struct i965_shader_cache *shc,
                      struct i965_shader **shaders,
                      int num_shaders);

static INLINE void
i965_shader_cache_mark_busy(struct i965_shader_cache *shc)
{
   if (shc->cur)
      shc->busy = TRUE;
}

struct i965_shader *
i965_shader_compile_vs(const struct i965_shader_state *state,
                       const struct i965_shader_variant *variant);

struct i965_shader *
i965_shader_compile_fs(const struct i965_shader_state *state,
                       const struct i965_shader_variant *variant);

static INLINE struct i965_shader *
i965_shader_compile_gs(const struct i965_shader_state *state,
                       const struct i965_shader_variant *variant)
{
   return CALLOC_STRUCT(i965_shader);
}

static INLINE struct i965_shader *
i965_shader_compile_cs(const struct i965_shader_state *state,
                       const struct i965_shader_variant *variant)
{
   return CALLOC_STRUCT(i965_shader);
}

static INLINE void
i965_shader_destroy(struct i965_shader *sh)
{
   FREE(sh->kernel);
   FREE(sh);
}

#endif /* I965_SHADER_H */
