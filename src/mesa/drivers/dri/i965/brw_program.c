/*
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 develop this 3D driver.
 
 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:
 
 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 
 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keith@tungstengraphics.com>
  */
  
#include "main/imports.h"
#include "main/enums.h"
#include "main/shaderobj.h"
#include "program/prog_parameter.h"
#include "program/program.h"
#include "program/programopt.h"
#include "tnl/tnl.h"
#include "glsl/ralloc.h"

#include "brw_context.h"
#include "brw_wm.h"

static void brwBindProgram( struct gl_context *ctx,
			    GLenum target, 
			    struct gl_program *prog )
{
   struct brw_context *brw = brw_context(ctx);

   switch (target) {
   case GL_VERTEX_PROGRAM_ARB: 
      brw->state.dirty.brw |= BRW_NEW_VERTEX_PROGRAM;
      break;
   case GL_FRAGMENT_PROGRAM_ARB:
      brw->state.dirty.brw |= BRW_NEW_FRAGMENT_PROGRAM;
      break;
   }
}

static struct gl_program *brwNewProgram( struct gl_context *ctx,
				      GLenum target, 
				      GLuint id )
{
   struct brw_context *brw = brw_context(ctx);

   switch (target) {
   case GL_VERTEX_PROGRAM_ARB: {
      struct brw_vertex_program *prog = CALLOC_STRUCT(brw_vertex_program);
      if (prog) {
	 prog->id = brw->program_id++;

	 return _mesa_init_vertex_program( ctx, &prog->program,
					     target, id );
      }
      else
	 return NULL;
   }

   case GL_FRAGMENT_PROGRAM_ARB: {
      struct brw_fragment_program *prog = CALLOC_STRUCT(brw_fragment_program);
      if (prog) {
	 prog->id = brw->program_id++;

	 return _mesa_init_fragment_program( ctx, &prog->program,
					     target, id );
      }
      else
	 return NULL;
   }

   default:
      return _mesa_new_program(ctx, target, id);
   }
}

static void brwDeleteProgram( struct gl_context *ctx,
			      struct gl_program *prog )
{
   _mesa_delete_program( ctx, prog );
}


static GLboolean
brwIsProgramNative(struct gl_context *ctx,
		   GLenum target,
		   struct gl_program *prog)
{
   return true;
}

static GLboolean
brwProgramStringNotify(struct gl_context *ctx,
		       GLenum target,
		       struct gl_program *prog)
{
   struct brw_context *brw = brw_context(ctx);

   if (target == GL_FRAGMENT_PROGRAM_ARB) {
      struct gl_fragment_program *fprog = (struct gl_fragment_program *) prog;
      struct brw_fragment_program *newFP = brw_fragment_program(fprog);
      const struct brw_fragment_program *curFP =
         brw_fragment_program_const(brw->fragment_program);

      if (newFP == curFP)
	 brw->state.dirty.brw |= BRW_NEW_FRAGMENT_PROGRAM;
      newFP->id = brw->program_id++;      
   }
   else if (target == GL_VERTEX_PROGRAM_ARB) {
      struct gl_vertex_program *vprog = (struct gl_vertex_program *) prog;
      struct brw_vertex_program *newVP = brw_vertex_program(vprog);
      const struct brw_vertex_program *curVP =
         brw_vertex_program_const(brw->vertex_program);

      if (newVP == curVP)
	 brw->state.dirty.brw |= BRW_NEW_VERTEX_PROGRAM;
      if (newVP->program.IsPositionInvariant) {
	 _mesa_insert_mvp_code(ctx, &newVP->program);
      }
      newVP->id = brw->program_id++;      

      /* Also tell tnl about it:
       */
      _tnl_program_string(ctx, target, prog);
   }

   return true;
}

/* Per-thread scratch space is a power-of-two multiple of 1KB. */
int
brw_get_scratch_size(int size)
{
   int i;

   for (i = 1024; i < size; i *= 2)
      ;

   return i;
}

void
brw_get_scratch_bo(struct intel_context *intel,
		   drm_intel_bo **scratch_bo, int size)
{
   drm_intel_bo *old_bo = *scratch_bo;

   if (old_bo && old_bo->size < size) {
      drm_intel_bo_unreference(old_bo);
      old_bo = NULL;
   }

   if (!old_bo) {
      *scratch_bo = drm_intel_bo_alloc(intel->bufmgr, "scratch bo", size, 4096);
   }
}

void brwInitFragProgFuncs( struct dd_function_table *functions )
{
   assert(functions->ProgramStringNotify == _tnl_program_string); 

   functions->BindProgram = brwBindProgram;
   functions->NewProgram = brwNewProgram;
   functions->DeleteProgram = brwDeleteProgram;
   functions->IsProgramNative = brwIsProgramNative;
   functions->ProgramStringNotify = brwProgramStringNotify;

   functions->NewShader = brw_new_shader;
   functions->NewShaderProgram = brw_new_shader_program;
   functions->LinkShader = brw_link_shader;
}

void
brw_init_shader_time(struct brw_context *brw)
{
   struct intel_context *intel = &brw->intel;

   const int max_entries = 4096;
   brw->shader_time.bo = drm_intel_bo_alloc(intel->bufmgr, "shader time",
                                            max_entries * 4, 4096);
   brw->shader_time.programs = rzalloc_array(brw, struct gl_shader_program *,
                                             max_entries);
   brw->shader_time.types = rzalloc_array(brw, enum shader_time_shader_type,
                                          max_entries);
   brw->shader_time.cumulative = rzalloc_array(brw, uint64_t,
                                               max_entries);
   brw->shader_time.max_entries = max_entries;
}

static int
compare_time(const void *a, const void *b)
{
   uint64_t * const *a_val = a;
   uint64_t * const *b_val = b;

   /* We don't just subtract because we're turning the value to an int. */
   if (**a_val < **b_val)
      return -1;
   else if (**a_val == **b_val)
      return 0;
   else
      return 1;
}

static void
brw_report_shader_time(struct brw_context *brw)
{
   if (!brw->shader_time.bo || !brw->shader_time.num_entries)
      return;

   uint64_t *sorted[brw->shader_time.num_entries];
   double total = 0;
   for (int i = 0; i < brw->shader_time.num_entries; i++) {
      sorted[i] = &brw->shader_time.cumulative[i];
      total += brw->shader_time.cumulative[i];
   }

   if (total == 0) {
      printf("No shader time collected yet\n");
      return;
   }

   qsort(sorted, brw->shader_time.num_entries, sizeof(sorted[0]), compare_time);

   printf("\n");
   printf("type   ID      cycles spent                   %% of total\n");
   for (int s = 0; s < brw->shader_time.num_entries; s++) {
      /* Work back from the sorted pointers times to a time to print. */
      int i = sorted[s] - brw->shader_time.cumulative;

      int shader_num = -1;
      if (brw->shader_time.programs[i]) {
         shader_num = brw->shader_time.programs[i]->Name;
      }

      switch (brw->shader_time.types[i]) {
      case ST_VS:
         printf("vs   %4d: ", shader_num);
         break;
      case ST_FS8:
         printf("fs8  %4d: ", shader_num);
         break;
      case ST_FS16:
         printf("fs16 %4d: ", shader_num);
         break;
      default:
         printf("other:     ");
         break;
      }

      printf("%16lld (%7.2f Gcycles)      %4.1f%%\n",
             (long long)brw->shader_time.cumulative[i],
             (double)brw->shader_time.cumulative[i] / 1000000000.0,
             (double)brw->shader_time.cumulative[i] / total * 100.0);
   }
}

static void
brw_collect_shader_time(struct brw_context *brw)
{
   if (!brw->shader_time.bo)
      return;

   /* This probably stalls on the last rendering.  We could fix that by
    * delaying reading the reports, but it doesn't look like it's a big
    * overhead compared to the cost of tracking the time in the first place.
    */
   drm_intel_bo_map(brw->shader_time.bo, true);

   uint32_t *times = brw->shader_time.bo->virtual;

   for (int i = 0; i < brw->shader_time.num_entries; i++) {
      brw->shader_time.cumulative[i] += times[i];
   }

   /* Zero the BO out to clear it out for our next collection.
    */
   memset(times, 0, brw->shader_time.bo->size);
   drm_intel_bo_unmap(brw->shader_time.bo);
}

void
brw_collect_and_report_shader_time(struct brw_context *brw)
{
   brw_collect_shader_time(brw);

   if (brw->shader_time.report_time == 0 ||
       get_time() - brw->shader_time.report_time >= 1.0) {
      brw_report_shader_time(brw);
      brw->shader_time.report_time = get_time();
   }
}

void
brw_destroy_shader_time(struct brw_context *brw)
{
   drm_intel_bo_unreference(brw->shader_time.bo);
   brw->shader_time.bo = NULL;
}
