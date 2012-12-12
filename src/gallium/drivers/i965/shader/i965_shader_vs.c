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

#include "tgsi/tgsi_dump.h"
#include "toy_compiler.h"
#include "toy_tgsi.h"
#include "toy_legalize.h"
#include "toy_optimize.h"
#include "toy_helpers.h"
#include "i965_context.h"
#include "i965_shader.h"

struct vs_compile_context {
   struct i965_shader *shader;
   const struct i965_shader_variant *variant;

   struct toy_compiler tc;
   struct toy_tgsi tgsi;

   int output_map[PIPE_MAX_SHADER_OUTPUTS];

   int num_grf_per_vrf;
   int first_const_grf;
   int first_vue_grf;
   int first_free_grf;
   int last_free_grf;

   int first_free_mrf;
   int last_free_mrf;
};

static void
vs_lower_opcode_tgsi_in(struct vs_compile_context *vcc,
                        struct toy_dst dst, int dim, int idx)
{
   struct toy_compiler *tc = &vcc->tc;
   int slot;

   assert(!dim);

   slot = toy_tgsi_find_input(&vcc->tgsi, idx);
   if (slot >= 0) {
      const int grf = vcc->first_vue_grf +
         vcc->tgsi.inputs[slot].semantic_index;
      const struct toy_src src = tsrc(TOY_FILE_GRF, grf, 0);

      tc_MOV(tc, dst, src);
   }
   else {
      /* undeclared input */
      tc_MOV(tc, dst, tsrc_imm_f(0.0f));
   }
}

static void
vs_lower_opcode_tgsi_const(struct vs_compile_context *vcc,
                           struct toy_dst dst, int dim, struct toy_src idx)
{
   const struct toy_dst header =
      tdst_ud(tdst(TOY_FILE_MRF, vcc->first_free_mrf, 0));
   const struct toy_dst block_offsets =
      tdst_ud(tdst(TOY_FILE_MRF, vcc->first_free_mrf + 1, 0));
   const struct toy_src r0 = tsrc_ud(tsrc(TOY_FILE_GRF, 0, 0));
   struct toy_compiler *tc = &vcc->tc;
   unsigned msg_type, msg_ctrl, msg_len;
   struct toy_inst *inst;
   struct toy_src desc;

   /* set message header */
   inst = tc_MOV(tc, header, r0);
   inst->mask_ctrl = BRW_MASK_DISABLE;

   /* set block offsets */
   tc_MOV(tc, block_offsets, idx);

   msg_type = GEN6_DATAPORT_READ_MESSAGE_OWORD_DUAL_BLOCK_READ;
   msg_ctrl = BRW_DATAPORT_OWORD_DUAL_BLOCK_1OWORD;;
   msg_len = 2;

   desc = tsrc_imm_mdesc_data_port(tc, FALSE, msg_len, 1, TRUE, FALSE,
         msg_type, msg_ctrl, I965_VS_CONST_SURFACE(dim));

   tc_SEND(tc, dst, tsrc_from(header), desc,
         GEN6_SFID_DATAPORT_SAMPLER_CACHE);
}

static void
vs_lower_opcode_tgsi_imm(struct vs_compile_context *vcc,
                         struct toy_dst dst, int idx, boolean is_immx)
{
   const uint32_t *imm;
   int ch;

   imm = toy_tgsi_get_imm(&vcc->tgsi, idx, is_immx, NULL);

   for (ch = 0; ch < 4; ch++) {
      /* raw moves */
      tc_MOV(&vcc->tc,
            tdst_writemask(tdst_ud(dst), 1 << ch),
            tsrc_imm_ud(imm[ch]));
   }
}


static void
vs_lower_opcode_tgsi_sv(struct vs_compile_context *vcc,
                        struct toy_dst dst, int dim, int idx)
{
   struct toy_compiler *tc = &vcc->tc;
   const struct toy_tgsi *tgsi = &vcc->tgsi;
   int slot;

   assert(!dim);

   slot = toy_tgsi_find_input(tgsi, idx);
   if (slot < 0)
      return;

   switch (tgsi->inputs[slot].semantic_name) {
   case TGSI_SEMANTIC_PRIMID:
   case TGSI_SEMANTIC_INSTANCEID:
   case TGSI_SEMANTIC_VERTEXID:
   default:
      assert(!"unhandled system value");
      tc_MOV(tc, dst, tsrc_imm_d(0));
      break;
   }
}

static void
vs_lower_opcode_tgsi_direct(struct vs_compile_context *vcc,
                            struct toy_inst *inst)
{
   struct toy_compiler *tc = &vcc->tc;
   int dim, idx;

   assert(inst->src[0].file == TOY_FILE_IMM);
   dim = inst->src[0].val32;

   assert(inst->src[1].file == TOY_FILE_IMM);
   idx = inst->src[1].val32;

   switch (inst->opcode) {
   case TOY_OPCODE_TGSI_IN:
      vs_lower_opcode_tgsi_in(vcc, inst->dst, dim, idx);
      break;
   case TOY_OPCODE_TGSI_CONST:
      vs_lower_opcode_tgsi_const(vcc, inst->dst, dim, inst->src[1]);
      break;
   case TOY_OPCODE_TGSI_SV:
      vs_lower_opcode_tgsi_sv(vcc, inst->dst, dim, idx);
      break;
   case TOY_OPCODE_TGSI_IMM:
      assert(!dim);
      vs_lower_opcode_tgsi_imm(vcc, inst->dst, idx, FALSE);
      break;
   case TOY_OPCODE_TGSI_IMMX:
      assert(!dim);
      vs_lower_opcode_tgsi_imm(vcc, inst->dst, idx, TRUE);
      break;
   default:
      assert(!"unhandled TGSI fetch");
      break;
   }

   tc_discard_inst(tc, inst);
}

static void
vs_lower_opcode_tgsi_indirect(struct vs_compile_context *vcc,
                              struct toy_inst *inst)
{
   struct toy_compiler *tc = &vcc->tc;
   enum tgsi_file_type file;
   unsigned dim;
   struct toy_src idx;
   int offset = 0;

   assert(inst->src[0].file == TOY_FILE_IMM);
   file = inst->src[0].val32;
   assert(inst->src[1].file == TOY_FILE_IMM);
   dim = inst->src[1].val32;

   idx = inst->src[2];
   /* XXX ahh, see init_tgsi_reg() */
   if (idx.indirect) {
      offset = idx.indirect_subreg;
      if (offset > 31)
         offset -= 64;
      idx.indirect = 0;
      idx.indirect_subreg = 0;
   }

   switch (inst->opcode) {
   case TOY_OPCODE_TGSI_INDIRECT_FETCH:
      if (file == TGSI_FILE_CONSTANT) {
         if (offset) {
            struct toy_dst tmp = tc_alloc_tmp(tc);

            tc_ADD(tc, tmp, idx, tsrc_imm_d(offset));
            idx = tsrc_from(tmp);
         }

         vs_lower_opcode_tgsi_const(vcc, inst->dst, dim, idx);
         break;
      }
      /* fall through */
   case TOY_OPCODE_TGSI_INDIRECT_STORE:
   default:
      assert(!"unhandled TGSI indirection");
      break;
   }

   tc_discard_inst(tc, inst);
}

/**
 * Emit instructions to move sampling parameters to the message registers.
 */
static int
vs_add_sampler_params(struct toy_compiler *tc, int msg_type, int base_mrf,
                      struct toy_src coords, int num_coords,
                      struct toy_src bias_or_lod, struct toy_src ref_or_si,
                      struct toy_src ddx, struct toy_src ddy, int num_derivs)
{
   const unsigned coords_writemask = (1 << num_coords) - 1;
   struct toy_dst m[3];
   int num_params, i;

   assert(num_coords <= 4);
   assert(num_derivs <= 3 && num_derivs <= num_coords);

   for (i = 0; i < Elements(m); i++)
      m[i] = tdst(TOY_FILE_MRF, base_mrf + i, 0);

   switch (msg_type) {
   case GEN5_SAMPLER_MESSAGE_SAMPLE_LOD:
      tc_MOV(tc, tdst_writemask(m[0], coords_writemask), coords);
      tc_MOV(tc, tdst_writemask(m[1], TOY_WRITEMASK_X), bias_or_lod);
      num_params = 5;
      break;
   case GEN5_SAMPLER_MESSAGE_SAMPLE_DERIVS:
      tc_MOV(tc, tdst_writemask(m[0], coords_writemask), coords);
      tc_MOV(tc, tdst_writemask(m[1], TOY_WRITEMASK_XZ),
            tsrc_swizzle(ddx, 0, 0, 1, 1));
      tc_MOV(tc, tdst_writemask(m[1], TOY_WRITEMASK_YW),
            tsrc_swizzle(ddy, 0, 0, 1, 1));
      if (num_derivs > 2) {
         tc_MOV(tc, tdst_writemask(m[2], TOY_WRITEMASK_X),
               tsrc_swizzle1(ddx, 2));
         tc_MOV(tc, tdst_writemask(m[2], TOY_WRITEMASK_Y),
               tsrc_swizzle1(ddy, 2));
      }
      num_params = 4 + num_derivs * 2;
      break;
   case GEN5_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE:
      tc_MOV(tc, tdst_writemask(m[0], coords_writemask), coords);
      tc_MOV(tc, tdst_writemask(m[1], TOY_WRITEMASK_X), ref_or_si);
      tc_MOV(tc, tdst_writemask(m[1], TOY_WRITEMASK_Y), bias_or_lod);
      num_params = 6;
      break;
   case GEN5_SAMPLER_MESSAGE_SAMPLE_LD:
      assert(num_coords <= 3);
      tc_MOV(tc, tdst_writemask(m[0], coords_writemask), coords);
      tc_MOV(tc, tdst_writemask(m[0], TOY_WRITEMASK_W), bias_or_lod);
      tc_MOV(tc, tdst_writemask(m[1], TOY_WRITEMASK_X), ref_or_si);
      num_params = 5;
      break;
   case GEN5_SAMPLER_MESSAGE_SAMPLE_RESINFO:
      tc_MOV(tc, tdst_writemask(m[0], TOY_WRITEMASK_X), bias_or_lod);
      num_params = 1;
      break;
   default:
      assert(!"unknown sampler opcode");
      num_params = 0;
      break;
   }

   return (num_params + 3) / 4;
}

/**
 * Set up message registers and return the message descriptor for sampling.
 */
static struct toy_src
vs_prepare_tgsi_sampling(struct toy_compiler *tc, const struct toy_inst *inst,
                         int base_mrf, unsigned *ret_sampler_index)
{
   unsigned simd_mode, msg_type, msg_len, sampler_index, binding_table_index;
   struct toy_src coords, ddx, ddy, bias_or_lod, ref_or_si;
   int num_coords, ref_pos, num_derivs;
   int sampler_src;

   simd_mode = BRW_SAMPLER_SIMD_MODE_SIMD4X2;

   coords = inst->src[0];
   ddx = tsrc_null();
   ddy = tsrc_null();
   bias_or_lod = tsrc_null();
   ref_or_si = tsrc_null();
   num_derivs = 0;
   sampler_src = 1;

   num_coords = toy_tgsi_get_texture_coord_dim(inst->tex.target, &ref_pos);

   /* extract the parameters */
   switch (inst->opcode) {
   case TOY_OPCODE_TGSI_TXD:
      msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE_DERIVS;
      ddx = inst->src[1];
      ddy = inst->src[2];
      num_derivs = num_coords;
      sampler_src = 3;
      break;
   case TOY_OPCODE_TGSI_TXL:
      if (ref_pos >= 0) {
         assert(ref_pos < 3);

         msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE;
         ref_or_si = tsrc_swizzle1(coords, ref_pos);
      }
      else {
         msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE_LOD;
      }

      bias_or_lod = tsrc_swizzle1(coords, TOY_SWIZZLE_W);
      break;
   case TOY_OPCODE_TGSI_TXF:
      msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE_LD;

      switch (inst->tex.target) {
      case TGSI_TEXTURE_2D_MSAA:
      case TGSI_TEXTURE_2D_ARRAY_MSAA:
         assert(ref_pos >= 0 && ref_pos < 4);
         ref_or_si = tsrc_swizzle1(coords, ref_pos);
         break;
      default:
         bias_or_lod = tsrc_swizzle1(coords, TOY_SWIZZLE_W);
         break;
      }

      /* offset the coordinates */
      if (!tsrc_is_null(inst->tex.offsets[0])) {
         struct toy_dst tmp;

         tmp = tc_alloc_tmp(tc);
         tc_ADD(tc, tmp, coords, inst->tex.offsets[0]);
         coords = tsrc_from(tmp);
      }

      sampler_src = 2;
      break;
   case TOY_OPCODE_TGSI_TXQ:
      msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE_RESINFO;
      num_coords = 0;
      bias_or_lod = tsrc_swizzle1(coords, TOY_SWIZZLE_X);
      break;
   case TOY_OPCODE_TGSI_TXQ_LZ:
      msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE_RESINFO;
      num_coords = 0;
      sampler_src = 0;
      break;
   case TOY_OPCODE_TGSI_TXL2:
      if (ref_pos >= 0) {
         assert(ref_pos < 4);

         msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE;
         ref_or_si = tsrc_swizzle1(coords, ref_pos);
      }
      else {
         msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE_LOD;
      }

      bias_or_lod = tsrc_swizzle1(inst->src[1], TOY_SWIZZLE_X);
      sampler_src = 2;
      break;
   default:
      assert(!"unhandled sampling opcode");
      return tsrc_null();
      break;
   }

   assert(inst->src[sampler_src].file == TOY_FILE_IMM);
   sampler_index = inst->src[sampler_src].val32;
   binding_table_index = I965_VS_TEXTURE_SURFACE(sampler_index);

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 18:
    *
    *     "Note that the (cube map) coordinates delivered to the sampling
    *      engine must already have been divided by the component with the
    *      largest absolute value."
    */
   switch (inst->tex.target) {
   case TGSI_TEXTURE_CUBE:
   case TGSI_TEXTURE_SHADOWCUBE:
   case TGSI_TEXTURE_CUBE_ARRAY:
   case TGSI_TEXTURE_SHADOWCUBE_ARRAY:
      /* TXQ does not need coordinates */
      if (num_coords >= 3) {
         struct toy_dst tmp, max;
         struct toy_src abs_coords[3];
         int i;

         tmp = tc_alloc_tmp(tc);
         max = tdst_writemask(tmp, TOY_WRITEMASK_W);

         for (i = 0; i < 3; i++)
            abs_coords[i] = tsrc_absolute(tsrc_swizzle1(coords, i));

         tc_SEL(tc, max, abs_coords[0], abs_coords[0], BRW_CONDITIONAL_GE);
         tc_SEL(tc, max, tsrc_from(max), abs_coords[0], BRW_CONDITIONAL_GE);
         tc_INV(tc, max, tsrc_from(max));

         for (i = 0; i < 3; i++)
            tc_MUL(tc, tdst_writemask(tmp, 1 << i), coords, tsrc_from(max));

         coords = tsrc_from(tmp);
      }
      break;
   }

   /* set up sampler parameters */
   msg_len = vs_add_sampler_params(tc, msg_type, base_mrf,
         coords, num_coords, bias_or_lod, ref_or_si, ddx, ddy, num_derivs);

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 136:
    *
    *     "The maximum message length allowed to the sampler is 11. This would
    *      disallow sample_d, sample_b_c, and sample_l_c with a SIMD Mode of
    *      SIMD16."
    */
   assert(msg_len <= 11);

   if (ret_sampler_index)
      *ret_sampler_index = sampler_index;

   return tsrc_imm_mdesc_sampler(tc, msg_len, 1,
         FALSE, simd_mode, msg_type, sampler_index, binding_table_index);
}

static void
vs_lower_opcode_tgsi_sampling(struct vs_compile_context *vcc,
                              struct toy_inst *inst)
{
   struct toy_compiler *tc = &vcc->tc;
   struct toy_src desc;
   struct toy_dst dst, tmp;
   unsigned sampler_index;
   int swizzles[4], i;
   unsigned swizzle_zero_mask, swizzle_one_mask, swizzle_normal_mask;

   desc = vs_prepare_tgsi_sampling(tc, inst,
         vcc->first_free_mrf, &sampler_index);

   toy_compiler_lower_to_send(tc, inst, FALSE, BRW_SFID_SAMPLER);
   inst->src[0] = tsrc(TOY_FILE_MRF, vcc->first_free_mrf, 0);
   inst->src[1] = desc;

   /* write to a temp first */
   tmp = tc_alloc_tmp(tc);
   dst = inst->dst;
   inst->dst = tmp;

   tc_move_inst(tc, inst);

   assert(sampler_index < vcc->variant->num_sampler_views);
   swizzles[0] = vcc->variant->sampler_view_swizzles[sampler_index].r;
   swizzles[1] = vcc->variant->sampler_view_swizzles[sampler_index].g;
   swizzles[2] = vcc->variant->sampler_view_swizzles[sampler_index].b;
   swizzles[3] = vcc->variant->sampler_view_swizzles[sampler_index].a;

   swizzle_zero_mask = 0;
   swizzle_one_mask = 0;
   swizzle_normal_mask = 0;
   for (i = 0; i < 4; i++) {
      switch (swizzles[i]) {
      case PIPE_SWIZZLE_ZERO:
         swizzle_zero_mask |= 1 << i;
         swizzles[i] = i;
         break;
      case PIPE_SWIZZLE_ONE:
         swizzle_one_mask |= 1 << i;
         swizzles[i] = i;
         break;
      default:
         swizzle_normal_mask |= 1 << i;
         break;
      }
   }

   /* swizzle the results */
   if (swizzle_normal_mask) {
      tc_MOV(tc, tdst_writemask(dst, swizzle_normal_mask),
            tsrc_swizzle(tsrc_from(tmp), swizzles[0],
               swizzles[1], swizzles[2], swizzles[3]));
   }
   if (swizzle_zero_mask)
      tc_MOV(tc, tdst_writemask(dst, swizzle_zero_mask), tsrc_imm_f(0.0f));
   if (swizzle_one_mask)
      tc_MOV(tc, tdst_writemask(dst, swizzle_one_mask), tsrc_imm_f(1.0f));
}

static void
vs_lower_opcode_urb_write(struct toy_compiler *tc, struct toy_inst *inst)
{
   /* vs_write_vue() has set up the message registers */
   toy_compiler_lower_to_send(tc, inst, FALSE, BRW_SFID_URB);
}

static void
vs_lower_virtual_opcodes(struct vs_compile_context *vcc)
{
   struct toy_compiler *tc = &vcc->tc;
   struct toy_inst *inst;

   tc_head(tc);
   while ((inst = tc_next(tc)) != NULL) {
      switch (inst->opcode) {
      case TOY_OPCODE_TGSI_IN:
      case TOY_OPCODE_TGSI_CONST:
      case TOY_OPCODE_TGSI_SV:
      case TOY_OPCODE_TGSI_IMM:
      case TOY_OPCODE_TGSI_IMMX:
         vs_lower_opcode_tgsi_direct(vcc, inst);
         break;
      case TOY_OPCODE_TGSI_INDIRECT_FETCH:
      case TOY_OPCODE_TGSI_INDIRECT_STORE:
         vs_lower_opcode_tgsi_indirect(vcc, inst);
         break;
      case TOY_OPCODE_TGSI_TEX:
      case TOY_OPCODE_TGSI_TXB:
      case TOY_OPCODE_TGSI_TXD:
      case TOY_OPCODE_TGSI_TXL:
      case TOY_OPCODE_TGSI_TXP:
      case TOY_OPCODE_TGSI_TXF:
      case TOY_OPCODE_TGSI_TXQ:
      case TOY_OPCODE_TGSI_TXQ_LZ:
      case TOY_OPCODE_TGSI_TEX2:
      case TOY_OPCODE_TGSI_TXB2:
      case TOY_OPCODE_TGSI_TXL2:
      case TOY_OPCODE_TGSI_SAMPLE:
      case TOY_OPCODE_TGSI_SAMPLE_I:
      case TOY_OPCODE_TGSI_SAMPLE_I_MS:
      case TOY_OPCODE_TGSI_SAMPLE_B:
      case TOY_OPCODE_TGSI_SAMPLE_C:
      case TOY_OPCODE_TGSI_SAMPLE_C_LZ:
      case TOY_OPCODE_TGSI_SAMPLE_D:
      case TOY_OPCODE_TGSI_SAMPLE_L:
      case TOY_OPCODE_TGSI_GATHER4:
      case TOY_OPCODE_TGSI_SVIEWINFO:
      case TOY_OPCODE_TGSI_SAMPLE_POS:
      case TOY_OPCODE_TGSI_SAMPLE_INFO:
         vs_lower_opcode_tgsi_sampling(vcc, inst);
         break;
      case TOY_OPCODE_INV:
      case TOY_OPCODE_LOG:
      case TOY_OPCODE_EXP:
      case TOY_OPCODE_SQRT:
      case TOY_OPCODE_RSQ:
      case TOY_OPCODE_SIN:
      case TOY_OPCODE_COS:
      case TOY_OPCODE_FDIV:
      case TOY_OPCODE_POW:
      case TOY_OPCODE_INT_DIV_QUOTIENT:
      case TOY_OPCODE_INT_DIV_REMAINDER:
         toy_compiler_lower_math(tc, inst);
         break;
      case TOY_OPCODE_URB_WRITE:
         vs_lower_opcode_urb_write(tc, inst);
         break;
      default:
         if (inst->opcode > 127)
            assert(!"unhandled virtual opcode");
         break;
      }
   }
}

/**
 * Compile the shader.
 */
static boolean
vs_compile(struct vs_compile_context *vcc)
{
   struct toy_compiler *tc = &vcc->tc;
   struct i965_shader *sh = vcc->shader;

   /* lower all virtual opcodes */
   vs_lower_virtual_opcodes(vcc);

   if (!toy_compiler_legalize_for_ra(tc))
      return FALSE;

   toy_compiler_optimize(tc);

   toy_compiler_allocate_registers(tc,
         vcc->first_free_grf,
         vcc->last_free_grf,
         vcc->num_grf_per_vrf);

   if (!toy_compiler_legalize_for_asm(tc))
      return FALSE;

   if (i965_debug & I965_DEBUG_VS) {
      debug_printf("legalized instructions:\n");
      toy_compiler_dump(tc);
      debug_printf("\n");
   }

   sh->kernel = toy_compiler_assemble(tc, &sh->kernel_size);
   if (!sh->kernel)
      return FALSE;

   if (i965_debug & I965_DEBUG_VS) {
      debug_printf("disassembly:\n");
      toy_compiler_disassemble(tc, sh->kernel, sh->kernel_size);
      debug_printf("\n");
   }

   return TRUE;
}

/**
 * Collect the toy registers to be written to the VUE.
 */
static int
vs_collect_outputs(struct vs_compile_context *vcc, struct toy_src *outs)
{
   const struct toy_tgsi *tgsi = &vcc->tgsi;
   int i;

   for (i = 0; i < vcc->shader->out.count; i++) {
      const int slot = vcc->output_map[i];
      const int vrf = (slot >= 0) ? toy_tgsi_get_vrf(tgsi,
            TGSI_FILE_OUTPUT, 0, tgsi->outputs[slot].index) : -1;
      struct toy_src src;

      if (vrf >= 0) {
         struct toy_dst dst;

         dst = tdst(TOY_FILE_VRF, vrf, 0);
         src = tsrc_from(dst);

         if (i == 0) {
            /* PSIZE is at channel W */
            tc_MOV(&vcc->tc, tdst_writemask(dst, TOY_WRITEMASK_W),
                  tsrc_swizzle1(src, TOY_SWIZZLE_X));

            /* the other channels are for the header */
            dst = tdst_d(dst);
            tc_MOV(&vcc->tc, tdst_writemask(dst, TOY_WRITEMASK_XYZ),
                  tsrc_imm_d(0));
         }
         else {
            /* initialize unused channels to 0.0f */
            if (tgsi->outputs[slot].undefined_mask) {
               dst = tdst_writemask(dst, tgsi->outputs[slot].undefined_mask);
               tc_MOV(&vcc->tc, dst, tsrc_imm_f(0.0f));
            }
         }
      }
      else {
         src = (i == 0) ? tsrc_imm_d(0) : tsrc_imm_f(0.0f);
      }

      outs[i] = src;
   }

   return i;
}

/**
 * Emit instructions to write the VUE.
 */
static void
vs_write_vue(struct vs_compile_context *vcc)
{
   struct toy_compiler *tc = &vcc->tc;
   struct toy_src outs[PIPE_MAX_SHADER_OUTPUTS];
   struct toy_dst header;
   struct toy_src r0;
   struct toy_inst *inst;
   int sent, total;

   header = tdst_ud(tdst(TOY_FILE_MRF, vcc->first_free_mrf, 0));
   r0 = tsrc_ud(tsrc(TOY_FILE_GRF, 0, 0));
   inst = tc_MOV(tc, header, r0);
   inst->mask_ctrl = BRW_MASK_DISABLE;

   total = vs_collect_outputs(vcc, outs);
   sent = 0;
   while (sent < total) {
      struct toy_src desc;
      int mrf = vcc->first_free_mrf + 1;
      int mrf_len, msg_len, i;
      boolean eot;

      /*
       * From the Sandy Bridge PRM, volume 4 part 2, page 26:
       *
       *     "At least 256 bits per vertex (512 bits total, M1 & M2) must be
       *      written.  Writing only 128 bits per vertex (256 bits total, M1
       *      only) results in UNDEFINED operation."
       *
       *     "[DevSNB] Interleave writes must be in multiples of 256 per
       *     vertex."
       */
      mrf_len = total - sent;
      eot = TRUE;
      if (mrf_len > vcc->last_free_mrf - mrf + 1) {
         mrf_len = vcc->last_free_mrf - mrf + 1;
         mrf_len &= ~1;
         eot = FALSE;
      }
      msg_len = align(mrf_len, 2);

      /* do not forget the header */
      msg_len++;

      for (i = 0; i < mrf_len; i++)
         tc_MOV(tc, tdst(TOY_FILE_MRF, mrf++, 0), outs[sent + i]);

      desc = tsrc_imm_mdesc_urb(tc, eot, msg_len,
            BRW_URB_SWIZZLE_INTERLEAVE, sent);

      tc_add2(tc, TOY_OPCODE_URB_WRITE, tdst_null(), tsrc_from(header), desc);

      sent += mrf_len;
   }
}

/**
 * Set up shader inputs for fixed-function units.
 */
static void
vs_setup_shader_in(struct i965_shader *sh, const struct toy_tgsi *tgsi)
{
   int num_attrs, i;

   num_attrs = 0;
   for (i = 0; i < tgsi->num_inputs; i++) {
      assert(tgsi->inputs[i].semantic_name == TGSI_SEMANTIC_GENERIC);
      if (tgsi->inputs[i].semantic_index >= num_attrs)
         num_attrs = tgsi->inputs[i].semantic_index + 1;
   }
   assert(num_attrs <= PIPE_MAX_ATTRIBS);

   /* VF cannot remap VEs.  VE[i] must be used as GENERIC[i]. */
   sh->in.count = num_attrs;
   for (i = 0; i < sh->in.count; i++) {
      sh->in.semantic_names[i] = TGSI_SEMANTIC_GENERIC;
      sh->in.semantic_indices[i] = i;
      sh->in.interp[i] = TGSI_INTERPOLATE_CONSTANT;
      sh->in.centroid[i] = FALSE;
   }

   sh->in.has_pos = FALSE;
   sh->in.has_linear_interp = FALSE;
   sh->in.barycentric_interpolation_mode = 0;
}

/**
 * Set up shader outputs for fixed-function units.
 */
static void
vs_setup_shader_out(struct i965_shader *sh, const struct toy_tgsi *tgsi,
                    int *output_map)
{
   int psize_slot = -1, pos_slot = -1;
   int color_slot[4] = { -1, -1, -1, -1 };
   int num_outs, i;

   /* find out the slots of outputs that need special care */
   for (i = 0; i < tgsi->num_outputs; i++) {
      switch (tgsi->outputs[i].semantic_name) {
      case TGSI_SEMANTIC_PSIZE:
         psize_slot = i;
         break;
      case TGSI_SEMANTIC_POSITION:
         pos_slot = i;
         break;
      case TGSI_SEMANTIC_COLOR:
         if (tgsi->outputs[i].semantic_index)
            color_slot[2] = i;
         else
            color_slot[0] = i;
         break;
      case TGSI_SEMANTIC_BCOLOR:
         if (tgsi->outputs[i].semantic_index)
            color_slot[3] = i;
         else
            color_slot[1] = i;
         break;
      default:
         break;
      }
   }

   /* the first two VUEs are always PSIZE and POSITION */
   num_outs = 2;
   sh->out.semantic_names[0] = TGSI_SEMANTIC_PSIZE;
   sh->out.semantic_indices[0] = 0;
   sh->out.semantic_names[1] = TGSI_SEMANTIC_POSITION;
   sh->out.semantic_indices[1] = 0;

   sh->out.has_pos = TRUE;
   output_map[0] = psize_slot;
   output_map[1] = pos_slot;

   /*
    * make BCOLOR follow COLOR so that we can make use of
    * ATTRIBUTE_SWIZZLE_INPUTATTR_FACING in 3DSTATE_SF
    */
   for (i = 0; i < 4; i++) {
      const int slot = color_slot[i];

      if (slot < 0)
         continue;

      sh->out.semantic_names[num_outs] = tgsi->outputs[slot].semantic_name;
      sh->out.semantic_indices[num_outs] = tgsi->outputs[slot].semantic_index;

      output_map[num_outs++] = slot;
   }

   /* add the rest of the outputs */
   for (i = 0; i < tgsi->num_outputs; i++) {
      switch (tgsi->outputs[i].semantic_name) {
      case TGSI_SEMANTIC_PSIZE:
      case TGSI_SEMANTIC_POSITION:
      case TGSI_SEMANTIC_COLOR:
      case TGSI_SEMANTIC_BCOLOR:
         break;
      default:
         sh->out.semantic_names[num_outs] = tgsi->outputs[i].semantic_name;
         sh->out.semantic_indices[num_outs] = tgsi->outputs[i].semantic_index;
         output_map[num_outs++] = i;
         break;
      }
   }

   sh->out.count = num_outs;
}

/**
 * Translate the TGSI tokens.
 */
static boolean
vs_setup_tgsi(struct toy_compiler *tc, const struct tgsi_token *tokens,
              struct toy_tgsi *tgsi)
{
   if (i965_debug & I965_DEBUG_VS) {
      debug_printf("dumping vertex shader\n");
      debug_printf("\n");

      tgsi_dump(tokens, 0);
      debug_printf("\n");
   }

   if (!toy_compiler_translate_tgsi(tc, tokens, TRUE, tgsi))
      return FALSE;

   if (i965_debug & I965_DEBUG_VS) {
      debug_printf("TGSI translator:\n");
      toy_tgsi_dump(tgsi);
      debug_printf("\n");
      toy_compiler_dump(tc);
      debug_printf("\n");
   }

   return TRUE;
}

/**
 * Set up VS compile context.  This includes translating the TGSI tokens.
 */
static boolean
vs_setup(struct vs_compile_context *vcc,
         const struct i965_shader_state *state,
         const struct i965_shader_variant *variant)
{
   int num_consts;

   memset(vcc, 0, sizeof(*vcc));

   vcc->shader = CALLOC_STRUCT(i965_shader);
   if (!vcc->shader)
      return FALSE;

   vcc->variant = variant;

   toy_compiler_init(&vcc->tc, state->info.gen);
   vcc->tc.templ.access_mode = BRW_ALIGN_16;
   vcc->tc.templ.exec_size = BRW_EXECUTE_8;

   if (!vs_setup_tgsi(&vcc->tc, state->info.tokens, &vcc->tgsi)) {
      toy_compiler_cleanup(&vcc->tc);
      FREE(vcc->shader);
      return FALSE;
   }

   vs_setup_shader_in(vcc->shader, &vcc->tgsi);
   vs_setup_shader_out(vcc->shader, &vcc->tgsi, vcc->output_map);

   /* we do not make use of push constant buffers yet */
   num_consts = 0;

   /* r0 is reserved for payload header */
   vcc->first_const_grf = 1;
   vcc->first_vue_grf = vcc->first_const_grf + num_consts;
   vcc->first_free_grf = vcc->first_vue_grf + vcc->shader->in.count;
   vcc->last_free_grf = 127;

   /* m0 is reserved for system routines */
   vcc->first_free_mrf = 1;
   vcc->last_free_mrf = 15;

   vcc->num_grf_per_vrf = 1;

   vcc->shader->in.start_grf = vcc->first_vue_grf;

   return TRUE;
}

/**
 * Compile the vertex shader.
 */
struct i965_shader *
i965_shader_compile_vs(const struct i965_shader_state *state,
                       const struct i965_shader_variant *variant)
{
   struct vs_compile_context vcc;

   if (!vs_setup(&vcc, state, variant))
      return NULL;

   vs_write_vue(&vcc);

   if (!vs_compile(&vcc)) {
      FREE(vcc.shader);
      vcc.shader = NULL;
   }

   toy_tgsi_cleanup(&vcc.tgsi);
   toy_compiler_cleanup(&vcc.tc);

   return vcc.shader;
}
