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

#include "util/u_framebuffer.h"
#include "util/u_helpers.h"
#include "i965_shader.h"
#include "i965_common.h"
#include "i965_context.h"
#include "i965_state.h"

static void
finalize_shader_states(struct i965_context *i965)
{
   const struct {
      struct i965_shader_state *state;
      struct i965_shader *prev_shader;
      uint32_t prev_cache_seqno;
      uint32_t dirty;
      uint32_t deps;
   } sh[PIPE_SHADER_TYPES] = {
      [PIPE_SHADER_VERTEX] = {
         .state = i965->vs,
         .prev_shader = (i965->vs) ? i965->vs->shader : NULL,
         .prev_cache_seqno = (i965->vs) ? i965->vs->shader->cache_seqno : 0,
         .dirty = I965_DIRTY_VS,
         .deps = I965_DIRTY_VERTEX_SAMPLER_VIEWS,
      },
      [PIPE_SHADER_FRAGMENT] = {
         .state = i965->fs,
         .prev_shader = (i965->fs) ? i965->fs->shader : NULL,
         .prev_cache_seqno = (i965->fs) ? i965->fs->shader->cache_seqno : 0,
         .dirty = I965_DIRTY_FS,
         .deps = I965_DIRTY_FRAGMENT_SAMPLER_VIEWS |
                 I965_DIRTY_RASTERIZER |
                 I965_DIRTY_FRAMEBUFFER,
      },
      [PIPE_SHADER_GEOMETRY] = {
         .state = i965->gs,
         .prev_shader = (i965->gs) ? i965->gs->shader : NULL,
         .prev_cache_seqno = (i965->gs) ? i965->gs->shader->cache_seqno : 0,
         .dirty = I965_DIRTY_GS,
         .deps = I965_DIRTY_GEOMETRY_SAMPLER_VIEWS,
      },
      [PIPE_SHADER_COMPUTE] = {
         .state = NULL,
         .prev_shader = NULL,
         .prev_cache_seqno = 0,
         .dirty = 0,
         .deps = 0,
      },
   };
   struct i965_shader *shaders[PIPE_SHADER_TYPES];
   int num_shaders = 0, i;

   for (i = 0; i < PIPE_SHADER_TYPES; i++) {
      /* no state bound */
      if (!sh[i].state)
         continue;

      /* switch variant if the shader or the states it depends on changed */
      if (i965->dirty & (sh[i].dirty | sh[i].deps)) {
         struct i965_shader_variant variant;

         i965_shader_variant_init(&variant, &sh[i].state->info, i965);
         i965_shader_state_use_variant(sh[i].state, &variant);
      }

      shaders[num_shaders++] = sh[i].state->shader;
   }

   i965_shader_cache_set(i965->shader_cache, shaders, num_shaders);

   for (i = 0; i < PIPE_SHADER_TYPES; i++) {
      /* no state bound */
      if (!sh[i].state)
         continue;

      /*
       * mark the shader state dirty if
       *
       *  - a new variant is selected, or
       *  - the kernel is uploaded to a different bo
       */
      if (sh[i].state->shader != sh[i].prev_shader ||
          sh[i].state->shader->cache_seqno != sh[i].prev_cache_seqno)
         i965->dirty |= sh[i].dirty;
   }
}

static void
finalize_constant_buffers(struct i965_context *i965)
{
   int sh;

   if (!(i965->dirty & I965_DIRTY_CONSTANT_BUFFER))
      return;

   for (sh = 0; sh < PIPE_SHADER_TYPES; sh++) {
      int last_cbuf = Elements(i965->constant_buffers[sh].buffers) - 1;

      /* find the last cbuf */
      while (last_cbuf >= 0 &&
             !i965->constant_buffers[sh].buffers[last_cbuf].buffer)
         last_cbuf--;

      i965->constant_buffers[sh].num_buffers = last_cbuf + 1;
   }
}

/**
 * Finalize states.  Some states depend on other states and are
 * incomplete/invalid until finalized.
 */
void
i965_finalize_states(struct i965_context *i965)
{
   finalize_shader_states(i965);
   finalize_constant_buffers(i965);
}

static void *
i965_create_blend_state(struct pipe_context *pipe,
                        const struct pipe_blend_state *state)
{
   struct pipe_blend_state *blend;

   blend = MALLOC_STRUCT(pipe_blend_state);
   assert(blend);

   *blend = *state;

   return blend;
}

static void
i965_bind_blend_state(struct pipe_context *pipe, void *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->blend = state;

   i965->dirty |= I965_DIRTY_BLEND;
}

static void
i965_delete_blend_state(struct pipe_context *pipe, void  *state)
{
   FREE(state);
}

static void *
i965_create_sampler_state(struct pipe_context *pipe,
                          const struct pipe_sampler_state *state)
{
   struct pipe_sampler_state *sampler;

   sampler = MALLOC_STRUCT(pipe_sampler_state);
   assert(sampler);

   *sampler = *state;

   return sampler;
}

static void
bind_samplers(struct i965_context *i965,
              unsigned shader, unsigned start, unsigned count,
              void **samplers, boolean unbind_old)
{
   struct pipe_sampler_state **dst = i965->samplers[shader].samplers;
   unsigned i;

   assert(start + count <= Elements(i965->samplers[shader].samplers));

   if (unbind_old) {
      if (!samplers) {
         start = 0;
         count = 0;
      }

      for (i = 0; i < start; i++)
         dst[i] = NULL;
      for (; i < start + count; i++)
         dst[i] = samplers[i - start];
      for (; i < i965->samplers[shader].num_samplers; i++)
         dst[i] = NULL;

      i965->samplers[shader].num_samplers = start + count;

      return;
   }

   dst += start;
   if (samplers) {
      for (i = 0; i < count; i++)
         dst[i] = samplers[i];
   }
   else {
      for (i = 0; i < count; i++)
         dst[i] = NULL;
   }

   if (i965->samplers[shader].num_samplers <= start + count) {
      count += start;

      while (count > 0 && !i965->samplers[shader].samplers[count - 1])
         count--;

      i965->samplers[shader].num_samplers = count;
   }
}

static void
i965_bind_fragment_sampler_states(struct pipe_context *pipe,
                                  unsigned num_samplers,
                                  void **samplers)
{
   struct i965_context *i965 = i965_context(pipe);

   bind_samplers(i965, PIPE_SHADER_FRAGMENT, 0, num_samplers, samplers, TRUE);
   i965->dirty |= I965_DIRTY_FRAGMENT_SAMPLERS;
}

static void
i965_bind_vertex_sampler_states(struct pipe_context *pipe,
                                unsigned num_samplers,
                                void **samplers)
{
   struct i965_context *i965 = i965_context(pipe);

   bind_samplers(i965, PIPE_SHADER_VERTEX, 0, num_samplers, samplers, TRUE);
   i965->dirty |= I965_DIRTY_VERTEX_SAMPLERS;
}

static void
i965_bind_geometry_sampler_states(struct pipe_context *pipe,
                                  unsigned num_samplers,
                                  void **samplers)
{
   struct i965_context *i965 = i965_context(pipe);

   bind_samplers(i965, PIPE_SHADER_GEOMETRY, 0, num_samplers, samplers, TRUE);
   i965->dirty |= I965_DIRTY_GEOMETRY_SAMPLERS;
}

static void
i965_bind_compute_sampler_states(struct pipe_context *pipe,
                                 unsigned start_slot,
                                 unsigned num_samplers,
                                 void **samplers)
{
   struct i965_context *i965 = i965_context(pipe);

   bind_samplers(i965, PIPE_SHADER_COMPUTE,
         start_slot, num_samplers, samplers, FALSE);
   i965->dirty |= I965_DIRTY_COMPUTE_SAMPLERS;
}

static void
i965_delete_sampler_state(struct pipe_context *pipe, void *state)
{
   FREE(state);
}

static void *
i965_create_rasterizer_state(struct pipe_context *pipe,
                             const struct pipe_rasterizer_state *state)
{
   struct pipe_rasterizer_state *rast;

   rast = MALLOC_STRUCT(pipe_rasterizer_state);
   assert(rast);

   *rast = *state;

   return rast;
}

static void
i965_bind_rasterizer_state(struct pipe_context *pipe, void *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->rasterizer = state;

   i965->dirty |= I965_DIRTY_RASTERIZER;
}

static void
i965_delete_rasterizer_state(struct pipe_context *pipe, void *state)
{
   FREE(state);
}

static void *
i965_create_depth_stencil_alpha_state(struct pipe_context *pipe,
                                      const struct pipe_depth_stencil_alpha_state *state)
{
   struct pipe_depth_stencil_alpha_state *dsa;

   dsa = MALLOC_STRUCT(pipe_depth_stencil_alpha_state);
   assert(dsa);

   *dsa = *state;

   return dsa;
}

static void
i965_bind_depth_stencil_alpha_state(struct pipe_context *pipe, void *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->depth_stencil_alpha = state;

   i965->dirty |= I965_DIRTY_DEPTH_STENCIL_ALPHA;
}

static void
i965_delete_depth_stencil_alpha_state(struct pipe_context *pipe, void *state)
{
   FREE(state);
}

static void *
i965_create_fs_state(struct pipe_context *pipe,
                     const struct pipe_shader_state *state)
{
   struct i965_context *i965 = i965_context(pipe);
   return i965_shader_state_create(i965, PIPE_SHADER_FRAGMENT, state);
}

static void
i965_bind_fs_state(struct pipe_context *pipe, void *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->fs = state;

   i965->dirty |= I965_DIRTY_FS;
}

static void
i965_delete_fs_state(struct pipe_context *pipe, void *state)
{
   struct i965_shader_state *fs = (struct i965_shader_state *) state;
   i965_shader_state_destroy(fs);
}

static void *
i965_create_vs_state(struct pipe_context *pipe,
                     const struct pipe_shader_state *state)
{
   struct i965_context *i965 = i965_context(pipe);
   return i965_shader_state_create(i965, PIPE_SHADER_VERTEX, state);
}

static void
i965_bind_vs_state(struct pipe_context *pipe, void *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->vs = state;

   i965->dirty |= I965_DIRTY_VS;
}

static void
i965_delete_vs_state(struct pipe_context *pipe, void *state)
{
   struct i965_shader_state *vs = (struct i965_shader_state *) state;
   i965_shader_state_destroy(vs);
}

static void *
i965_create_gs_state(struct pipe_context *pipe,
                     const struct pipe_shader_state *state)
{
   struct i965_context *i965 = i965_context(pipe);
   return i965_shader_state_create(i965, PIPE_SHADER_GEOMETRY, state);
}

static void
i965_bind_gs_state(struct pipe_context *pipe, void *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->gs = state;

   i965->dirty |= I965_DIRTY_GS;
}

static void
i965_delete_gs_state(struct pipe_context *pipe, void *state)
{
   struct i965_shader_state *gs = (struct i965_shader_state *) state;
   i965_shader_state_destroy(gs);
}

static void *
i965_create_vertex_elements_state(struct pipe_context *pipe,
                                  unsigned num_elements,
                                  const struct pipe_vertex_element *elements)
{
   struct i965_vertex_element *velem;

   velem = MALLOC_STRUCT(i965_vertex_element);
   assert(velem);

   memcpy(velem->elements, elements, sizeof(*elements) * num_elements);
   velem->num_elements = num_elements;

   return velem;
}

static void
i965_bind_vertex_elements_state(struct pipe_context *pipe, void *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->vertex_elements = state;

   i965->dirty |= I965_DIRTY_VERTEX_ELEMENTS;
}

static void
i965_delete_vertex_elements_state(struct pipe_context *pipe, void *state)
{
   FREE(state);
}

static void
i965_set_blend_color(struct pipe_context *pipe,
                     const struct pipe_blend_color *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->blend_color = *state;

   i965->dirty |= I965_DIRTY_BLEND_COLOR;
}

static void
i965_set_stencil_ref(struct pipe_context *pipe,
                     const struct pipe_stencil_ref *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->stencil_ref = *state;

   i965->dirty |= I965_DIRTY_STENCIL_REF;
}

static void
i965_set_sample_mask(struct pipe_context *pipe,
                     unsigned sample_mask)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->sample_mask = sample_mask;

   i965->dirty |= I965_DIRTY_SAMPLE_MASK;
}

static void
i965_set_clip_state(struct pipe_context *pipe,
                    const struct pipe_clip_state *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->clip = *state;

   i965->dirty |= I965_DIRTY_CLIP;
}

static void
i965_set_constant_buffer(struct pipe_context *pipe,
                         uint shader, uint index,
                         struct pipe_constant_buffer *buf)
{
   struct i965_context *i965 = i965_context(pipe);
   struct pipe_constant_buffer *cbuf;

   assert(shader < Elements(i965->constant_buffers));
   assert(index < Elements(i965->constant_buffers[shader].buffers));

   cbuf = &i965->constant_buffers[shader].buffers[index];

   pipe_resource_reference(&cbuf->buffer, NULL);

   if (buf) {
      pipe_resource_reference(&cbuf->buffer, buf->buffer);
      cbuf->buffer_offset = buf->buffer_offset;
      cbuf->buffer_size = buf->buffer_size;
      cbuf->user_buffer = buf->user_buffer;
   }
   else {
      cbuf->buffer_offset = 0;
      cbuf->buffer_size = 0;
      cbuf->user_buffer = 0;
   }

   /* the correct value will be set in i965_finalize_states() */
   i965->constant_buffers[shader].num_buffers = 0;

   i965->dirty |= I965_DIRTY_CONSTANT_BUFFER;
}

static void
i965_set_framebuffer_state(struct pipe_context *pipe,
                           const struct pipe_framebuffer_state *state)
{
   struct i965_context *i965 = i965_context(pipe);

   util_copy_framebuffer_state(&i965->framebuffer, state);

   i965->dirty |= I965_DIRTY_FRAMEBUFFER;
}

static void
i965_set_polygon_stipple(struct pipe_context *pipe,
                         const struct pipe_poly_stipple *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->poly_stipple = *state;

   i965->dirty |= I965_DIRTY_POLY_STIPPLE;
}

static void
i965_set_scissor_state(struct pipe_context *pipe,
                       const struct pipe_scissor_state *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->scissor = *state;

   i965->dirty |= I965_DIRTY_SCISSOR;
}

static void
i965_set_viewport_state(struct pipe_context *pipe,
                        const struct pipe_viewport_state *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->viewport = *state;

   i965->dirty |= I965_DIRTY_VIEWPORT;
}

static void
set_sampler_views(struct i965_context *i965,
                  unsigned shader, unsigned start, unsigned count,
                  struct pipe_sampler_view **views, boolean unset_old)
{
   struct pipe_sampler_view **dst = i965->sampler_views[shader].views;
   unsigned i;

   assert(start + count <= Elements(i965->sampler_views[shader].views));

   if (unset_old) {
      if (!views) {
         start = 0;
         count = 0;
      }

      for (i = 0; i < start; i++)
         pipe_sampler_view_reference(&dst[i], NULL);
      for (; i < start + count; i++)
         pipe_sampler_view_reference(&dst[i], views[i - start]);
      for (; i < i965->sampler_views[shader].num_views; i++)
         pipe_sampler_view_reference(&dst[i], NULL);

      i965->sampler_views[shader].num_views = start + count;

      return;
   }

   dst += start;
   if (views) {
      for (i = 0; i < count; i++)
         pipe_sampler_view_reference(&dst[i], views[i]);
   }
   else {
      for (i = 0; i < count; i++)
         pipe_sampler_view_reference(&dst[i], NULL);
   }

   if (i965->sampler_views[shader].num_views <= start + count) {
      count += start;

      while (count > 0 && !i965->sampler_views[shader].views[count - 1])
         count--;

      i965->sampler_views[shader].num_views = count;
   }
}

static void
i965_set_fragment_sampler_views(struct pipe_context *pipe,
                                unsigned num_views,
                                struct pipe_sampler_view **views)
{
   struct i965_context *i965 = i965_context(pipe);

   set_sampler_views(i965, PIPE_SHADER_FRAGMENT, 0, num_views, views, TRUE);
   i965->dirty |= I965_DIRTY_FRAGMENT_SAMPLER_VIEWS;
}

static void
i965_set_vertex_sampler_views(struct pipe_context *pipe,
                              unsigned num_views,
                              struct pipe_sampler_view **views)
{
   struct i965_context *i965 = i965_context(pipe);

   set_sampler_views(i965, PIPE_SHADER_VERTEX, 0, num_views, views, TRUE);
   i965->dirty |= I965_DIRTY_VERTEX_SAMPLER_VIEWS;
}

static void
i965_set_geometry_sampler_views(struct pipe_context *pipe,
                                unsigned num_views,
                                struct pipe_sampler_view **views)
{
   struct i965_context *i965 = i965_context(pipe);

   set_sampler_views(i965, PIPE_SHADER_GEOMETRY, 0, num_views, views, TRUE);
   i965->dirty |= I965_DIRTY_GEOMETRY_SAMPLER_VIEWS;
}

static void
i965_set_compute_sampler_views(struct pipe_context *pipe,
                               unsigned start_slot, unsigned num_views,
                               struct pipe_sampler_view **views)
{
   struct i965_context *i965 = i965_context(pipe);

   set_sampler_views(i965, PIPE_SHADER_COMPUTE,
         start_slot, num_views, views, FALSE);

   i965->dirty |= I965_DIRTY_COMPUTE_SAMPLER_VIEWS;
}

static void
i965_set_shader_resources(struct pipe_context *pipe,
                          unsigned start, unsigned count,
                          struct pipe_surface **surfaces)
{
   struct i965_context *i965 = i965_context(pipe);
   struct pipe_surface **dst = i965->shader_resources.surfaces;
   unsigned i;

   assert(start + count <= Elements(i965->shader_resources.surfaces));

   dst += start;
   if (surfaces) {
      for (i = 0; i < count; i++)
         pipe_surface_reference(&dst[i], surfaces[i]);
   }
   else {
      for (i = 0; i < count; i++)
         pipe_surface_reference(&dst[i], NULL);
   }

   if (i965->shader_resources.num_surfaces <= start + count) {
      count += start;

      while (count > 0 && !i965->shader_resources.surfaces[count - 1])
         count--;

      i965->shader_resources.num_surfaces = count;
   }

   i965->dirty |= I965_DIRTY_SHADER_RESOURCES;
}

static void
i965_set_vertex_buffers(struct pipe_context *pipe,
                        unsigned start_slot, unsigned num_buffers,
                        const struct pipe_vertex_buffer *buffers)
{
   struct i965_context *i965 = i965_context(pipe);

   util_set_vertex_buffers_count(i965->vertex_buffers.buffers,
         &i965->vertex_buffers.num_buffers, buffers, start_slot, num_buffers);

   i965->dirty |= I965_DIRTY_VERTEX_BUFFERS;
}

static void
i965_set_index_buffer(struct pipe_context *pipe,
                      const struct pipe_index_buffer *state)
{
   struct i965_context *i965 = i965_context(pipe);

   if (state) {
      i965->index_buffer.index_size = state->index_size;
      i965->index_buffer.offset = state->offset;
      pipe_resource_reference(&i965->index_buffer.buffer, state->buffer);
      i965->index_buffer.user_buffer = state->user_buffer;
   }
   else {
      i965->index_buffer.index_size = 0;
      i965->index_buffer.offset = 0;
      pipe_resource_reference(&i965->index_buffer.buffer, NULL);
      i965->index_buffer.user_buffer = NULL;
   }

   i965->dirty |= I965_DIRTY_INDEX_BUFFER;
}

static struct pipe_stream_output_target *
i965_create_stream_output_target(struct pipe_context *pipe,
                                 struct pipe_resource *res,
                                 unsigned buffer_offset,
                                 unsigned buffer_size)
{
   struct pipe_stream_output_target *target;

   target = MALLOC_STRUCT(pipe_stream_output_target);
   assert(target);

   pipe_reference_init(&target->reference, 1);
   target->buffer = NULL;
   pipe_resource_reference(&target->buffer, res);
   target->context = pipe;
   target->buffer_offset = buffer_offset;
   target->buffer_size = buffer_size;

   return target;
}

static void
i965_set_stream_output_targets(struct pipe_context *pipe,
                               unsigned num_targets,
                               struct pipe_stream_output_target **targets,
                               unsigned append_bitmask)
{
   struct i965_context *i965 = i965_context(pipe);
   unsigned i;

   if (!targets)
      num_targets = 0;

   for (i = 0; i < num_targets; i++) {
      pipe_so_target_reference(&i965->stream_output_targets.targets[i],
                               targets[i]);
   }
   for (; i < i965->stream_output_targets.num_targets; i++)
      pipe_so_target_reference(&i965->stream_output_targets.targets[i], NULL);

   i965->stream_output_targets.num_targets = num_targets;
   i965->stream_output_targets.append_bitmask = append_bitmask;

   i965->dirty |= I965_DIRTY_STREAM_OUTPUT_TARGETS;
}

static void
i965_stream_output_target_destroy(struct pipe_context *pipe,
                                  struct pipe_stream_output_target *target)
{
   pipe_resource_reference(&target->buffer, NULL);
   FREE(target);
}

static struct pipe_sampler_view *
i965_create_sampler_view(struct pipe_context *pipe,
                         struct pipe_resource *res,
                         const struct pipe_sampler_view *templ)
{
   struct pipe_sampler_view *view;

   view = MALLOC_STRUCT(pipe_sampler_view);
   assert(view);

   *view = *templ;
   pipe_reference_init(&view->reference, 1);
   view->texture = NULL;
   pipe_resource_reference(&view->texture, res);
   view->context = pipe;

   return view;
}

static void
i965_sampler_view_destroy(struct pipe_context *pipe,
                          struct pipe_sampler_view *view)
{
   pipe_resource_reference(&view->texture, NULL);
   FREE(view);
}

static struct pipe_surface *
i965_create_surface(struct pipe_context *pipe,
                    struct pipe_resource *res,
                    const struct pipe_surface *templ)
{
   struct pipe_surface *surface;

   surface = MALLOC_STRUCT(pipe_surface);
   assert(surface);

   *surface = *templ;
   pipe_reference_init(&surface->reference, 1);
   surface->texture = NULL;
   pipe_resource_reference(&surface->texture, res);

   surface->context = pipe;
   surface->width = u_minify(res->width0, surface->u.tex.level);
   surface->height = u_minify(res->height0, surface->u.tex.level);

   return surface;
}

static void
i965_surface_destroy(struct pipe_context *pipe,
                     struct pipe_surface *surface)
{
   pipe_resource_reference(&surface->texture, NULL);
   FREE(surface);
}

static void *
i965_create_compute_state(struct pipe_context *pipe,
                          const struct pipe_compute_state *state)
{
   struct i965_context *i965 = i965_context(pipe);
   return i965_shader_state_create(i965, PIPE_SHADER_COMPUTE, state);
}

static void
i965_bind_compute_state(struct pipe_context *pipe, void *state)
{
   struct i965_context *i965 = i965_context(pipe);

   i965->compute = state;

   i965->dirty |= I965_DIRTY_COMPUTE;
}

static void
i965_delete_compute_state(struct pipe_context *pipe, void *state)
{
   struct i965_shader_state *cs = (struct i965_shader_state *) state;
   i965_shader_state_destroy(cs);
}

static void
i965_set_compute_resources(struct pipe_context *pipe,
                           unsigned start, unsigned count,
                           struct pipe_surface **surfaces)
{
   struct i965_context *i965 = i965_context(pipe);
   struct pipe_surface **dst = i965->compute_resources.surfaces;
   unsigned i;

   assert(start + count <= Elements(i965->compute_resources.surfaces));

   dst += start;
   if (surfaces) {
      for (i = 0; i < count; i++)
         pipe_surface_reference(&dst[i], surfaces[i]);
   }
   else {
      for (i = 0; i < count; i++)
         pipe_surface_reference(&dst[i], NULL);
   }

   if (i965->compute_resources.num_surfaces <= start + count) {
      count += start;

      while (count > 0 && !i965->compute_resources.surfaces[count - 1])
         count--;

      i965->compute_resources.num_surfaces = count;
   }

   i965->dirty |= I965_DIRTY_COMPUTE_RESOURCES;
}

static void
i965_set_global_binding(struct pipe_context *pipe,
                        unsigned start, unsigned count,
                        struct pipe_resource **resources,
                        uint32_t **handles)
{
   struct i965_context *i965 = i965_context(pipe);
   struct pipe_resource **dst = i965->global_binding.resources;
   unsigned i;

   assert(start + count <= Elements(i965->global_binding.resources));

   dst += start;
   if (resources) {
      for (i = 0; i < count; i++)
         pipe_resource_reference(&dst[i], resources[i]);
   }
   else {
      for (i = 0; i < count; i++)
         pipe_resource_reference(&dst[i], NULL);
   }

   if (i965->global_binding.num_resources <= start + count) {
      count += start;

      while (count > 0 && !i965->global_binding.resources[count - 1])
         count--;

      i965->global_binding.num_resources = count;
   }

   i965->dirty |= I965_DIRTY_GLOBAL_BINDING;
}

/**
 * Initialize state-related functions.
 */
void
i965_init_state_functions(struct i965_context *i965)
{
   STATIC_ASSERT(I965_STATE_COUNT <= 32);

   i965->base.create_blend_state = i965_create_blend_state;
   i965->base.bind_blend_state = i965_bind_blend_state;
   i965->base.delete_blend_state = i965_delete_blend_state;
   i965->base.create_sampler_state = i965_create_sampler_state;
   i965->base.bind_fragment_sampler_states = i965_bind_fragment_sampler_states;
   i965->base.bind_vertex_sampler_states = i965_bind_vertex_sampler_states;
   i965->base.bind_geometry_sampler_states = i965_bind_geometry_sampler_states;
   i965->base.bind_compute_sampler_states = i965_bind_compute_sampler_states;
   i965->base.delete_sampler_state = i965_delete_sampler_state;
   i965->base.create_rasterizer_state = i965_create_rasterizer_state;
   i965->base.bind_rasterizer_state = i965_bind_rasterizer_state;
   i965->base.delete_rasterizer_state = i965_delete_rasterizer_state;
   i965->base.create_depth_stencil_alpha_state = i965_create_depth_stencil_alpha_state;
   i965->base.bind_depth_stencil_alpha_state = i965_bind_depth_stencil_alpha_state;
   i965->base.delete_depth_stencil_alpha_state = i965_delete_depth_stencil_alpha_state;
   i965->base.create_fs_state = i965_create_fs_state;
   i965->base.bind_fs_state = i965_bind_fs_state;
   i965->base.delete_fs_state = i965_delete_fs_state;
   i965->base.create_vs_state = i965_create_vs_state;
   i965->base.bind_vs_state = i965_bind_vs_state;
   i965->base.delete_vs_state = i965_delete_vs_state;
   i965->base.create_gs_state = i965_create_gs_state;
   i965->base.bind_gs_state = i965_bind_gs_state;
   i965->base.delete_gs_state = i965_delete_gs_state;
   i965->base.create_vertex_elements_state = i965_create_vertex_elements_state;
   i965->base.bind_vertex_elements_state = i965_bind_vertex_elements_state;
   i965->base.delete_vertex_elements_state = i965_delete_vertex_elements_state;

   i965->base.set_blend_color = i965_set_blend_color;
   i965->base.set_stencil_ref = i965_set_stencil_ref;
   i965->base.set_sample_mask = i965_set_sample_mask;
   i965->base.set_clip_state = i965_set_clip_state;
   i965->base.set_constant_buffer = i965_set_constant_buffer;
   i965->base.set_framebuffer_state = i965_set_framebuffer_state;
   i965->base.set_polygon_stipple = i965_set_polygon_stipple;
   i965->base.set_scissor_state = i965_set_scissor_state;
   i965->base.set_viewport_state = i965_set_viewport_state;
   i965->base.set_fragment_sampler_views = i965_set_fragment_sampler_views;
   i965->base.set_vertex_sampler_views = i965_set_vertex_sampler_views;
   i965->base.set_geometry_sampler_views = i965_set_geometry_sampler_views;
   i965->base.set_compute_sampler_views = i965_set_compute_sampler_views;
   i965->base.set_shader_resources = i965_set_shader_resources;
   i965->base.set_vertex_buffers = i965_set_vertex_buffers;
   i965->base.set_index_buffer = i965_set_index_buffer;

   i965->base.create_stream_output_target = i965_create_stream_output_target;
   i965->base.stream_output_target_destroy = i965_stream_output_target_destroy;
   i965->base.set_stream_output_targets = i965_set_stream_output_targets;

   i965->base.create_sampler_view = i965_create_sampler_view;
   i965->base.sampler_view_destroy = i965_sampler_view_destroy;

   i965->base.create_surface = i965_create_surface;
   i965->base.surface_destroy = i965_surface_destroy;

   i965->base.create_compute_state = i965_create_compute_state;
   i965->base.bind_compute_state = i965_bind_compute_state;
   i965->base.delete_compute_state = i965_delete_compute_state;
   i965->base.set_compute_resources = i965_set_compute_resources;
   i965->base.set_global_binding = i965_set_global_binding;
}
