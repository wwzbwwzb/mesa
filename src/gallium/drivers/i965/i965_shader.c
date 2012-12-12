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

#include "tgsi/tgsi_parse.h"
#include "i965_common.h"
#include "i965_shader.h"

/**
 * Initialize a shader variant.
 */
void
i965_shader_variant_init(struct i965_shader_variant *variant,
                         const struct i965_shader_info *info,
                         const struct i965_context *i965)
{
   int num_views, i;

   memset(variant, 0, sizeof(*variant));

   switch (info->type) {
   case PIPE_SHADER_VERTEX:
      break;
   case PIPE_SHADER_GEOMETRY:
      break;
   case PIPE_SHADER_FRAGMENT:
      variant->u.fs.flatshade =
         (info->has_color_interp && i965->rasterizer->flatshade);
      variant->u.fs.fb_height = (info->has_pos) ?
         i965->framebuffer.height : 1;
      break;
   default:
      assert(!"unknown shader type");
      break;
   }

   num_views = i965->sampler_views[info->type].num_views;
   assert(info->num_samplers <= num_views);

   variant->num_sampler_views = info->num_samplers;
   for (i = 0; i < info->num_samplers; i++) {
      const struct pipe_sampler_view *view =
         i965->sampler_views[info->type].views[i];
      const struct pipe_sampler_state *sampler =
         i965->samplers[info->type].samplers[i];

      if (view) {
         variant->sampler_view_swizzles[i].r = view->swizzle_r;
         variant->sampler_view_swizzles[i].g = view->swizzle_g;
         variant->sampler_view_swizzles[i].b = view->swizzle_b;
         variant->sampler_view_swizzles[i].a = view->swizzle_a;
      }
      else if (info->shadow_samplers & (1 << i)) {
         variant->sampler_view_swizzles[i].r = PIPE_SWIZZLE_RED;
         variant->sampler_view_swizzles[i].g = PIPE_SWIZZLE_RED;
         variant->sampler_view_swizzles[i].b = PIPE_SWIZZLE_RED;
         variant->sampler_view_swizzles[i].a = PIPE_SWIZZLE_ONE;
      }
      else {
         variant->sampler_view_swizzles[i].r = PIPE_SWIZZLE_RED;
         variant->sampler_view_swizzles[i].g = PIPE_SWIZZLE_GREEN;
         variant->sampler_view_swizzles[i].b = PIPE_SWIZZLE_BLUE;
         variant->sampler_view_swizzles[i].a = PIPE_SWIZZLE_ALPHA;
      }

      /*
       * When non-nearest filter and PIPE_TEX_WRAP_CLAMP wrap mode is used,
       * the HW wrap mode is set to BRW_TEXCOORDMODE_CLAMP_BORDER, and we need
       * to manually saturate the texture coordinates.
       */
      if ((sampler->min_img_filter != PIPE_TEX_FILTER_NEAREST ||
           sampler->min_mip_filter != PIPE_TEX_MIPFILTER_NONE) &&
          (sampler->mag_img_filter != PIPE_TEX_FILTER_NEAREST ||
           sampler->max_anisotropy)) {
         if (sampler->wrap_s == PIPE_TEX_WRAP_CLAMP)
            variant->saturate_tex_coords[0] |= 1 << i;
         if (sampler->wrap_t == PIPE_TEX_WRAP_CLAMP)
            variant->saturate_tex_coords[1] |= 1 << i;
         if (sampler->wrap_r == PIPE_TEX_WRAP_CLAMP)
            variant->saturate_tex_coords[2] |= 1 << i;
      }
   }
}

/**
 * Guess the shader variant, knowing that the context may still change.
 */
static void
i965_shader_variant_guess(struct i965_shader_variant *variant,
                          const struct i965_shader_info *info,
                          const struct i965_context *i965)
{
   int i;

   memset(variant, 0, sizeof(*variant));

   switch (info->type) {
   case PIPE_SHADER_VERTEX:
      break;
   case PIPE_SHADER_GEOMETRY:
      break;
   case PIPE_SHADER_FRAGMENT:
      variant->u.fs.flatshade = FALSE;
      variant->u.fs.fb_height = (info->has_pos) ?
         i965->framebuffer.height : 1;
      break;
   default:
      assert(!"unknown shader type");
      break;
   }

   variant->num_sampler_views = info->num_samplers;
   for (i = 0; i < info->num_samplers; i++) {
      if (info->shadow_samplers & (1 << i)) {
         variant->sampler_view_swizzles[i].r = PIPE_SWIZZLE_RED;
         variant->sampler_view_swizzles[i].g = PIPE_SWIZZLE_RED;
         variant->sampler_view_swizzles[i].b = PIPE_SWIZZLE_RED;
         variant->sampler_view_swizzles[i].a = PIPE_SWIZZLE_ONE;
      }
      else {
         variant->sampler_view_swizzles[i].r = PIPE_SWIZZLE_RED;
         variant->sampler_view_swizzles[i].g = PIPE_SWIZZLE_GREEN;
         variant->sampler_view_swizzles[i].b = PIPE_SWIZZLE_BLUE;
         variant->sampler_view_swizzles[i].a = PIPE_SWIZZLE_ALPHA;
      }
   }
}

/**
 * Hash a shader variant.
 */
static unsigned int
i965_shader_variant_hash(const struct i965_shader_variant *variant)
{
   const int num_bytes = sizeof(*variant);
   const unsigned char *bytes = (const unsigned char *) variant;
   const unsigned int seed = 131;
   unsigned int hash = 0;
   int i;

   for (i = 0; i < num_bytes; i++)
      hash = hash * seed + bytes[i];

   return hash;
}

/**
 * Parse a TGSI instruction for the shader info.
 */
static void
i965_shader_info_parse_inst(struct i965_shader_info *info,
                            const struct tgsi_full_instruction *inst)
{
   int i;

   if (inst->Instruction.Texture) {
      boolean shadow;

      switch (inst->Texture.Texture) {
      case TGSI_TEXTURE_SHADOW1D:
      case TGSI_TEXTURE_SHADOW2D:
      case TGSI_TEXTURE_SHADOWRECT:
      case TGSI_TEXTURE_SHADOW1D_ARRAY:
      case TGSI_TEXTURE_SHADOW2D_ARRAY:
      case TGSI_TEXTURE_SHADOWCUBE:
      case TGSI_TEXTURE_SHADOWCUBE_ARRAY:
         shadow = TRUE;
         break;
      default:
         shadow = FALSE;
         break;
      }

      for (i = 0; i < inst->Instruction.NumSrcRegs; i++) {
         const struct tgsi_full_src_register *src = &inst->Src[i];

         if (src->Register.File == TGSI_FILE_SAMPLER) {
            const int idx = src->Register.Index;

            if (idx >= info->num_samplers)
               info->num_samplers = idx + 1;

            if (shadow)
               info->shadow_samplers |= 1 << idx;
         }
      }
   }
}

/**
 * Parse a TGSI declaration for the shader info.
 */
static void
i965_shader_info_parse_decl(struct i965_shader_info *info,
                            const struct tgsi_full_declaration *decl)
{
   switch (decl->Declaration.File) {
   case TGSI_FILE_INPUT:
      if (decl->Declaration.Interpolate &&
          decl->Interp.Interpolate == TGSI_INTERPOLATE_COLOR)
         info->has_color_interp = TRUE;
      if (decl->Declaration.Semantic &&
          decl->Semantic.Name == TGSI_SEMANTIC_POSITION)
         info->has_pos = TRUE;
      break;
   default:
      break;
   }
}

static void
i965_shader_info_parse_tokens(struct i965_shader_info *info)
{
   struct tgsi_parse_context parse;

   tgsi_parse_init(&parse, info->tokens);
   while (!tgsi_parse_end_of_tokens(&parse)) {
      const union tgsi_full_token *token;

      tgsi_parse_token(&parse);
      token = &parse.FullToken;

      switch (token->Token.Type) {
      case TGSI_TOKEN_TYPE_DECLARATION:
         i965_shader_info_parse_decl(info, &token->FullDeclaration);
         break;
      case TGSI_TOKEN_TYPE_INSTRUCTION:
         i965_shader_info_parse_inst(info, &token->FullInstruction);
         break;
      default:
         break;
      }
   }
   tgsi_parse_free(&parse);
}

/**
 * Create a shader state.
 */
struct i965_shader_state *
i965_shader_state_create(const struct i965_context *i965,
                         int type, const void *templ)
{
   struct i965_shader_state *state;
   struct i965_shader_variant variant;

   state = CALLOC_STRUCT(i965_shader_state);
   if (!state)
      return NULL;

   state->info.type = type;
   state->info.gen = i965->gen;

   if (type == PIPE_SHADER_COMPUTE) {
      const struct pipe_compute_state *c =
         (const struct pipe_compute_state *) templ;

      state->info.tokens = tgsi_dup_tokens(c->prog);
      state->info.compute.req_local_mem = c->req_local_mem;
      state->info.compute.req_private_mem = c->req_private_mem;
      state->info.compute.req_input_mem = c->req_input_mem;
   }
   else {
      const struct pipe_shader_state *s =
         (const struct pipe_shader_state *) templ;

      state->info.tokens = tgsi_dup_tokens(s->tokens);
      state->info.stream_output = s->stream_output;
   }

   list_inithead(&state->variants);

   i965_shader_info_parse_tokens(&state->info);

   /* guess and compile now */
   i965_shader_variant_guess(&variant, &state->info, i965);
   if (!i965_shader_state_use_variant(state, &variant)) {
      i965_shader_state_destroy(state);
      return NULL;
   }

   return state;
}

/**
 * Destroy a shader state.
 */
void
i965_shader_state_destroy(struct i965_shader_state *state)
{
   struct i965_shader *sh, *next;

   LIST_FOR_EACH_ENTRY_SAFE(sh, next, &state->variants, list)
      i965_shader_destroy(sh);

   FREE((struct tgsi_token *) state->info.tokens);
   FREE(state);
}

/**
 * Add a compiled shader to the shader state.
 */
static void
i965_shader_state_add_shader(struct i965_shader_state *state,
                             struct i965_shader *sh)
{
   list_add(&sh->list, &state->variants);
   state->num_variants++;
   state->total_size += sh->kernel_size;
}

/**
 * Remove a compiled shader from the shader state.
 */
static void
i965_shader_state_remove_shader(struct i965_shader_state *state,
                                struct i965_shader *sh)
{
   list_del(&sh->list);
   state->num_variants--;
   state->total_size -= sh->kernel_size;
}

/**
 * Garbage collect shader variants in the shader state.
 */
static void
i965_shader_state_gc(struct i965_shader_state *state)
{
   /* activate when the variants take up more than 4KiB of space */
   const int limit = 4 * 1024;
   struct i965_shader *sh, *next;

   if (state->total_size < limit)
      return;

   /* remove from the tail as the most recently ones are at the head */
   LIST_FOR_EACH_ENTRY_SAFE_REV(sh, next, &state->variants, list) {
      i965_shader_state_remove_shader(state, sh);
      i965_shader_destroy(sh);

      if (state->total_size <= limit / 2)
         break;
   }
}

/**
 * Search for a shader variant.
 */
static struct i965_shader *
i965_shader_state_search_variant(struct i965_shader_state *state,
                                 unsigned int hash,
                                 const struct i965_shader_variant *variant)
{
   struct i965_shader *sh = NULL, *tmp;

   LIST_FOR_EACH_ENTRY(tmp, &state->variants, list) {
      if (tmp->hash == hash &&
          memcmp(&tmp->variant, variant, sizeof(*variant)) == 0) {
         sh = tmp;
         break;
      }
   }

   return sh;
}

/**
 * Add a shader variant to the shader state.
 */
struct i965_shader *
i965_shader_state_add_variant(struct i965_shader_state *state,
                              const struct i965_shader_variant *variant)
{
   const unsigned int hash = i965_shader_variant_hash(variant);
   struct i965_shader *sh;

   sh = i965_shader_state_search_variant(state, hash, variant);
   if (sh)
      return sh;

   i965_shader_state_gc(state);

   switch (state->info.type) {
   case PIPE_SHADER_VERTEX:
      sh = i965_shader_compile_vs(state, variant);
      break;
   case PIPE_SHADER_FRAGMENT:
      sh = i965_shader_compile_fs(state, variant);
      break;
   case PIPE_SHADER_GEOMETRY:
      sh = i965_shader_compile_gs(state, variant);
      break;
   case PIPE_SHADER_COMPUTE:
      sh = i965_shader_compile_cs(state, variant);
      break;
   default:
      sh = NULL;
      break;
   }
   if (!sh)
      return NULL;

   sh->variant = *variant;
   sh->hash = hash;

   i965_shader_state_add_shader(state, sh);

   return sh;
}

/**
 * Update state->shader to point to a variant.  If the variant does not exist,
 * it will be added first.
 */
boolean
i965_shader_state_use_variant(struct i965_shader_state *state,
                              const struct i965_shader_variant *variant)
{
   struct i965_shader *sh;

   sh = i965_shader_state_add_variant(state, variant);
   if (!sh)
      return FALSE;

   /* move to head */
   if (state->variants.next != &sh->list) {
      list_del(&sh->list);
      list_add(&sh->list, &state->variants);
   }

   state->shader = sh;

   return TRUE;
}

/**
 * Reset the shader cache.
 */
static void
i965_shader_cache_reset(struct i965_shader_cache *shc)
{
   if (shc->bo)
      shc->bo->unreference(shc->bo);

   shc->bo = shc->winsys->alloc(shc->winsys, "shader cache", shc->size, 64);
   shc->busy = FALSE;
   shc->cur = 0;
   shc->seqno++;
   if (!shc->seqno)
      shc->seqno = 1;
}

/**
 * Create a shader cache.  A shader cache is a bo holding all compiled shaders.
 * When the bo is full, a larger bo is allocated and all cached shaders are
 * invalidated.  This is how outdated shaders get dropped.  Active shaders
 * will be added to the new bo when used.
 */
struct i965_shader_cache *
i965_shader_cache_create(struct intel_winsys *winsys)
{
   struct i965_shader_cache *shc;

   shc = CALLOC_STRUCT(i965_shader_cache);
   if (!shc)
      return NULL;

   shc->winsys = winsys;
   /* initial cache size */
   shc->size = 4096;

   i965_shader_cache_reset(shc);

   return shc;
}

/**
 * Destroy a shader cache.
 */
void
i965_shader_cache_destroy(struct i965_shader_cache *shc)
{
   if (shc->bo)
      shc->bo->unreference(shc->bo);

   FREE(shc);
}

/**
 * Add shaders to the cache.  This may invalidate all other shaders in the
 * cache.
 */
void
i965_shader_cache_set(struct i965_shader_cache *shc,
                      struct i965_shader **shaders,
                      int num_shaders)
{
   int new_cur, i;

   /* calculate the space needed */
   new_cur = shc->cur;
   for (i = 0; i < num_shaders; i++) {
      if (shaders[i]->cache_seqno != shc->seqno)
         new_cur = align(new_cur, 64) + shaders[i]->kernel_size;
   }

   /* all shaders are already in the cache */
   if (new_cur == shc->cur)
      return;

   /*
    * From the Sandy Bridge PRM, volume 4 part 2, page 112:
    *
    *     "Due to prefetch of the instruction stream, the EUs may attempt to
    *      access up to 8 instructions (128 bytes) beyond the end of the kernel
    *      program – possibly into the next memory page.  Although these
    *      instructions will not be executed, software must account for the
    *      prefetch in order to avoid invalid page access faults."
    */
   new_cur += 128;

   /*
    * we should be able to append data without being blocked even the bo
    * is busy...
    */

   /* reallocate when the cache is full or busy */
   if (new_cur > shc->size || shc->busy) {
      while (new_cur > shc->size)
         shc->size <<= 1;

      i965_shader_cache_reset(shc);
   }

   /* upload now */
   for (i = 0; i < num_shaders; i++) {
      if (shaders[i]->cache_seqno != shc->seqno) {
         /* kernels must be aligned to 64-byte */
         shc->cur = align(shc->cur, 64);
         shc->bo->subdata(shc->bo, shc->cur,
               shaders[i]->kernel_size, shaders[i]->kernel);

         shaders[i]->cache_seqno = shc->seqno;
         shaders[i]->cache_offset = shc->cur;

         shc->cur += shaders[i]->kernel_size;
      }
   }
}
