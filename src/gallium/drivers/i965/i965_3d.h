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

#ifndef I965_3D_H
#define I965_3D_H

#include "i965_common.h"

struct i965_context;
struct i965_cp;
struct i965_query;
struct i965_3d;

struct i965_3d *
i965_3d_create(struct i965_cp *cp, int gen);

void
i965_3d_destroy(struct i965_3d *hw3d);

void
i965_3d_new_cp_batch(struct i965_3d *hw3d);

void
i965_3d_pre_cp_flush(struct i965_3d *hw3d);

void
i965_3d_post_cp_flush(struct i965_3d *hw3d);

void
i965_3d_begin_query(struct i965_context *i965, struct i965_query *q);

void
i965_3d_end_query(struct i965_context *i965, struct i965_query *q);

void
i965_3d_update_query_result(struct i965_context *i965, struct i965_query *q);

void
i965_init_3d_functions(struct i965_context *i965);

#endif /* I965_3D_H */