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

#ifndef I965_COMMON_H
#define I965_COMMON_H

#include "pipe/p_compiler.h"
#include "pipe/p_defines.h"
#include "pipe/p_format.h"

#include "util/u_debug.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_double_list.h"

/**
 * \see brw_context.h
 */
#define I965_MAX_DRAW_BUFFERS    8
#define I965_MAX_CONST_BUFFERS   1
#define I965_MAX_SAMPLER_VIEWS   16
#define I965_MAX_SAMPLERS        16
#define I965_MAX_SO_BINDINGS     64
#define I965_MAX_SO_BUFFERS      4

#define I965_MAX_VS_SURFACES        (I965_MAX_CONST_BUFFERS + I965_MAX_SAMPLER_VIEWS)
#define I965_VS_CONST_SURFACE(i)    (i)
#define I965_VS_TEXTURE_SURFACE(i)  (I965_MAX_CONST_BUFFERS  + i)

#define I965_MAX_GS_SURFACES        (I965_MAX_SO_BINDINGS)
#define I965_GS_SO_SURFACE(i)       (i)

#define I965_MAX_WM_SURFACES        (I965_MAX_DRAW_BUFFERS + I965_MAX_CONST_BUFFERS + I965_MAX_SAMPLER_VIEWS)
#define I965_WM_DRAW_SURFACE(i)     (i)
#define I965_WM_CONST_SURFACE(i)    (I965_MAX_DRAW_BUFFERS + i)
#define I965_WM_TEXTURE_SURFACE(i)  (I965_MAX_DRAW_BUFFERS + I965_MAX_CONST_BUFFERS  + i)

enum i965_debug {
   I965_DEBUG_NOHW      = 0x01,
   I965_DEBUG_3D        = 0x02,
   I965_DEBUG_VS        = 0x04,
   I965_DEBUG_FS        = 0x08,
   I965_DEBUG_NOCACHE   = 0x10,
};

#ifdef DEBUG
extern int i965_debug;
#else
#define i965_debug 0
#endif

#endif /* I965_COMMON_H */