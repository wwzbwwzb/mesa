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

#include "i965_common.h"
#include "i965_context.h"
#include "i965_state.h"

/**
 * Initialize state-related functions.
 */
void
i965_init_state_functions(struct i965_context *i965)
{
   i965->base.create_blend_state = NULL;
   i965->base.bind_blend_state = NULL;
   i965->base.delete_blend_state = NULL;
   i965->base.create_sampler_state = NULL;
   i965->base.bind_fragment_sampler_states = NULL;
   i965->base.bind_vertex_sampler_states = NULL;
   i965->base.bind_geometry_sampler_states = NULL;
   i965->base.bind_compute_sampler_states = NULL;
   i965->base.delete_sampler_state = NULL;
   i965->base.create_rasterizer_state = NULL;
   i965->base.bind_rasterizer_state = NULL;
   i965->base.delete_rasterizer_state = NULL;
   i965->base.create_depth_stencil_alpha_state = NULL;
   i965->base.bind_depth_stencil_alpha_state = NULL;
   i965->base.delete_depth_stencil_alpha_state = NULL;
   i965->base.create_fs_state = NULL;
   i965->base.bind_fs_state = NULL;
   i965->base.delete_fs_state = NULL;
   i965->base.create_vs_state = NULL;
   i965->base.bind_vs_state = NULL;
   i965->base.delete_vs_state = NULL;
   i965->base.create_gs_state = NULL;
   i965->base.bind_gs_state = NULL;
   i965->base.delete_gs_state = NULL;
   i965->base.create_vertex_elements_state = NULL;
   i965->base.bind_vertex_elements_state = NULL;
   i965->base.delete_vertex_elements_state = NULL;

   i965->base.set_blend_color = NULL;
   i965->base.set_stencil_ref = NULL;
   i965->base.set_sample_mask = NULL;
   i965->base.set_clip_state = NULL;
   i965->base.set_constant_buffer = NULL;
   i965->base.set_framebuffer_state = NULL;
   i965->base.set_polygon_stipple = NULL;
   i965->base.set_scissor_state = NULL;
   i965->base.set_viewport_state = NULL;
   i965->base.set_fragment_sampler_views = NULL;
   i965->base.set_vertex_sampler_views = NULL;
   i965->base.set_geometry_sampler_views = NULL;
   i965->base.set_compute_sampler_views = NULL;
   i965->base.set_shader_resources = NULL;
   i965->base.set_vertex_buffers = NULL;
   i965->base.set_index_buffer = NULL;

   i965->base.create_stream_output_target = NULL;
   i965->base.stream_output_target_destroy = NULL;
   i965->base.set_stream_output_targets = NULL;

   i965->base.create_sampler_view = NULL;
   i965->base.sampler_view_destroy = NULL;

   i965->base.create_surface = NULL;
   i965->base.surface_destroy = NULL;

   i965->base.create_compute_state = NULL;
   i965->base.bind_compute_state = NULL;
   i965->base.delete_compute_state = NULL;
   i965->base.set_compute_resources = NULL;
   i965->base.set_global_binding = NULL;
}
