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

#include "intel_chipset.h"

#include "intel_winsys.h"
#include "i965_common.h"
#include "i965_format.h"
#include "i965_context.h"
#include "i965_resource.h"
#include "i965_screen.h"

#ifdef DEBUG
int i965_debug;
#endif

static const struct debug_named_value i965_debug_flags[] = {
   { "nohw",      I965_DEBUG_NOHW,     "Do not send commands to HW" },
   { "nocache",   I965_DEBUG_NOCACHE,  "Always invalidate HW caches" },
   { "3d",        I965_DEBUG_3D,       "Dump 3D commands and states" },
   { "vs",        I965_DEBUG_VS,       "Dump vertex shaders" },
   { "fs",        I965_DEBUG_FS,       "Dump fragment shaders" },
   DEBUG_NAMED_VALUE_END
};

struct pipe_screen *
i965_screen_create(struct intel_winsys *ws)
{
   struct i965_screen *is;
   const struct intel_info *info;

#ifdef DEBUG
   i965_debug = debug_get_flags_option("I965_DEBUG", i965_debug_flags, 0);
#endif

   is = CALLOC_STRUCT(i965_screen);
   if (!is)
      return NULL;

   is->winsys = ws;

   info = is->winsys->get_info(is->winsys);

   /* require fences */
   if (!info->num_fences_avail) {
      FREE(is);
      return NULL;
   }

   is->winsys->enable_fenced_relocs(is->winsys);

   is->devid = info->devid;
   if (IS_GEN6(info->devid)) {
      is->gen = 6;
   }
   else {
      /* only GEN6 is supported */
      FREE(is);
      return NULL;
   }

   is->base.destroy = NULL;
   is->base.get_name = NULL;
   is->base.get_vendor = NULL;
   is->base.get_param = NULL;
   is->base.get_paramf = NULL;
   is->base.get_shader_param = NULL;
   is->base.get_video_param = NULL;
   is->base.get_compute_param = NULL;

   is->base.flush_frontbuffer = NULL;
   is->base.fence_reference = NULL;
   is->base.fence_signalled = NULL;
   is->base.fence_finish = NULL;

   i965_init_format_functions(is);
   i965_init_context_functions(is);
   i965_init_resource_functions(is);

   return &is->base;
}
