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

#ifndef I965_TRANSLATE_H
#define I965_TRANSLATE_H

#include "intel_winsys.h"

#include "pipe/p_compiler.h"
#include "pipe/p_format.h"

int
i965_translate_winsys_tiling(enum intel_tiling_mode tiling);

int
i965_translate_color_format(enum pipe_format format);

int
i965_translate_depth_format(enum pipe_format format);

int
i965_translate_render_format(enum pipe_format format);

int
i965_translate_texture_format(enum pipe_format format);

int
i965_translate_vertex_format(enum pipe_format format);

int
i965_translate_pipe_prim(unsigned prim);

int
i965_translate_pipe_logicop(unsigned logicop);

int
i965_translate_pipe_blend(unsigned blend);

int
i965_translate_pipe_blendfactor(unsigned blendfactor);

int
i965_translate_pipe_stencil_op(unsigned stencil_op);

int
i965_translate_texture(enum pipe_texture_target target);

int
i965_translate_tex_mipfilter(unsigned filter);

int
i965_translate_tex_filter(unsigned filter);

int
i965_translate_tex_wrap(unsigned wrap, boolean clamp_to_edge);

int
i965_translate_dsa_func(unsigned func);

int
i965_translate_shadow_func(unsigned func);

int
i965_translate_index_size(int size);

#endif /* I965_TRANSLATE_H */
