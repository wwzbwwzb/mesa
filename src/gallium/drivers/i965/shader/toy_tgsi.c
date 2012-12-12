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

#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_strings.h"
#include "util/u_hash_table.h"
#include "toy_helpers.h"
#include "toy_tgsi.h"

/* map TGSI opcode to GEN opcode 1-to-1 */
static const struct {
   int opcode;
   int num_dst;
   int num_src;
} aos_simple_opcode_map[TGSI_OPCODE_LAST] = {
   [TGSI_OPCODE_ARL]          = { BRW_OPCODE_RNDD,                1, 1 },
   [TGSI_OPCODE_MOV]          = { BRW_OPCODE_MOV,                 1, 1 },
   [TGSI_OPCODE_RCP]          = { TOY_OPCODE_INV,                 1, 1 },
   [TGSI_OPCODE_RSQ]          = { TOY_OPCODE_RSQ,                 1, 1 },
   [TGSI_OPCODE_MUL]          = { BRW_OPCODE_MUL,                 1, 2 },
   [TGSI_OPCODE_ADD]          = { BRW_OPCODE_ADD,                 1, 2 },
   [TGSI_OPCODE_DP3]          = { BRW_OPCODE_DP3,                 1, 2 },
   [TGSI_OPCODE_DP4]          = { BRW_OPCODE_DP4,                 1, 2 },
   [TGSI_OPCODE_MIN]          = { BRW_OPCODE_SEL,                 1, 2 },
   [TGSI_OPCODE_MAX]          = { BRW_OPCODE_SEL,                 1, 2 },
   /* a later pass will move src[2] to accumulator */
   [TGSI_OPCODE_MAD]          = { BRW_OPCODE_MAC,                 1, 3 },
   [TGSI_OPCODE_SUB]          = { BRW_OPCODE_ADD,                 1, 2 },
   [TGSI_OPCODE_FRC]          = { BRW_OPCODE_FRC,                 1, 1 },
   [TGSI_OPCODE_FLR]          = { BRW_OPCODE_RNDD,                1, 1 },
   [TGSI_OPCODE_ROUND]        = { BRW_OPCODE_RNDE,                1, 1 },
   [TGSI_OPCODE_EX2]          = { TOY_OPCODE_EXP,                 1, 1 },
   [TGSI_OPCODE_LG2]          = { TOY_OPCODE_LOG,                 1, 1 },
   [TGSI_OPCODE_POW]          = { TOY_OPCODE_POW,                 1, 2 },
   [TGSI_OPCODE_ABS]          = { BRW_OPCODE_MOV,                 1, 1 },
   [TGSI_OPCODE_DPH]          = { BRW_OPCODE_DPH,                 1, 2 },
   [TGSI_OPCODE_COS]          = { TOY_OPCODE_COS,                 1, 1 },
   [TGSI_OPCODE_KILP]         = { TOY_OPCODE_KIL,                 0, 0 },
   [TGSI_OPCODE_SIN]          = { TOY_OPCODE_SIN,                 1, 1 },
   [TGSI_OPCODE_ARR]          = { BRW_OPCODE_RNDZ,                1, 1 },
   [TGSI_OPCODE_DP2]          = { BRW_OPCODE_DP2,                 1, 2 },
   [TGSI_OPCODE_IF]           = { BRW_OPCODE_IF,                  0, 1 },
   [TGSI_OPCODE_ELSE]         = { BRW_OPCODE_ELSE,                0, 0 },
   [TGSI_OPCODE_ENDIF]        = { BRW_OPCODE_ENDIF,               0, 0 },
   [TGSI_OPCODE_I2F]          = { BRW_OPCODE_MOV,                 1, 1 },
   [TGSI_OPCODE_NOT]          = { BRW_OPCODE_NOT,                 1, 1 },
   [TGSI_OPCODE_TRUNC]        = { BRW_OPCODE_RNDZ,                1, 1 },
   [TGSI_OPCODE_SHL]          = { BRW_OPCODE_SHL,                 1, 2 },
   [TGSI_OPCODE_AND]          = { BRW_OPCODE_AND,                 1, 2 },
   [TGSI_OPCODE_OR]           = { BRW_OPCODE_OR,                  1, 2 },
   [TGSI_OPCODE_MOD]          = { TOY_OPCODE_INT_DIV_REMAINDER,   1, 2 },
   [TGSI_OPCODE_XOR]          = { BRW_OPCODE_XOR,                 1, 2 },
   [TGSI_OPCODE_NOP]          = { BRW_OPCODE_NOP,                 0, 0 },
   [TGSI_OPCODE_KIL]          = { TOY_OPCODE_KIL,                 0, 1 },
   [TGSI_OPCODE_END]          = { BRW_OPCODE_NOP,                 0, 0 },
   [TGSI_OPCODE_F2I]          = { BRW_OPCODE_MOV,                 1, 1 },
   [TGSI_OPCODE_IDIV]         = { TOY_OPCODE_INT_DIV_QUOTIENT,    1, 2 },
   [TGSI_OPCODE_IMAX]         = { BRW_OPCODE_SEL,                 1, 2 },
   [TGSI_OPCODE_IMIN]         = { BRW_OPCODE_SEL,                 1, 2 },
   [TGSI_OPCODE_INEG]         = { BRW_OPCODE_MOV,                 1, 1 },
   [TGSI_OPCODE_ISHR]         = { BRW_OPCODE_ASR,                 1, 2 },
   [TGSI_OPCODE_F2U]          = { BRW_OPCODE_MOV,                 1, 1 },
   [TGSI_OPCODE_U2F]          = { BRW_OPCODE_MOV,                 1, 1 },
   [TGSI_OPCODE_UADD]         = { BRW_OPCODE_ADD,                 1, 2 },
   [TGSI_OPCODE_UDIV]         = { TOY_OPCODE_INT_DIV_QUOTIENT,    1, 2 },
   /* a later pass will move src[2] to accumulator */
   [TGSI_OPCODE_UMAD]         = { BRW_OPCODE_MAC,                 1, 3 },
   [TGSI_OPCODE_UMAX]         = { BRW_OPCODE_SEL,                 1, 2 },
   [TGSI_OPCODE_UMIN]         = { BRW_OPCODE_SEL,                 1, 2 },
   [TGSI_OPCODE_UMOD]         = { TOY_OPCODE_INT_DIV_REMAINDER,   1, 2 },
   [TGSI_OPCODE_UMUL]         = { BRW_OPCODE_MUL,                 1, 2 },
   [TGSI_OPCODE_USHR]         = { BRW_OPCODE_SHR,                 1, 2 },
   [TGSI_OPCODE_UARL]         = { BRW_OPCODE_MOV,                 1, 1 },
   [TGSI_OPCODE_IABS]         = { BRW_OPCODE_MOV,                 1, 1 },
};

static void
aos_simple(struct toy_compiler *tc,
           const struct tgsi_full_instruction *tgsi_inst,
           struct toy_dst *dst,
           struct toy_src *src)
{
   struct toy_inst *inst;
   int opcode;
   int cond_modifier = BRW_CONDITIONAL_NONE;
   int num_dst = tgsi_inst->Instruction.NumDstRegs;
   int num_src = tgsi_inst->Instruction.NumSrcRegs;
   int i;

   opcode = aos_simple_opcode_map[tgsi_inst->Instruction.Opcode].opcode;
   assert(num_dst == aos_simple_opcode_map[tgsi_inst->Instruction.Opcode].num_dst);
   assert(num_src == aos_simple_opcode_map[tgsi_inst->Instruction.Opcode].num_src);
   if (!opcode) {
      assert(!"invalid aos_simple() call");
      return;
   }

   /* no need to emit nop */
   if (opcode == BRW_OPCODE_NOP)
      return;

   inst = tc_add(tc);
   if (!inst)
      return;

   inst->opcode = opcode;

   switch (tgsi_inst->Instruction.Opcode) {
   case TGSI_OPCODE_MIN:
   case TGSI_OPCODE_IMIN:
   case TGSI_OPCODE_UMIN:
      cond_modifier = BRW_CONDITIONAL_L;
      break;
   case TGSI_OPCODE_MAX:
   case TGSI_OPCODE_IMAX:
   case TGSI_OPCODE_UMAX:
      cond_modifier = BRW_CONDITIONAL_GE;
      break;
   case TGSI_OPCODE_SUB:
      src[1] = tsrc_negate(src[1]);
      break;
   case TGSI_OPCODE_ABS:
   case TGSI_OPCODE_IABS:
      src[0] = tsrc_absolute(src[0]);
      break;
   case TGSI_OPCODE_IF:
      cond_modifier = BRW_CONDITIONAL_NEQ;
      num_src = 2;
      assert(src[0].type == TOY_TYPE_D);
      src[0] = tsrc_swizzle1(src[0], TOY_SWIZZLE_X);
      src[1] = tsrc_imm_d(0);
      break;
   case TGSI_OPCODE_INEG:
      src[0] = tsrc_negate(src[0]);
      break;
   case TGSI_OPCODE_SHL:
   case TGSI_OPCODE_ISHR:
   case TGSI_OPCODE_USHR:
      src[1] = tsrc_swizzle1(src[1], TOY_SWIZZLE_X);
      break;
   case TGSI_OPCODE_RCP:
   case TGSI_OPCODE_RSQ:
   case TGSI_OPCODE_EX2:
   case TGSI_OPCODE_LG2:
   case TGSI_OPCODE_COS:
   case TGSI_OPCODE_SIN:
      src[0] = tsrc_swizzle1(src[0], TOY_SWIZZLE_X);
      break;
   case TGSI_OPCODE_POW:
      src[0] = tsrc_swizzle1(src[0], TOY_SWIZZLE_X);
      src[1] = tsrc_swizzle1(src[1], TOY_SWIZZLE_X);
      break;
   }

   inst->cond_modifier = cond_modifier;

   if (num_dst) {
      assert(num_dst == 1);
      inst->dst = dst[0];
   }

   assert(num_src <= Elements(inst->src));
   for (i = 0; i < num_src; i++)
      inst->src[i] = src[i];
}

static void
aos_set_on_cond(struct toy_compiler *tc,
                const struct tgsi_full_instruction *tgsi_inst,
                struct toy_dst *dst,
                struct toy_src *src)
{
   struct toy_inst *inst;
   int cond;
   struct toy_src zero, one;

   switch (tgsi_inst->Instruction.Opcode) {
   case TGSI_OPCODE_SLT:
   case TGSI_OPCODE_ISLT:
   case TGSI_OPCODE_USLT:
      cond = BRW_CONDITIONAL_L;
      break;
   case TGSI_OPCODE_SGE:
   case TGSI_OPCODE_ISGE:
   case TGSI_OPCODE_USGE:
      cond = BRW_CONDITIONAL_GE;
      break;
   case TGSI_OPCODE_SEQ:
   case TGSI_OPCODE_USEQ:
      cond = BRW_CONDITIONAL_EQ;
      break;
   case TGSI_OPCODE_SGT:
      cond = BRW_CONDITIONAL_G;
      break;
   case TGSI_OPCODE_SLE:
      cond = BRW_CONDITIONAL_LE;
      break;
   case TGSI_OPCODE_SNE:
   case TGSI_OPCODE_USNE:
      cond = BRW_CONDITIONAL_NEQ;
      break;
   default:
      assert(!"invalid aos_set_on_cond() call");
      return;
   }

   /* note that for integer versions, all bits are set */
   switch (dst[0].type) {
   case TOY_TYPE_F:
   default:
      zero = tsrc_imm_f(0.0f);
      one = tsrc_imm_f(1.0f);
      break;
   case TOY_TYPE_D:
      zero = tsrc_imm_d(0);
      one = tsrc_imm_d(-1);
      break;
   case TOY_TYPE_UD:
      zero = tsrc_imm_ud(0);
      one = tsrc_imm_ud(~0);
      break;
   }

   tc_MOV(tc, dst[0], zero);
   tc_CMP(tc, tdst_null(), src[0], src[1], cond);
   inst = tc_MOV(tc, dst[0], one);
   inst->pred_ctrl = BRW_PREDICATE_NORMAL;
}

static void
aos_compare(struct toy_compiler *tc,
            const struct tgsi_full_instruction *tgsi_inst,
            struct toy_dst *dst,
            struct toy_src *src)
{
   struct toy_inst *inst;
   struct toy_src zero;

   switch (tgsi_inst->Instruction.Opcode) {
   case TGSI_OPCODE_CMP:
      zero = tsrc_imm_f(0.0f);
      break;
   case TGSI_OPCODE_UCMP:
      zero = tsrc_imm_ud(0);
      break;
   default:
      assert(!"invalid aos_compare() call");
      return;
   }

   tc_CMP(tc, tdst_null(), src[0], zero, BRW_CONDITIONAL_L);
   inst = tc_SEL(tc, dst[0], src[1], src[2], BRW_CONDITIONAL_NONE);
   inst->pred_ctrl = BRW_PREDICATE_NORMAL;
}

static void
aos_set_sign(struct toy_compiler *tc,
             const struct tgsi_full_instruction *tgsi_inst,
             struct toy_dst *dst,
             struct toy_src *src)
{
   struct toy_inst *inst;
   struct toy_src zero, one, neg_one;

   switch (tgsi_inst->Instruction.Opcode) {
   case TGSI_OPCODE_SSG:
      zero = tsrc_imm_f(0.0f);
      one = tsrc_imm_f(1.0f);
      neg_one = tsrc_imm_f(-1.0f);
      break;
   case TGSI_OPCODE_ISSG:
      zero = tsrc_imm_d(0);
      one = tsrc_imm_d(1);
      neg_one = tsrc_imm_d(-1);
      break;
   default:
      assert(!"invalid aos_set_sign() call");
      return;
   }

   tc_MOV(tc, dst[0], zero);

   tc_CMP(tc, tdst_null(), src[0], zero, BRW_CONDITIONAL_G);
   inst = tc_MOV(tc, dst[0], one);
   inst->pred_ctrl = BRW_PREDICATE_NORMAL;

   tc_CMP(tc, tdst_null(), src[0], zero, BRW_CONDITIONAL_L);
   inst = tc_MOV(tc, dst[0], neg_one);
   inst->pred_ctrl = BRW_PREDICATE_NORMAL;
}

static void
aos_tex(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   struct toy_inst *inst;
   enum toy_opcode opcode;
   int i;

   switch (tgsi_inst->Instruction.Opcode) {
   case TGSI_OPCODE_TEX:
      opcode = TOY_OPCODE_TGSI_TEX;
      break;
   case TGSI_OPCODE_TXD:
      opcode = TOY_OPCODE_TGSI_TXD;
      break;
   case TGSI_OPCODE_TXP:
      opcode = TOY_OPCODE_TGSI_TXP;
      break;
   case TGSI_OPCODE_TXB:
      opcode = TOY_OPCODE_TGSI_TXB;
      break;
   case TGSI_OPCODE_TXL:
      opcode = TOY_OPCODE_TGSI_TXL;
      break;
   case TGSI_OPCODE_TXF:
      opcode = TOY_OPCODE_TGSI_TXF;
      break;
   case TGSI_OPCODE_TXQ:
      opcode = TOY_OPCODE_TGSI_TXQ;
      break;
   case TGSI_OPCODE_TXQ_LZ:
      opcode = TOY_OPCODE_TGSI_TXQ_LZ;
      break;
   case TGSI_OPCODE_TEX2:
      opcode = TOY_OPCODE_TGSI_TEX2;
   case TGSI_OPCODE_TXB2:
      opcode = TOY_OPCODE_TGSI_TXB2;
      break;
   case TGSI_OPCODE_TXL2:
      opcode = TOY_OPCODE_TGSI_TXL2;
      break;
   default:
      assert(!"unsupported texturing opcode");
      return;
      break;
   }

   inst = tc_add(tc);
   inst->opcode = opcode;
   inst->tex.target = tgsi_inst->Texture.Texture;

   assert(tgsi_inst->Instruction.NumSrcRegs <= Elements(inst->src));
   assert(tgsi_inst->Instruction.NumDstRegs == 1);

   inst->dst = dst[0];
   for (i = 0; i < tgsi_inst->Instruction.NumSrcRegs; i++)
      inst->src[i] = src[i];
}

static void
aos_sample(struct toy_compiler *tc,
           const struct tgsi_full_instruction *tgsi_inst,
           struct toy_dst *dst,
           struct toy_src *src)
{
   struct toy_inst *inst;
   enum toy_opcode opcode;
   int i;

   assert(!"sampling untested");

   switch (tgsi_inst->Instruction.Opcode) {
   case TGSI_OPCODE_SAMPLE:
      opcode = TOY_OPCODE_TGSI_SAMPLE;
      break;
   case TGSI_OPCODE_SAMPLE_I:
      opcode = TOY_OPCODE_TGSI_SAMPLE_I;
      break;
   case TGSI_OPCODE_SAMPLE_I_MS:
      opcode = TOY_OPCODE_TGSI_SAMPLE_I_MS;
      break;
   case TGSI_OPCODE_SAMPLE_B:
      opcode = TOY_OPCODE_TGSI_SAMPLE_B;
      break;
   case TGSI_OPCODE_SAMPLE_C:
      opcode = TOY_OPCODE_TGSI_SAMPLE_C;
      break;
   case TGSI_OPCODE_SAMPLE_C_LZ:
      opcode = TOY_OPCODE_TGSI_SAMPLE_C_LZ;
      break;
   case TGSI_OPCODE_SAMPLE_D:
      opcode = TOY_OPCODE_TGSI_SAMPLE_D;
      break;
   case TGSI_OPCODE_SAMPLE_L:
      opcode = TOY_OPCODE_TGSI_SAMPLE_L;
      break;
   case TGSI_OPCODE_GATHER4:
      opcode = TOY_OPCODE_TGSI_GATHER4;
      break;
   case TGSI_OPCODE_SVIEWINFO:
      opcode = TOY_OPCODE_TGSI_SVIEWINFO;
      break;
   case TGSI_OPCODE_SAMPLE_POS:
      opcode = TOY_OPCODE_TGSI_SAMPLE_POS;
      break;
   case TGSI_OPCODE_SAMPLE_INFO:
      opcode = TOY_OPCODE_TGSI_SAMPLE_INFO;
      break;
   default:
      assert(!"unsupported sampling opcode");
      return;
      break;
   }

   inst = tc_add(tc);
   inst->opcode = opcode;

   assert(tgsi_inst->Instruction.NumSrcRegs <= Elements(inst->src));
   assert(tgsi_inst->Instruction.NumDstRegs == 1);

   inst->dst = dst[0];
   for (i = 0; i < tgsi_inst->Instruction.NumSrcRegs; i++)
      inst->src[i] = src[i];
}

static void
aos_LIT(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   struct toy_inst *inst;

   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_XW), tsrc_imm_f(1.0f));

   if (!(dst[0].writemask & TOY_WRITEMASK_YZ))
      return;

   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_YZ), tsrc_imm_f(0.0f));

   tc_CMP(tc, tdst_null(),
         tsrc_swizzle1(src[0], TOY_SWIZZLE_X),
         tsrc_imm_f(0.0f),
         BRW_CONDITIONAL_G);

   inst = tc_MOV(tc,
         tdst_writemask(dst[0], TOY_WRITEMASK_Y),
         tsrc_swizzle1(src[0], TOY_SWIZZLE_X));
   inst->pred_ctrl = BRW_PREDICATE_NORMAL;

   /* clamp W to (-128, 128)? */
   inst = tc_POW(tc,
         tdst_writemask(dst[0], TOY_WRITEMASK_Z),
         tsrc_swizzle1(src[0], TOY_SWIZZLE_Y),
         tsrc_swizzle1(src[0], TOY_SWIZZLE_W));
   inst->pred_ctrl = BRW_PREDICATE_NORMAL;
}

static void
aos_EXP(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   struct toy_src src0 = tsrc_swizzle1(src[0], TOY_SWIZZLE_X);

   if (dst[0].writemask & TOY_WRITEMASK_X) {
      struct toy_dst tmp =
         tdst_d(tdst_writemask(tc_alloc_tmp(tc), TOY_WRITEMASK_X));

      tc_RNDD(tc, tmp, src0);

      /* construct the floating point number manually */
      tc_ADD(tc, tmp, tsrc_from(tmp), tsrc_imm_d(127));
      tc_SHL(tc, tdst_d(tdst_writemask(dst[0], TOY_WRITEMASK_X)),
            tsrc_from(tmp), tsrc_imm_d(23));
   }

   tc_FRC(tc, tdst_writemask(dst[0], TOY_WRITEMASK_Y), src0);
   tc_EXP(tc, tdst_writemask(dst[0], TOY_WRITEMASK_Z), src0);
   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_W), tsrc_imm_f(1.0f));
}

static void
aos_LOG(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   struct toy_src src0 = tsrc_swizzle1(src[0], TOY_SWIZZLE_X);

   if (dst[0].writemask & TOY_WRITEMASK_XY) {
      struct toy_dst tmp;

      tmp = tdst_d(tdst_writemask(tc_alloc_tmp(tc), TOY_WRITEMASK_X));

      /* exponent */
      tc_SHR(tc, tmp, tsrc_absolute(tsrc_d(src0)), tsrc_imm_d(23));
      tc_ADD(tc, tdst_writemask(dst[0], TOY_WRITEMASK_X),
            tsrc_from(tmp), tsrc_imm_d(-127));

      /* mantissa  */
      tc_AND(tc, tmp, tsrc_d(src0), tsrc_imm_d((1 << 23) - 1));
      tc_OR(tc, tdst_writemask(tdst_d(dst[0]), TOY_WRITEMASK_Y),
            tsrc_from(tmp), tsrc_imm_d(127 << 23));
   }

   tc_LOG(tc, tdst_writemask(dst[0], TOY_WRITEMASK_Z), src0);
   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_W), tsrc_imm_f(1.0f));
}

static void
aos_DST(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_X), tsrc_imm_f(1.0f));
   tc_MUL(tc, tdst_writemask(dst[0], TOY_WRITEMASK_Y), src[0], src[1]);
   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_Z), src[0]);
   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_W), src[1]);
}

static void
aos_LRP(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   struct toy_dst tmp = tc_alloc_tmp(tc);

   tc_ADD(tc, tmp, tsrc_negate(src[0]), tsrc_imm_f(1.0f));
   tc_MUL(tc, tmp, tsrc_from(tmp), src[2]);
   tc_MAC(tc, dst[0], src[0], src[1], tsrc_from(tmp));
}

static void
aos_CND(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   struct toy_inst *inst;

   assert(!"CND untested");

   tc_CMP(tc, tdst_null(), src[2], tsrc_imm_f(0.5f), BRW_CONDITIONAL_G);
   inst = tc_SEL(tc, dst[0], src[0], src[1], BRW_CONDITIONAL_NONE);
   inst->pred_ctrl = BRW_PREDICATE_NORMAL;
}

static void
aos_DP2A(struct toy_compiler *tc,
         const struct tgsi_full_instruction *tgsi_inst,
         struct toy_dst *dst,
         struct toy_src *src)
{
   struct toy_dst tmp = tc_alloc_tmp(tc);

   assert(!"DP2A untested");

   tc_DP2(tc, tmp, src[0], src[1]);
   tc_ADD(tc, dst[0], tsrc_swizzle1(tsrc_from(tmp), TOY_SWIZZLE_X), src[2]);
}

static void
aos_CLAMP(struct toy_compiler *tc,
          const struct tgsi_full_instruction *tgsi_inst,
          struct toy_dst *dst,
          struct toy_src *src)
{
   assert(!"CLAMP untested");

   tc_SEL(tc, dst[0], src[0], src[1], BRW_CONDITIONAL_GE);
   tc_SEL(tc, dst[0], src[2], tsrc_from(dst[0]), BRW_CONDITIONAL_L);
}

static void
aos_XPD(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   struct toy_dst tmp = tc_alloc_tmp(tc);

   tc_MUL(tc, tdst_writemask(tmp, TOY_WRITEMASK_XYZ),
         tsrc_swizzle(src[0], TOY_SWIZZLE_Z, TOY_SWIZZLE_X,
                              TOY_SWIZZLE_Y, TOY_SWIZZLE_W),
         tsrc_swizzle(src[1], TOY_SWIZZLE_Y, TOY_SWIZZLE_Z,
                              TOY_SWIZZLE_X, TOY_SWIZZLE_W));

   tc_MAC(tc, tdst_writemask(dst[0], TOY_WRITEMASK_XYZ),
         tsrc_swizzle(src[0], TOY_SWIZZLE_Y, TOY_SWIZZLE_Z,
                              TOY_SWIZZLE_X, TOY_SWIZZLE_W),
         tsrc_swizzle(src[1], TOY_SWIZZLE_Z, TOY_SWIZZLE_X,
                              TOY_SWIZZLE_Y, TOY_SWIZZLE_W),
         tsrc_negate(tsrc_from(tmp)));

   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_W),
         tsrc_imm_f(1.0f));
}

static void
aos_PK2H(struct toy_compiler *tc,
         const struct tgsi_full_instruction *tgsi_inst,
         struct toy_dst *dst,
         struct toy_src *src)
{
   const struct toy_src h1 = tsrc_ud(tsrc_swizzle1(src[0], TOY_SWIZZLE_X));
   const struct toy_src h2 = tsrc_ud(tsrc_swizzle1(src[0], TOY_SWIZZLE_Y));
   struct toy_dst tmp = tdst_ud(tc_alloc_tmp(tc));

   assert(!"PK2H untested");

   tc_SHL(tc, tmp, h2, tsrc_imm_ud(16));
   tc_OR(tc, tdst_ud(dst[0]), h1, tsrc_from(tmp));
}

static void
aos_SFL(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   assert(!"SFL untested");

   tc_MOV(tc, dst[0], tsrc_imm_f(0.0f));
}

static void
aos_STR(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   assert(!"STR untested");

   tc_MOV(tc, dst[0], tsrc_imm_f(1.0f));
}

static void
aos_UP2H(struct toy_compiler *tc,
         const struct tgsi_full_instruction *tgsi_inst,
         struct toy_dst *dst,
         struct toy_src *src)
{
   assert(!"UP2H untested");

   tc_AND(tc, tdst_writemask(tdst_ud(dst[0]), TOY_WRITEMASK_XZ),
         tsrc_ud(src[0]), tsrc_imm_ud(0xffff));
   tc_SHR(tc, tdst_writemask(tdst_ud(dst[0]), TOY_WRITEMASK_YW),
         tsrc_ud(src[0]), tsrc_imm_ud(16));
}

static void
aos_SCS(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   assert(!"SCS untested");

   tc_add1(tc, TOY_OPCODE_COS,
         tdst_writemask(dst[0], TOY_WRITEMASK_X), src[0]);

   tc_add1(tc, TOY_OPCODE_SIN,
         tdst_writemask(dst[0], TOY_WRITEMASK_Y), src[0]);

   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_Z), tsrc_imm_f(0.0f));
   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_W), tsrc_imm_f(1.0f));
}

static void
aos_NRM(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   struct toy_dst tmp = tc_alloc_tmp(tc);

   assert(!"NRM untested");

   tc_DP3(tc, tmp, src[0], src[0]);
   tc_INV(tc, tmp, tsrc_from(tmp));
   tc_MUL(tc, tdst_writemask(dst[0], TOY_WRITEMASK_XYZ),
         src[0], tsrc_from(tmp));

   tc_MOV(tc, tdst_writemask(dst[0], TOY_WRITEMASK_W), tsrc_imm_f(1.0f));
}

static void
aos_DIV(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   struct toy_dst tmp = tc_alloc_tmp(tc);

   assert(!"DIV untested");

   tc_INV(tc, tmp, src[1]);
   tc_MUL(tc, dst[0], src[0], tsrc_from(tmp));
}

static void
aos_BRK(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   tc_add0(tc, BRW_OPCODE_BREAK);
}

static void
aos_CEIL(struct toy_compiler *tc,
         const struct tgsi_full_instruction *tgsi_inst,
         struct toy_dst *dst,
         struct toy_src *src)
{
   struct toy_dst tmp = tc_alloc_tmp(tc);

   tc_RNDD(tc, tmp, tsrc_negate(src[0]));
   tc_MOV(tc, dst[0], tsrc_negate(tsrc_from(tmp)));
}

static void
aos_SAD(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst,
        struct toy_src *src)
{
   struct toy_dst tmp = tc_alloc_tmp(tc);

   assert(!"SAD untested");

   tc_ADD(tc, tmp, src[0], tsrc_negate(src[1]));
   tc_ADD(tc, dst[0], tsrc_absolute(tsrc_from(tmp)), src[2]);
}

static void
aos_CONT(struct toy_compiler *tc,
         const struct tgsi_full_instruction *tgsi_inst,
         struct toy_dst *dst,
         struct toy_src *src)
{
   tc_add0(tc, BRW_OPCODE_CONTINUE);
}

static void
aos_BGNLOOP(struct toy_compiler *tc,
            const struct tgsi_full_instruction *tgsi_inst,
            struct toy_dst *dst,
            struct toy_src *src)
{
   struct toy_inst *inst;

   inst = tc_add0(tc, BRW_OPCODE_DO);
   /* this is just a marker */
   inst->marker = TRUE;
}

static void
aos_ENDLOOP(struct toy_compiler *tc,
            const struct tgsi_full_instruction *tgsi_inst,
            struct toy_dst *dst,
            struct toy_src *src)
{
   tc_add0(tc, BRW_OPCODE_WHILE);
}

static void
aos_NRM4(struct toy_compiler *tc,
         const struct tgsi_full_instruction *tgsi_inst,
         struct toy_dst *dst,
         struct toy_src *src)
{
   struct toy_dst tmp = tc_alloc_tmp(tc);

   assert(!"NRM4 untested");

   tc_DP4(tc, tmp, src[0], src[0]);
   tc_INV(tc, tmp, tsrc_from(tmp));
   tc_MUL(tc, dst[0], tsrc_swizzle1(src[0], TOY_SWIZZLE_X), tsrc_from(tmp));
}

static void
aos_unsupported(struct toy_compiler *tc,
                const struct tgsi_full_instruction *tgsi_inst,
                struct toy_dst *dst,
                struct toy_src *src)
{
   const char *name = tgsi_get_opcode_name(tgsi_inst->Instruction.Opcode);

   debug_printf("unsupported TGSI opcode: TGSI_OPCODE_%s\n", name);
   assert(!"unsupported TGSI instruction");
}

static const toy_tgsi_translate aos_translate_table[TGSI_OPCODE_LAST] = {
   [TGSI_OPCODE_ARL]          = aos_simple,
   [TGSI_OPCODE_MOV]          = aos_simple,
   [TGSI_OPCODE_LIT]          = aos_LIT,
   [TGSI_OPCODE_RCP]          = aos_simple,
   [TGSI_OPCODE_RSQ]          = aos_simple,
   [TGSI_OPCODE_EXP]          = aos_EXP,
   [TGSI_OPCODE_LOG]          = aos_LOG,
   [TGSI_OPCODE_MUL]          = aos_simple,
   [TGSI_OPCODE_ADD]          = aos_simple,
   [TGSI_OPCODE_DP3]          = aos_simple,
   [TGSI_OPCODE_DP4]          = aos_simple,
   [TGSI_OPCODE_DST]          = aos_DST,
   [TGSI_OPCODE_MIN]          = aos_simple,
   [TGSI_OPCODE_MAX]          = aos_simple,
   [TGSI_OPCODE_SLT]          = aos_set_on_cond,
   [TGSI_OPCODE_SGE]          = aos_set_on_cond,
   [TGSI_OPCODE_MAD]          = aos_simple,
   [TGSI_OPCODE_SUB]          = aos_simple,
   [TGSI_OPCODE_LRP]          = aos_LRP,
   [TGSI_OPCODE_CND]          = aos_CND,
   [20]                       = aos_unsupported,
   [TGSI_OPCODE_DP2A]         = aos_DP2A,
   [22]                       = aos_unsupported,
   [23]                       = aos_unsupported,
   [TGSI_OPCODE_FRC]          = aos_simple,
   [TGSI_OPCODE_CLAMP]        = aos_CLAMP,
   [TGSI_OPCODE_FLR]          = aos_simple,
   [TGSI_OPCODE_ROUND]        = aos_simple,
   [TGSI_OPCODE_EX2]          = aos_simple,
   [TGSI_OPCODE_LG2]          = aos_simple,
   [TGSI_OPCODE_POW]          = aos_simple,
   [TGSI_OPCODE_XPD]          = aos_XPD,
   [32]                       = aos_unsupported,
   [TGSI_OPCODE_ABS]          = aos_simple,
   [TGSI_OPCODE_RCC]          = aos_unsupported,
   [TGSI_OPCODE_DPH]          = aos_simple,
   [TGSI_OPCODE_COS]          = aos_simple,
   [TGSI_OPCODE_DDX]          = aos_unsupported,
   [TGSI_OPCODE_DDY]          = aos_unsupported,
   [TGSI_OPCODE_KILP]         = aos_simple,
   [TGSI_OPCODE_PK2H]         = aos_PK2H,
   [TGSI_OPCODE_PK2US]        = aos_unsupported,
   [TGSI_OPCODE_PK4B]         = aos_unsupported,
   [TGSI_OPCODE_PK4UB]        = aos_unsupported,
   [TGSI_OPCODE_RFL]          = aos_unsupported,
   [TGSI_OPCODE_SEQ]          = aos_set_on_cond,
   [TGSI_OPCODE_SFL]          = aos_SFL,
   [TGSI_OPCODE_SGT]          = aos_set_on_cond,
   [TGSI_OPCODE_SIN]          = aos_simple,
   [TGSI_OPCODE_SLE]          = aos_set_on_cond,
   [TGSI_OPCODE_SNE]          = aos_set_on_cond,
   [TGSI_OPCODE_STR]          = aos_STR,
   [TGSI_OPCODE_TEX]          = aos_tex,
   [TGSI_OPCODE_TXD]          = aos_tex,
   [TGSI_OPCODE_TXP]          = aos_tex,
   [TGSI_OPCODE_UP2H]         = aos_UP2H,
   [TGSI_OPCODE_UP2US]        = aos_unsupported,
   [TGSI_OPCODE_UP4B]         = aos_unsupported,
   [TGSI_OPCODE_UP4UB]        = aos_unsupported,
   [TGSI_OPCODE_X2D]          = aos_unsupported,
   [TGSI_OPCODE_ARA]          = aos_unsupported,
   [TGSI_OPCODE_ARR]          = aos_simple,
   [TGSI_OPCODE_BRA]          = aos_unsupported,
   [TGSI_OPCODE_CAL]          = aos_unsupported,
   [TGSI_OPCODE_RET]          = aos_unsupported,
   [TGSI_OPCODE_SSG]          = aos_set_sign,
   [TGSI_OPCODE_CMP]          = aos_compare,
   [TGSI_OPCODE_SCS]          = aos_SCS,
   [TGSI_OPCODE_TXB]          = aos_tex,
   [TGSI_OPCODE_NRM]          = aos_NRM,
   [TGSI_OPCODE_DIV]          = aos_DIV,
   [TGSI_OPCODE_DP2]          = aos_simple,
   [TGSI_OPCODE_TXL]          = aos_tex,
   [TGSI_OPCODE_BRK]          = aos_BRK,
   [TGSI_OPCODE_IF]           = aos_simple,
   [75]                       = aos_unsupported,
   [76]                       = aos_unsupported,
   [TGSI_OPCODE_ELSE]         = aos_simple,
   [TGSI_OPCODE_ENDIF]        = aos_simple,
   [79]                       = aos_unsupported,
   [80]                       = aos_unsupported,
   [TGSI_OPCODE_PUSHA]        = aos_unsupported,
   [TGSI_OPCODE_POPA]         = aos_unsupported,
   [TGSI_OPCODE_CEIL]         = aos_CEIL,
   [TGSI_OPCODE_I2F]          = aos_simple,
   [TGSI_OPCODE_NOT]          = aos_simple,
   [TGSI_OPCODE_TRUNC]        = aos_simple,
   [TGSI_OPCODE_SHL]          = aos_simple,
   [88]                       = aos_unsupported,
   [TGSI_OPCODE_AND]          = aos_simple,
   [TGSI_OPCODE_OR]           = aos_simple,
   [TGSI_OPCODE_MOD]          = aos_simple,
   [TGSI_OPCODE_XOR]          = aos_simple,
   [TGSI_OPCODE_SAD]          = aos_SAD,
   [TGSI_OPCODE_TXF]          = aos_tex,
   [TGSI_OPCODE_TXQ]          = aos_tex,
   [TGSI_OPCODE_CONT]         = aos_CONT,
   [TGSI_OPCODE_EMIT]         = aos_unsupported,
   [TGSI_OPCODE_ENDPRIM]      = aos_unsupported,
   [TGSI_OPCODE_BGNLOOP]      = aos_BGNLOOP,
   [TGSI_OPCODE_BGNSUB]       = aos_unsupported,
   [TGSI_OPCODE_ENDLOOP]      = aos_ENDLOOP,
   [TGSI_OPCODE_ENDSUB]       = aos_unsupported,
   [TGSI_OPCODE_TXQ_LZ]       = aos_tex,
   [104]                      = aos_unsupported,
   [105]                      = aos_unsupported,
   [106]                      = aos_unsupported,
   [TGSI_OPCODE_NOP]          = aos_simple,
   [108]                      = aos_unsupported,
   [109]                      = aos_unsupported,
   [110]                      = aos_unsupported,
   [111]                      = aos_unsupported,
   [TGSI_OPCODE_NRM4]         = aos_NRM4,
   [TGSI_OPCODE_CALLNZ]       = aos_unsupported,
   [TGSI_OPCODE_IFC]          = aos_unsupported,
   [TGSI_OPCODE_BREAKC]       = aos_unsupported,
   [TGSI_OPCODE_KIL]          = aos_simple,
   [TGSI_OPCODE_END]          = aos_simple,
   [118]                      = aos_unsupported,
   [TGSI_OPCODE_F2I]          = aos_simple,
   [TGSI_OPCODE_IDIV]         = aos_simple,
   [TGSI_OPCODE_IMAX]         = aos_simple,
   [TGSI_OPCODE_IMIN]         = aos_simple,
   [TGSI_OPCODE_INEG]         = aos_simple,
   [TGSI_OPCODE_ISGE]         = aos_set_on_cond,
   [TGSI_OPCODE_ISHR]         = aos_simple,
   [TGSI_OPCODE_ISLT]         = aos_set_on_cond,
   [TGSI_OPCODE_F2U]          = aos_simple,
   [TGSI_OPCODE_U2F]          = aos_simple,
   [TGSI_OPCODE_UADD]         = aos_simple,
   [TGSI_OPCODE_UDIV]         = aos_simple,
   [TGSI_OPCODE_UMAD]         = aos_simple,
   [TGSI_OPCODE_UMAX]         = aos_simple,
   [TGSI_OPCODE_UMIN]         = aos_simple,
   [TGSI_OPCODE_UMOD]         = aos_simple,
   [TGSI_OPCODE_UMUL]         = aos_simple,
   [TGSI_OPCODE_USEQ]         = aos_set_on_cond,
   [TGSI_OPCODE_USGE]         = aos_set_on_cond,
   [TGSI_OPCODE_USHR]         = aos_simple,
   [TGSI_OPCODE_USLT]         = aos_set_on_cond,
   [TGSI_OPCODE_USNE]         = aos_set_on_cond,
   [TGSI_OPCODE_SWITCH]       = aos_unsupported,
   [TGSI_OPCODE_CASE]         = aos_unsupported,
   [TGSI_OPCODE_DEFAULT]      = aos_unsupported,
   [TGSI_OPCODE_ENDSWITCH]    = aos_unsupported,
   [TGSI_OPCODE_SAMPLE]       = aos_sample,
   [TGSI_OPCODE_SAMPLE_I]     = aos_sample,
   [TGSI_OPCODE_SAMPLE_I_MS]  = aos_sample,
   [TGSI_OPCODE_SAMPLE_B]     = aos_sample,
   [TGSI_OPCODE_SAMPLE_C]     = aos_sample,
   [TGSI_OPCODE_SAMPLE_C_LZ]  = aos_sample,
   [TGSI_OPCODE_SAMPLE_D]     = aos_sample,
   [TGSI_OPCODE_SAMPLE_L]     = aos_sample,
   [TGSI_OPCODE_GATHER4]      = aos_sample,
   [TGSI_OPCODE_SVIEWINFO]    = aos_sample,
   [TGSI_OPCODE_SAMPLE_POS]   = aos_sample,
   [TGSI_OPCODE_SAMPLE_INFO]  = aos_sample,
   [TGSI_OPCODE_UARL]         = aos_simple,
   [TGSI_OPCODE_UCMP]         = aos_compare,
   [TGSI_OPCODE_IABS]         = aos_simple,
   [TGSI_OPCODE_ISSG]         = aos_set_sign,
   [TGSI_OPCODE_LOAD]         = aos_unsupported,
   [TGSI_OPCODE_STORE]        = aos_unsupported,
   [TGSI_OPCODE_MFENCE]       = aos_unsupported,
   [TGSI_OPCODE_LFENCE]       = aos_unsupported,
   [TGSI_OPCODE_SFENCE]       = aos_unsupported,
   [TGSI_OPCODE_BARRIER]      = aos_unsupported,
   [TGSI_OPCODE_ATOMUADD]     = aos_unsupported,
   [TGSI_OPCODE_ATOMXCHG]     = aos_unsupported,
   [TGSI_OPCODE_ATOMCAS]      = aos_unsupported,
   [TGSI_OPCODE_ATOMAND]      = aos_unsupported,
   [TGSI_OPCODE_ATOMOR]       = aos_unsupported,
   [TGSI_OPCODE_ATOMXOR]      = aos_unsupported,
   [TGSI_OPCODE_ATOMUMIN]     = aos_unsupported,
   [TGSI_OPCODE_ATOMUMAX]     = aos_unsupported,
   [TGSI_OPCODE_ATOMIMIN]     = aos_unsupported,
   [TGSI_OPCODE_ATOMIMAX]     = aos_unsupported,
   [TGSI_OPCODE_TEX2]         = aos_tex,
   [TGSI_OPCODE_TXB2]         = aos_tex,
   [TGSI_OPCODE_TXL2]         = aos_tex,
};

static void
soa_passthrough(struct toy_compiler *tc,
                const struct tgsi_full_instruction *tgsi_inst,
                struct toy_dst *dst_,
                struct toy_src *src_)
{
   const toy_tgsi_translate translate =
      aos_translate_table[tgsi_inst->Instruction.Opcode];

   translate(tc, tgsi_inst, dst_, src_);
}

static void
soa_per_channel(struct toy_compiler *tc,
                const struct tgsi_full_instruction *tgsi_inst,
                struct toy_dst *dst_,
                struct toy_src *src_)
{
   struct toy_dst dst[TGSI_FULL_MAX_DST_REGISTERS][4];
   struct toy_src src[TGSI_FULL_MAX_SRC_REGISTERS][4];
   int i, ch;

   for (i = 0; i < tgsi_inst->Instruction.NumDstRegs; i++)
      tdst_transpose(dst_[i], dst[i]);
   for (i = 0; i < tgsi_inst->Instruction.NumSrcRegs; i++)
      tsrc_transpose(src_[i], src[i]);

   /* emit the same instruction four times for the four channels */
   for (ch = 0; ch < 4; ch++) {
      struct toy_dst aos_dst[TGSI_FULL_MAX_DST_REGISTERS];
      struct toy_src aos_src[TGSI_FULL_MAX_SRC_REGISTERS];

      for (i = 0; i < tgsi_inst->Instruction.NumDstRegs; i++)
         aos_dst[i] = dst[i][ch];
      for (i = 0; i < tgsi_inst->Instruction.NumSrcRegs; i++)
         aos_src[i] = src[i][ch];

      aos_translate_table[tgsi_inst->Instruction.Opcode](tc,
            tgsi_inst, aos_dst, aos_src);
   }
}

static void
soa_shift(struct toy_compiler *tc,
          const struct tgsi_full_instruction *tgsi_inst,
          struct toy_dst *dst_,
          struct toy_src *src_)
{
   struct toy_dst dst0[4];
   struct toy_src src[2][4];
   int opcode, ch;

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src[0]);
   tsrc_transpose(src_[1], src[1]);

   opcode = aos_simple_opcode_map[tgsi_inst->Instruction.Opcode].opcode;
   assert(opcode);

   switch (tgsi_inst->Instruction.Opcode) {
   case TGSI_OPCODE_SHL:
   case TGSI_OPCODE_ISHR:
   case TGSI_OPCODE_USHR:
      for (ch = 0; ch < 4; ch++)
         tc_add2(tc, opcode, dst0[ch], src[0][ch], src[1][0]);
      break;
   default:
      assert(!"invalid soa_shift() call");
      return;
   }
}

static void
soa_scalar_replicate(struct toy_compiler *tc,
                     const struct tgsi_full_instruction *tgsi_inst,
                     struct toy_dst *dst_,
                     struct toy_src *src_)
{
   struct toy_dst dst0[4], tmp;
   struct toy_src srcx[TGSI_FULL_MAX_SRC_REGISTERS];
   int opcode, i;

   assert(tgsi_inst->Instruction.NumDstRegs == 1);

   tdst_transpose(dst_[0], dst0);
   for (i = 0; i < tgsi_inst->Instruction.NumSrcRegs; i++) {
      struct toy_src tmp[4];

      tsrc_transpose(src_[i], tmp);
      /* only the X channels */
      srcx[i] = tmp[0];
   }

   tmp = tc_alloc_tmp(tc);

   opcode = aos_simple_opcode_map[tgsi_inst->Instruction.Opcode].opcode;
   assert(opcode);

   switch (tgsi_inst->Instruction.Opcode) {
   case TGSI_OPCODE_RCP:
   case TGSI_OPCODE_RSQ:
   case TGSI_OPCODE_EX2:
   case TGSI_OPCODE_LG2:
   case TGSI_OPCODE_COS:
   case TGSI_OPCODE_SIN:
      tc_add1(tc, opcode, tmp, srcx[0]);
      break;
   case TGSI_OPCODE_POW:
      tc_add2(tc, opcode, tmp, srcx[0], srcx[1]);
      break;
   default:
      assert(!"invalid soa_scalar_replicate() call");
      return;
   }

   /* replicate the result */
   for (i = 0; i < 4; i++)
      tc_MOV(tc, dst0[i], tsrc_from(tmp));
}

static void
soa_dot_product(struct toy_compiler *tc,
                const struct tgsi_full_instruction *tgsi_inst,
                struct toy_dst *dst_,
                struct toy_src *src_)
{
   struct toy_dst dst0[4], tmp;
   struct toy_src src[TGSI_FULL_MAX_SRC_REGISTERS][4];
   int i;

   tdst_transpose(dst_[0], dst0);
   for (i = 0; i < tgsi_inst->Instruction.NumSrcRegs; i++)
      tsrc_transpose(src_[i], src[i]);

   tmp = tc_alloc_tmp(tc);

   switch (tgsi_inst->Instruction.Opcode) {
   case TGSI_OPCODE_DP2:
      tc_MUL(tc, tmp, src[0][1], src[1][1]);
      tc_MAC(tc, tmp, src[0][0], src[1][0], tsrc_from(tmp));
      break;
   case TGSI_OPCODE_DP2A:
      tc_MAC(tc, tmp, src[0][1], src[1][1], src[2][0]);
      tc_MAC(tc, tmp, src[0][0], src[1][0], tsrc_from(tmp));
      break;
   case TGSI_OPCODE_DP3:
      tc_MUL(tc, tmp, src[0][2], src[1][2]);
      tc_MAC(tc, tmp, src[0][1], src[1][1], tsrc_from(tmp));
      tc_MAC(tc, tmp, src[0][0], src[1][0], tsrc_from(tmp));
      break;
   case TGSI_OPCODE_DPH:
      tc_MAC(tc, tmp, src[0][2], src[1][2], src[1][3]);
      tc_MAC(tc, tmp, src[0][1], src[1][1], tsrc_from(tmp));
      tc_MAC(tc, tmp, src[0][0], src[1][0], tsrc_from(tmp));
      break;
   case TGSI_OPCODE_DP4:
      tc_MUL(tc, tmp, src[0][3], src[1][3]);
      tc_MAC(tc, tmp, src[0][2], src[1][2], tsrc_from(tmp));
      tc_MAC(tc, tmp, src[0][1], src[1][1], tsrc_from(tmp));
      tc_MAC(tc, tmp, src[0][0], src[1][0], tsrc_from(tmp));
      break;
   default:
      assert(!"invalid soa_dot_product() call");
      return;
   }

   for (i = 0; i < 4; i++)
      tc_MOV(tc, dst0[i], tsrc_from(tmp));
}

static void
soa_partial_derivative(struct toy_compiler *tc,
                       const struct tgsi_full_instruction *tgsi_inst,
                       struct toy_dst *dst_,
                       struct toy_src *src_)
{
   if (tgsi_inst->Instruction.Opcode == TGSI_OPCODE_DDX)
      tc_add1(tc, TOY_OPCODE_DDX, dst_[0], src_[0]);
   else
      tc_add1(tc, TOY_OPCODE_DDY, dst_[0], src_[0]);
}

static void
soa_LIT(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst_,
        struct toy_src *src_)
{
   struct toy_inst *inst;
   struct toy_dst dst0[4];
   struct toy_src src0[4];

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src0);

   tc_MOV(tc, dst0[0], tsrc_imm_f(1.0f));
   tc_MOV(tc, dst0[1], src0[0]);
   tc_POW(tc, dst0[2], src0[1], src0[3]);
   tc_MOV(tc, dst0[3], tsrc_imm_f(1.0f));

   /*
    * POW is calculated first because math with pred_ctrl is broken here.
    * But, why?
    */
   tc_CMP(tc, tdst_null(), src0[0], tsrc_imm_f(0.0f), BRW_CONDITIONAL_L);
   inst = tc_MOV(tc, dst0[1], tsrc_imm_f(0.0f));
   inst->pred_ctrl = BRW_PREDICATE_NORMAL;
   inst = tc_MOV(tc, dst0[2], tsrc_imm_f(0.0f));
   inst->pred_ctrl = BRW_PREDICATE_NORMAL;
}

static void
soa_EXP(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst_,
        struct toy_src *src_)
{
   struct toy_dst dst0[4];
   struct toy_src src0[4];

   assert(!"SoA EXP untested");

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src0);

   if (!tdst_is_null(dst0[0])) {
      struct toy_dst tmp = tdst_d(tc_alloc_tmp(tc));

      tc_RNDD(tc, tmp, src0[0]);

      /* construct the floating point number manually */
      tc_ADD(tc, tmp, tsrc_from(tmp), tsrc_imm_d(127));
      tc_SHL(tc, tdst_d(dst0[0]), tsrc_from(tmp), tsrc_imm_d(23));
   }

   tc_FRC(tc, dst0[1], src0[0]);
   tc_EXP(tc, dst0[2], src0[0]);
   tc_MOV(tc, dst0[3], tsrc_imm_f(1.0f));
}

static void
soa_LOG(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst_,
        struct toy_src *src_)
{
   struct toy_dst dst0[4];
   struct toy_src src0[4];

   assert(!"SoA LOG untested");

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src0);

   if (dst_[0].writemask & TOY_WRITEMASK_XY) {
      struct toy_dst tmp = tdst_d(tc_alloc_tmp(tc));

      /* exponent */
      tc_SHR(tc, tmp, tsrc_absolute(tsrc_d(src0[0])), tsrc_imm_d(23));
      tc_ADD(tc, dst0[0], tsrc_from(tmp), tsrc_imm_d(-127));

      /* mantissa  */
      tc_AND(tc, tmp, tsrc_d(src0[0]), tsrc_imm_d((1 << 23) - 1));
      tc_OR(tc, dst0[1], tsrc_from(tmp), tsrc_imm_d(127 << 23));
   }

   tc_LOG(tc, dst0[2], src0[0]);
   tc_MOV(tc, dst0[3], tsrc_imm_f(1.0f));
}

static void
soa_DST(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst_,
        struct toy_src *src_)
{
   struct toy_dst dst0[4];
   struct toy_src src[2][4];

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src[0]);
   tsrc_transpose(src_[1], src[1]);

   tc_MOV(tc, dst0[0], tsrc_imm_f(1.0f));
   tc_MUL(tc, dst0[1], src[0][1], src[1][1]);
   tc_MOV(tc, dst0[2], src[0][2]);
   tc_MOV(tc, dst0[3], src[1][3]);
}

static void
soa_XPD(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst_,
        struct toy_src *src_)
{
   struct toy_dst dst0[4];
   struct toy_src src[2][4];

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src[0]);
   tsrc_transpose(src_[1], src[1]);

   /* dst.x = src0.y * src1.z - src1.y * src0.z */
   tc_MUL(tc, dst0[0], src[0][2], src[1][1]);
   tc_MAC(tc, dst0[0], src[0][1], src[1][2], tsrc_negate(tsrc_from(dst0[0])));

   /* dst.y = src0.z * src1.x - src1.z * src0.x */
   tc_MUL(tc, dst0[1], src[0][0], src[1][2]);
   tc_MAC(tc, dst0[1], src[0][2], src[1][0], tsrc_negate(tsrc_from(dst0[1])));

   /* dst.z = src0.x * src1.y - src1.x * src0.y */
   tc_MUL(tc, dst0[2], src[0][1], src[1][0]);
   tc_MAC(tc, dst0[2], src[0][0], src[1][1], tsrc_negate(tsrc_from(dst0[2])));

   tc_MOV(tc, dst0[3], tsrc_imm_f(1.0f));
}

static void
soa_PK2H(struct toy_compiler *tc,
         const struct tgsi_full_instruction *tgsi_inst,
         struct toy_dst *dst_,
         struct toy_src *src_)
{
   struct toy_dst tmp = tdst_ud(tc_alloc_tmp(tc));
   struct toy_dst dst0[4];
   struct toy_src src0[4];
   int i;

   assert(!"SoA PK2H untested");

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src0);

   tc_SHL(tc, tmp, src0[1], tsrc_imm_ud(16));
   tc_OR(tc, tmp, src0[0], tsrc_from(tmp));

   for (i = 0; i < 4; i++)
      tc_MOV(tc, dst0[i], tsrc_from(tmp));
}

static void
soa_UP2H(struct toy_compiler *tc,
         const struct tgsi_full_instruction *tgsi_inst,
         struct toy_dst *dst_,
         struct toy_src *src_)
{
   struct toy_dst dst0[4];
   struct toy_src src0[4];

   assert(!"SoA UP2H untested");

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src0);

   tc_AND(tc, tdst_ud(dst0[0]), tsrc_ud(src0[0]), tsrc_imm_ud(0xffff));
   tc_SHR(tc, tdst_ud(dst0[1]), tsrc_ud(src0[1]), tsrc_imm_ud(16));
   tc_AND(tc, tdst_ud(dst0[2]), tsrc_ud(src0[2]), tsrc_imm_ud(0xffff));
   tc_SHR(tc, tdst_ud(dst0[3]), tsrc_ud(src0[3]), tsrc_imm_ud(16));

}

static void
soa_SCS(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst_,
        struct toy_src *src_)
{
   struct toy_dst dst0[4];
   struct toy_src src0[4];

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src0);

   tc_add1(tc, TOY_OPCODE_COS, dst0[0], src0[0]);
   tc_add1(tc, TOY_OPCODE_SIN, dst0[1], src0[0]);
   tc_MOV(tc, dst0[2], tsrc_imm_f(0.0f));
   tc_MOV(tc, dst0[3], tsrc_imm_f(1.0f));
}

static void
soa_NRM(struct toy_compiler *tc,
        const struct tgsi_full_instruction *tgsi_inst,
        struct toy_dst *dst_,
        struct toy_src *src_)
{
   const struct toy_dst tmp = tc_alloc_tmp(tc);
   struct toy_dst dst0[4];
   struct toy_src src0[4];

   assert(!"SoA NRM untested");

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src0);

   tc_MUL(tc, tmp, src0[2], src0[2]);
   tc_MAC(tc, tmp, src0[1], src0[1], tsrc_from(tmp));
   tc_MAC(tc, tmp, src0[0], src0[0], tsrc_from(tmp));
   tc_INV(tc, tmp, tsrc_from(tmp));

   tc_MUL(tc, dst0[0], src0[0], tsrc_from(tmp));
   tc_MUL(tc, dst0[1], src0[1], tsrc_from(tmp));
   tc_MUL(tc, dst0[2], src0[2], tsrc_from(tmp));
   tc_MOV(tc, dst0[3], tsrc_imm_f(1.0f));
}

static void
soa_IF(struct toy_compiler *tc,
       const struct tgsi_full_instruction *tgsi_inst,
       struct toy_dst *dst_,
       struct toy_src *src_)
{
   struct toy_src src0[4];

   assert(tsrc_is_swizzle1(src_[0]));
   tsrc_transpose(src_[0], src0);

   tc_IF(tc, tdst_null(), src0[0], tsrc_imm_d(0), BRW_CONDITIONAL_NEQ);
}

static void
soa_NRM4(struct toy_compiler *tc,
         const struct tgsi_full_instruction *tgsi_inst,
         struct toy_dst *dst_,
         struct toy_src *src_)
{
   const struct toy_dst tmp = tc_alloc_tmp(tc);
   struct toy_dst dst0[4];
   struct toy_src src0[4];
   int i;

   assert(!"SoA NRM4 untested");

   tdst_transpose(dst_[0], dst0);
   tsrc_transpose(src_[0], src0);

   tc_MUL(tc, tmp, src0[3], src0[3]);
   tc_MAC(tc, tmp, src0[2], src0[2], tsrc_from(tmp));
   tc_MAC(tc, tmp, src0[1], src0[1], tsrc_from(tmp));
   tc_MAC(tc, tmp, src0[0], src0[0], tsrc_from(tmp));
   tc_INV(tc, tmp, tsrc_from(tmp));

   for (i = 0; i < 4; i++)
      tc_MUL(tc, dst0[i], src0[0], tsrc_from(tmp));
}

static void
soa_unsupported(struct toy_compiler *tc,
                const struct tgsi_full_instruction *tgsi_inst,
                struct toy_dst *dst_,
                struct toy_src *src_)
{
   const struct tgsi_opcode_info *info =
      tgsi_get_opcode_info(tgsi_inst->Instruction.Opcode);

   debug_printf("unsupported TGSI opcode in SoA form: TGSI_OPCODE_%s\n",
         info->mnemonic);
   assert(!"unsupported TGSI instruction in SoA form");
}

static const toy_tgsi_translate soa_translate_table[TGSI_OPCODE_LAST] = {
   [TGSI_OPCODE_ARL]          = soa_per_channel,
   [TGSI_OPCODE_MOV]          = soa_per_channel,
   [TGSI_OPCODE_LIT]          = soa_LIT,
   [TGSI_OPCODE_RCP]          = soa_scalar_replicate,
   [TGSI_OPCODE_RSQ]          = soa_scalar_replicate,
   [TGSI_OPCODE_EXP]          = soa_EXP,
   [TGSI_OPCODE_LOG]          = soa_LOG,
   [TGSI_OPCODE_MUL]          = soa_per_channel,
   [TGSI_OPCODE_ADD]          = soa_per_channel,
   [TGSI_OPCODE_DP3]          = soa_dot_product,
   [TGSI_OPCODE_DP4]          = soa_dot_product,
   [TGSI_OPCODE_DST]          = soa_DST,
   [TGSI_OPCODE_MIN]          = soa_per_channel,
   [TGSI_OPCODE_MAX]          = soa_per_channel,
   [TGSI_OPCODE_SLT]          = soa_per_channel,
   [TGSI_OPCODE_SGE]          = soa_per_channel,
   [TGSI_OPCODE_MAD]          = soa_per_channel,
   [TGSI_OPCODE_SUB]          = soa_per_channel,
   [TGSI_OPCODE_LRP]          = soa_per_channel,
   [TGSI_OPCODE_CND]          = soa_per_channel,
   [20]                       = soa_unsupported,
   [TGSI_OPCODE_DP2A]         = soa_dot_product,
   [22]                       = soa_unsupported,
   [23]                       = soa_unsupported,
   [TGSI_OPCODE_FRC]          = soa_per_channel,
   [TGSI_OPCODE_CLAMP]        = soa_per_channel,
   [TGSI_OPCODE_FLR]          = soa_per_channel,
   [TGSI_OPCODE_ROUND]        = soa_per_channel,
   [TGSI_OPCODE_EX2]          = soa_scalar_replicate,
   [TGSI_OPCODE_LG2]          = soa_scalar_replicate,
   [TGSI_OPCODE_POW]          = soa_scalar_replicate,
   [TGSI_OPCODE_XPD]          = soa_XPD,
   [32]                       = soa_unsupported,
   [TGSI_OPCODE_ABS]          = soa_per_channel,
   [TGSI_OPCODE_RCC]          = soa_unsupported,
   [TGSI_OPCODE_DPH]          = soa_dot_product,
   [TGSI_OPCODE_COS]          = soa_scalar_replicate,
   [TGSI_OPCODE_DDX]          = soa_partial_derivative,
   [TGSI_OPCODE_DDY]          = soa_partial_derivative,
   [TGSI_OPCODE_KILP]         = soa_passthrough,
   [TGSI_OPCODE_PK2H]         = soa_PK2H,
   [TGSI_OPCODE_PK2US]        = soa_unsupported,
   [TGSI_OPCODE_PK4B]         = soa_unsupported,
   [TGSI_OPCODE_PK4UB]        = soa_unsupported,
   [TGSI_OPCODE_RFL]          = soa_unsupported,
   [TGSI_OPCODE_SEQ]          = soa_per_channel,
   [TGSI_OPCODE_SFL]          = soa_per_channel,
   [TGSI_OPCODE_SGT]          = soa_per_channel,
   [TGSI_OPCODE_SIN]          = soa_scalar_replicate,
   [TGSI_OPCODE_SLE]          = soa_per_channel,
   [TGSI_OPCODE_SNE]          = soa_per_channel,
   [TGSI_OPCODE_STR]          = soa_per_channel,
   [TGSI_OPCODE_TEX]          = soa_passthrough,
   [TGSI_OPCODE_TXD]          = soa_passthrough,
   [TGSI_OPCODE_TXP]          = soa_passthrough,
   [TGSI_OPCODE_UP2H]         = soa_UP2H,
   [TGSI_OPCODE_UP2US]        = soa_unsupported,
   [TGSI_OPCODE_UP4B]         = soa_unsupported,
   [TGSI_OPCODE_UP4UB]        = soa_unsupported,
   [TGSI_OPCODE_X2D]          = soa_unsupported,
   [TGSI_OPCODE_ARA]          = soa_unsupported,
   [TGSI_OPCODE_ARR]          = soa_per_channel,
   [TGSI_OPCODE_BRA]          = soa_unsupported,
   [TGSI_OPCODE_CAL]          = soa_unsupported,
   [TGSI_OPCODE_RET]          = soa_unsupported,
   [TGSI_OPCODE_SSG]          = soa_per_channel,
   [TGSI_OPCODE_CMP]          = soa_per_channel,
   [TGSI_OPCODE_SCS]          = soa_SCS,
   [TGSI_OPCODE_TXB]          = soa_passthrough,
   [TGSI_OPCODE_NRM]          = soa_NRM,
   [TGSI_OPCODE_DIV]          = soa_per_channel,
   [TGSI_OPCODE_DP2]          = soa_dot_product,
   [TGSI_OPCODE_TXL]          = soa_passthrough,
   [TGSI_OPCODE_BRK]          = soa_passthrough,
   [TGSI_OPCODE_IF]           = soa_IF,
   [75]                       = soa_unsupported,
   [76]                       = soa_unsupported,
   [TGSI_OPCODE_ELSE]         = soa_passthrough,
   [TGSI_OPCODE_ENDIF]        = soa_passthrough,
   [79]                       = soa_unsupported,
   [80]                       = soa_unsupported,
   [TGSI_OPCODE_PUSHA]        = soa_unsupported,
   [TGSI_OPCODE_POPA]         = soa_unsupported,
   [TGSI_OPCODE_CEIL]         = soa_per_channel,
   [TGSI_OPCODE_I2F]          = soa_per_channel,
   [TGSI_OPCODE_NOT]          = soa_per_channel,
   [TGSI_OPCODE_TRUNC]        = soa_per_channel,
   [TGSI_OPCODE_SHL]          = soa_shift,
   [88]                       = soa_unsupported,
   [TGSI_OPCODE_AND]          = soa_per_channel,
   [TGSI_OPCODE_OR]           = soa_per_channel,
   [TGSI_OPCODE_MOD]          = soa_per_channel,
   [TGSI_OPCODE_XOR]          = soa_per_channel,
   [TGSI_OPCODE_SAD]          = soa_per_channel,
   [TGSI_OPCODE_TXF]          = soa_passthrough,
   [TGSI_OPCODE_TXQ]          = soa_passthrough,
   [TGSI_OPCODE_CONT]         = soa_passthrough,
   [TGSI_OPCODE_EMIT]         = soa_unsupported,
   [TGSI_OPCODE_ENDPRIM]      = soa_unsupported,
   [TGSI_OPCODE_BGNLOOP]      = soa_passthrough,
   [TGSI_OPCODE_BGNSUB]       = soa_unsupported,
   [TGSI_OPCODE_ENDLOOP]      = soa_passthrough,
   [TGSI_OPCODE_ENDSUB]       = soa_unsupported,
   [TGSI_OPCODE_TXQ_LZ]       = soa_passthrough,
   [104]                      = soa_unsupported,
   [105]                      = soa_unsupported,
   [106]                      = soa_unsupported,
   [TGSI_OPCODE_NOP]          = soa_passthrough,
   [108]                      = soa_unsupported,
   [109]                      = soa_unsupported,
   [110]                      = soa_unsupported,
   [111]                      = soa_unsupported,
   [TGSI_OPCODE_NRM4]         = soa_NRM4,
   [TGSI_OPCODE_CALLNZ]       = soa_unsupported,
   [TGSI_OPCODE_IFC]          = soa_unsupported,
   [TGSI_OPCODE_BREAKC]       = soa_unsupported,
   [TGSI_OPCODE_KIL]          = soa_passthrough,
   [TGSI_OPCODE_END]          = soa_passthrough,
   [118]                      = soa_unsupported,
   [TGSI_OPCODE_F2I]          = soa_per_channel,
   [TGSI_OPCODE_IDIV]         = soa_per_channel,
   [TGSI_OPCODE_IMAX]         = soa_per_channel,
   [TGSI_OPCODE_IMIN]         = soa_per_channel,
   [TGSI_OPCODE_INEG]         = soa_per_channel,
   [TGSI_OPCODE_ISGE]         = soa_per_channel,
   [TGSI_OPCODE_ISHR]         = soa_shift,
   [TGSI_OPCODE_ISLT]         = soa_per_channel,
   [TGSI_OPCODE_F2U]          = soa_per_channel,
   [TGSI_OPCODE_U2F]          = soa_per_channel,
   [TGSI_OPCODE_UADD]         = soa_per_channel,
   [TGSI_OPCODE_UDIV]         = soa_per_channel,
   [TGSI_OPCODE_UMAD]         = soa_per_channel,
   [TGSI_OPCODE_UMAX]         = soa_per_channel,
   [TGSI_OPCODE_UMIN]         = soa_per_channel,
   [TGSI_OPCODE_UMOD]         = soa_per_channel,
   [TGSI_OPCODE_UMUL]         = soa_per_channel,
   [TGSI_OPCODE_USEQ]         = soa_per_channel,
   [TGSI_OPCODE_USGE]         = soa_per_channel,
   [TGSI_OPCODE_USHR]         = soa_shift,
   [TGSI_OPCODE_USLT]         = soa_per_channel,
   [TGSI_OPCODE_USNE]         = soa_per_channel,
   [TGSI_OPCODE_SWITCH]       = soa_unsupported,
   [TGSI_OPCODE_CASE]         = soa_unsupported,
   [TGSI_OPCODE_DEFAULT]      = soa_unsupported,
   [TGSI_OPCODE_ENDSWITCH]    = soa_unsupported,
   [TGSI_OPCODE_SAMPLE]       = soa_passthrough,
   [TGSI_OPCODE_SAMPLE_I]     = soa_passthrough,
   [TGSI_OPCODE_SAMPLE_I_MS]  = soa_passthrough,
   [TGSI_OPCODE_SAMPLE_B]     = soa_passthrough,
   [TGSI_OPCODE_SAMPLE_C]     = soa_passthrough,
   [TGSI_OPCODE_SAMPLE_C_LZ]  = soa_passthrough,
   [TGSI_OPCODE_SAMPLE_D]     = soa_passthrough,
   [TGSI_OPCODE_SAMPLE_L]     = soa_passthrough,
   [TGSI_OPCODE_GATHER4]      = soa_passthrough,
   [TGSI_OPCODE_SVIEWINFO]    = soa_passthrough,
   [TGSI_OPCODE_SAMPLE_POS]   = soa_passthrough,
   [TGSI_OPCODE_SAMPLE_INFO]  = soa_passthrough,
   [TGSI_OPCODE_UARL]         = soa_per_channel,
   [TGSI_OPCODE_UCMP]         = soa_per_channel,
   [TGSI_OPCODE_IABS]         = soa_per_channel,
   [TGSI_OPCODE_ISSG]         = soa_per_channel,
   [TGSI_OPCODE_LOAD]         = soa_unsupported,
   [TGSI_OPCODE_STORE]        = soa_unsupported,
   [TGSI_OPCODE_MFENCE]       = soa_unsupported,
   [TGSI_OPCODE_LFENCE]       = soa_unsupported,
   [TGSI_OPCODE_SFENCE]       = soa_unsupported,
   [TGSI_OPCODE_BARRIER]      = soa_unsupported,
   [TGSI_OPCODE_ATOMUADD]     = soa_unsupported,
   [TGSI_OPCODE_ATOMXCHG]     = soa_unsupported,
   [TGSI_OPCODE_ATOMCAS]      = soa_unsupported,
   [TGSI_OPCODE_ATOMAND]      = soa_unsupported,
   [TGSI_OPCODE_ATOMOR]       = soa_unsupported,
   [TGSI_OPCODE_ATOMXOR]      = soa_unsupported,
   [TGSI_OPCODE_ATOMUMIN]     = soa_unsupported,
   [TGSI_OPCODE_ATOMUMAX]     = soa_unsupported,
   [TGSI_OPCODE_ATOMIMIN]     = soa_unsupported,
   [TGSI_OPCODE_ATOMIMAX]     = soa_unsupported,
   [TGSI_OPCODE_TEX2]         = soa_unsupported,
   [TGSI_OPCODE_TXB2]         = soa_unsupported,
   [TGSI_OPCODE_TXL2]         = soa_unsupported,
};

static boolean
ra_dst_is_indirect(const struct tgsi_full_dst_register *d)
{
   return (d->Register.Indirect ||
         (d->Register.Dimension && d->Dimension.Indirect));
}

static int
ra_dst_index(const struct tgsi_full_dst_register *d)
{
   assert(!d->Register.Indirect);
   return d->Register.Index;
}

static int
ra_dst_dimension(const struct tgsi_full_dst_register *d)
{
   if (d->Register.Dimension) {
      assert(!d->Dimension.Indirect);
      return d->Dimension.Index;
   }
   else {
      return 0;
   }
}

static boolean
ra_is_src_indirect(const struct tgsi_full_src_register *s)
{
   return (s->Register.Indirect ||
         (s->Register.Dimension && s->Dimension.Indirect));
}

static int
ra_src_index(const struct tgsi_full_src_register *s)
{
   assert(!s->Register.Indirect);
   return s->Register.Index;
}

static int
ra_src_dimension(const struct tgsi_full_src_register *s)
{
   if (s->Register.Dimension) {
      assert(!s->Dimension.Indirect);
      return s->Dimension.Index;
   }
   else {
      return 0;
   }
}

/**
 * Infer the type of either the sources or the destination.
 */
static enum toy_type
ra_infer_opcode_type(int tgsi_opcode, boolean is_dst)
{
   enum toy_type type;

   if (is_dst) {
      boolean type_valid = FALSE;

      switch (tgsi_opcode) {
      case TGSI_OPCODE_I2F:
      case TGSI_OPCODE_U2F:
      case TGSI_OPCODE_TXF:
      case TGSI_OPCODE_TXQ:
      case TGSI_OPCODE_TXQ_LZ:
      case TGSI_OPCODE_SAMPLE_I:
      case TGSI_OPCODE_SAMPLE_I_MS:
      case TGSI_OPCODE_SAMPLE_POS:
         type = TOY_TYPE_F;
         type_valid = TRUE;
         break;
      case TGSI_OPCODE_ARL:
      case TGSI_OPCODE_ARR:
      case TGSI_OPCODE_F2I:
         type = TOY_TYPE_D;
         type_valid = TRUE;
         break;
      case TGSI_OPCODE_F2U:
         type = TOY_TYPE_UD;
         type_valid = TRUE;
         break;
      default:
         break;
      }

      if (type_valid)
         return type;
   }

   switch (tgsi_opcode) {
   case TGSI_OPCODE_IF:
   case TGSI_OPCODE_I2F:
   case TGSI_OPCODE_NOT:
   case TGSI_OPCODE_AND:
   case TGSI_OPCODE_OR:
   case TGSI_OPCODE_MOD:
   case TGSI_OPCODE_XOR:
   case TGSI_OPCODE_SAD: /* why? */
   case TGSI_OPCODE_TXF:
   case TGSI_OPCODE_TXQ:
   case TGSI_OPCODE_TXQ_LZ:
   case TGSI_OPCODE_IDIV:
   case TGSI_OPCODE_IMAX:
   case TGSI_OPCODE_IMIN:
   case TGSI_OPCODE_INEG:
   case TGSI_OPCODE_ISGE:
   case TGSI_OPCODE_ISHR:
   case TGSI_OPCODE_ISLT:
   case TGSI_OPCODE_UARL: /* why? */
   case TGSI_OPCODE_IABS:
   case TGSI_OPCODE_ISSG:
   case TGSI_OPCODE_ATOMXCHG:
   case TGSI_OPCODE_ATOMCAS:
   case TGSI_OPCODE_ATOMAND:
   case TGSI_OPCODE_ATOMOR:
   case TGSI_OPCODE_ATOMXOR:
   case TGSI_OPCODE_ATOMIMIN:
   case TGSI_OPCODE_ATOMIMAX:
      type = TOY_TYPE_D;
      break;
   case TGSI_OPCODE_SHL:
   case TGSI_OPCODE_U2F:
   case TGSI_OPCODE_UADD:
   case TGSI_OPCODE_UDIV:
   case TGSI_OPCODE_UMAD:
   case TGSI_OPCODE_UMAX:
   case TGSI_OPCODE_UMIN:
   case TGSI_OPCODE_UMOD:
   case TGSI_OPCODE_UMUL:
   case TGSI_OPCODE_USEQ:
   case TGSI_OPCODE_USGE:
   case TGSI_OPCODE_USHR:
   case TGSI_OPCODE_USLT:
   case TGSI_OPCODE_USNE:
   case TGSI_OPCODE_SAMPLE_I:
   case TGSI_OPCODE_SAMPLE_I_MS:
   case TGSI_OPCODE_SVIEWINFO:
   case TGSI_OPCODE_SAMPLE_POS:
   case TGSI_OPCODE_SAMPLE_INFO:
   case TGSI_OPCODE_UCMP:
   case TGSI_OPCODE_LOAD:
   case TGSI_OPCODE_STORE:
   case TGSI_OPCODE_ATOMUADD:
   case TGSI_OPCODE_ATOMUMIN:
   case TGSI_OPCODE_ATOMUMAX:
      type = TOY_TYPE_UD;
      break;
   default:
      type = TOY_TYPE_F;
      break;
   }

   return type;
}

/**
 * Return the type of an operand of the specified instruction.
 */
static enum toy_type
ra_get_type(struct toy_tgsi *tgsi, const struct tgsi_full_instruction *tgsi_inst,
            int operand, boolean is_dst)
{
   enum toy_type type;
   enum tgsi_file_type file;

   /* we need to look at both src and dst for MOV */
   /* XXX it should not be this complex */
   if (tgsi_inst->Instruction.Opcode == TGSI_OPCODE_MOV) {
      const enum tgsi_file_type dst_file = tgsi_inst->Dst[0].Register.File;
      const enum tgsi_file_type src_file = tgsi_inst->Src[0].Register.File;

      if (dst_file == TGSI_FILE_ADDRESS || src_file == TGSI_FILE_ADDRESS) {
         type = TOY_TYPE_D;
      }
      else if (src_file == TGSI_FILE_IMMEDIATE &&
               !tgsi_inst->Src[0].Register.Indirect) {
         const int src_idx = tgsi_inst->Src[0].Register.Index;
         type = tgsi->imm_data.types[src_idx];
      }
      else {
         /* this is the best we can do */
         type = TOY_TYPE_F;
      }

      return type;
   }

   type = ra_infer_opcode_type(tgsi_inst->Instruction.Opcode, is_dst);

   /* fix the type */
   file = (is_dst) ?
      tgsi_inst->Dst[operand].Register.File :
      tgsi_inst->Src[operand].Register.File;
   switch (file) {
   case TGSI_FILE_SAMPLER:
   case TGSI_FILE_RESOURCE:
   case TGSI_FILE_SAMPLER_VIEW:
      type = TOY_TYPE_D;
      break;
   case TGSI_FILE_ADDRESS:
      assert(type == TOY_TYPE_D);
      break;
   default:
      break;
   }

   return type;
}

/**
 * Allocate a VRF register.
 */
static int
ra_alloc_reg(struct toy_tgsi *tgsi, enum tgsi_file_type file)
{
   const int count = (tgsi->aos) ? 1 : 4;
   return tc_alloc_vrf(tgsi->tc, count);
}

/**
 * Construct the key for VRF mapping look-up.
 */
static void *
ra_get_map_key(enum tgsi_file_type file, unsigned dim, unsigned index)
{
   intptr_t key;

   /* this is ugly... */
   assert(file  < 1 << 4);
   assert(dim   < 1 << 12);
   assert(index < 1 << 16);
   key = (file << 28) | (dim << 16) | index;

   return intptr_to_pointer(key);
}

/**
 * Map a TGSI register to a VRF register.
 */
static int
ra_map_reg(struct toy_tgsi *tgsi, enum tgsi_file_type file,
           int dim, int index, boolean *is_new)
{
   void *key, *val;
   intptr_t vrf;

   key = ra_get_map_key(file, dim, index);

   /*
    * because we allocate vrf from 1 and on, val is never NULL as long as the
    * key exists
    */
   val = util_hash_table_get(tgsi->reg_mapping, key);
   if (val) {
      vrf = pointer_to_intptr(val);

      if (is_new)
         *is_new = FALSE;
   }
   else {
      vrf = (intptr_t) ra_alloc_reg(tgsi, file);

      /* add to the mapping */
      val = intptr_to_pointer(vrf);
      util_hash_table_set(tgsi->reg_mapping, key, val);

      if (is_new)
         *is_new = TRUE;
   }

   return (int) vrf;
}

/**
 * Return true if the destination aliases any of the sources.
 */
static boolean
ra_dst_is_aliasing(const struct tgsi_full_instruction *tgsi_inst, int dst_index)
{
   const struct tgsi_full_dst_register *d = &tgsi_inst->Dst[dst_index];
   int i;

   /* we need a scratch register for indirect dst anyway */
   if (ra_dst_is_indirect(d))
      return TRUE;

   for (i = 0; i < tgsi_inst->Instruction.NumSrcRegs; i++) {
      const struct tgsi_full_src_register *s = &tgsi_inst->Src[i];

      if (s->Register.File != d->Register.File)
         continue;

      /*
       * we can go on to check dimension and index respectively, but
       * keep it simple for now
       */
      if (ra_is_src_indirect(s))
         return TRUE;
      if (ra_src_dimension(s) == ra_dst_dimension(d) &&
          ra_src_index(s) == ra_dst_index(d))
         return TRUE;
   }

   return FALSE;
}

/**
 * Return the toy register for a TGSI destination operand.
 */
static struct toy_dst
ra_get_dst(struct toy_tgsi *tgsi,
           const struct tgsi_full_instruction *tgsi_inst, int dst_index,
           boolean *is_scratch)
{
   const struct tgsi_full_dst_register *d = &tgsi_inst->Dst[dst_index];
   boolean need_vrf = FALSE;
   struct toy_dst dst;

   switch (d->Register.File) {
   case TGSI_FILE_NULL:
      dst = tdst_null();
      break;
   case TGSI_FILE_OUTPUT:
   case TGSI_FILE_TEMPORARY:
   case TGSI_FILE_ADDRESS:
   case TGSI_FILE_PREDICATE:
   case TGSI_FILE_TEMPORARY_ARRAY:
      need_vrf = TRUE;
      break;
   default:
      assert(!"unhandled dst file");
      dst = tdst_null();
      break;
   }

   if (need_vrf) {
      /* XXX we do not always need a scratch given the conditions... */
      const boolean need_scratch =
         (ra_dst_is_indirect(d) || ra_dst_is_aliasing(tgsi_inst, dst_index) ||
          tgsi_inst->Instruction.Saturate);
      const enum toy_type type = ra_get_type(tgsi, tgsi_inst, dst_index, TRUE);
      int vrf;

      if (need_scratch) {
         vrf = ra_alloc_reg(tgsi, d->Register.File);
      }
      else {
         vrf = ra_map_reg(tgsi, d->Register.File,
               ra_dst_dimension(d), ra_dst_index(d), NULL);
      }

      if (is_scratch)
         *is_scratch = need_scratch;

      dst = tdst_full(TOY_FILE_VRF, type, TOY_RECT_LINEAR,
            FALSE, 0, d->Register.WriteMask, vrf * TOY_REG_WIDTH);
   }

   return dst;
}

static struct toy_src
ra_get_src_for_vrf(const struct tgsi_full_src_register *s,
                   enum toy_type type, int vrf)
{
   return tsrc_full(TOY_FILE_VRF, type, TOY_RECT_LINEAR,
                    FALSE, 0,
                    s->Register.SwizzleX, s->Register.SwizzleY,
                    s->Register.SwizzleZ, s->Register.SwizzleW,
                    s->Register.Absolute, s->Register.Negate,
                    vrf * TOY_REG_WIDTH);
}

static int
init_tgsi_reg(struct toy_tgsi *tgsi, struct toy_inst *inst,
              enum tgsi_file_type file, int index,
              const struct tgsi_src_register *indirect,
              const struct tgsi_dimension *dimension,
              const struct tgsi_src_register *dim_indirect)
{
   struct toy_src src;
   int num_src = 0;

   /* src[0]: TGSI file */
   src = tsrc_imm_d(file);
   inst->src[num_src++] = src;

   /* src[1]: TGSI dimension */
   if (!dimension) {
      src = tsrc_imm_d(0);
   }
   else if (dim_indirect) {
      const int vrf = ra_map_reg(tgsi, dim_indirect->File, 0,
            dim_indirect->Index, NULL);

      src = tsrc(TOY_FILE_VRF, vrf, 0);
      src = tsrc_swizzle1(tsrc_d(src), indirect->SwizzleX);

      /* XXX abuse indirect fields */
      if (dimension->Index) {
         src.indirect = TRUE;
         src.indirect_subreg = (unsigned) dimension->Index;
      }
   }
   else {
      src = tsrc_imm_d(dimension->Index);
   }

   inst->src[num_src++] = src;

   /* src[2]: TGSI index */
   if (indirect) {
      const int vrf = ra_map_reg(tgsi, indirect->File, 0,
            indirect->Index, NULL);

      src = tsrc(TOY_FILE_VRF, vrf, 0);
      src = tsrc_swizzle1(tsrc_d(src), indirect->SwizzleX);

      /* XXX abuse indirect offset */
      if (index) {
         src.indirect = TRUE;
         src.indirect_subreg = (unsigned) index;
      }
   }
   else {
      src = tsrc_imm_d(index);
   }

   inst->src[num_src++] = src;

   return num_src;
}

static struct toy_src
ra_get_src_indirect(struct toy_tgsi *tgsi,
                    const struct tgsi_full_instruction *tgsi_inst,
                    int src_index)
{
   const struct tgsi_full_src_register *s = &tgsi_inst->Src[src_index];
   boolean need_vrf = FALSE, is_resource = FALSE;
   struct toy_src src;

   switch (s->Register.File) {
   case TGSI_FILE_NULL:
      src = tsrc_null();
      break;
   case TGSI_FILE_SAMPLER:
   case TGSI_FILE_RESOURCE:
   case TGSI_FILE_SAMPLER_VIEW:
      is_resource = TRUE;
      /* fall through */
   case TGSI_FILE_CONSTANT:
   case TGSI_FILE_INPUT:
   case TGSI_FILE_SYSTEM_VALUE:
   case TGSI_FILE_TEMPORARY:
   case TGSI_FILE_ADDRESS:
   case TGSI_FILE_IMMEDIATE:
   case TGSI_FILE_PREDICATE:
   case TGSI_FILE_IMMEDIATE_ARRAY:
   case TGSI_FILE_TEMPORARY_ARRAY:
      need_vrf = TRUE;
      break;
   default:
      assert(!"unhandled src file");
      src = tsrc_null();
      break;
   }

   if (need_vrf) {
      const enum toy_type type = ra_get_type(tgsi, tgsi_inst, src_index, FALSE);
      int vrf;

      if (is_resource) {
         assert(!s->Register.Dimension);
         assert(s->Register.Indirect);

         vrf = ra_map_reg(tgsi, s->Indirect.File, 0, s->Indirect.Index, NULL);
      }
      else {
         vrf = ra_alloc_reg(tgsi, s->Register.File);
      }

      src = ra_get_src_for_vrf(s, type, vrf);

      /* emit indirect fetch */
      if (!is_resource) {
         struct toy_inst *inst;

         inst = tc_add(tgsi->tc);
         inst->opcode = TOY_OPCODE_TGSI_INDIRECT_FETCH;
         inst->dst = tdst_from(src);

         init_tgsi_reg(tgsi, inst, s->Register.File, s->Register.Index,
               (s->Register.Indirect) ? &s->Indirect : NULL,
               (s->Register.Dimension) ? &s->Dimension : NULL,
               (s->Dimension.Indirect) ? &s->DimIndirect : NULL);
      }
   }

   return src;
}

/**
 * Return the toy register for a TGSI source operand.
 */
static struct toy_src
ra_get_src(struct toy_tgsi *tgsi,
           const struct tgsi_full_instruction *tgsi_inst,
           int src_index)
{
   const struct tgsi_full_src_register *s = &tgsi_inst->Src[src_index];
   boolean need_vrf = FALSE;
   struct toy_src src;

   if (ra_is_src_indirect(s))
      return ra_get_src_indirect(tgsi, tgsi_inst, src_index);

   switch (s->Register.File) {
   case TGSI_FILE_NULL:
      src = tsrc_null();
      break;
   case TGSI_FILE_CONSTANT:
   case TGSI_FILE_INPUT:
   case TGSI_FILE_SYSTEM_VALUE:
      need_vrf = TRUE;
      break;
   case TGSI_FILE_TEMPORARY:
   case TGSI_FILE_ADDRESS:
   case TGSI_FILE_PREDICATE:
   case TGSI_FILE_TEMPORARY_ARRAY:
      need_vrf = TRUE;
      break;
   case TGSI_FILE_SAMPLER:
   case TGSI_FILE_RESOURCE:
   case TGSI_FILE_SAMPLER_VIEW:
      assert(!s->Register.Dimension);
      src = tsrc_imm_d(s->Register.Index);
      break;
   case TGSI_FILE_IMMEDIATE:
   case TGSI_FILE_IMMEDIATE_ARRAY:
      {
         const uint32_t *imm;
         enum toy_type imm_type;
         boolean is_scalar;

         imm = toy_tgsi_get_imm(tgsi, s->Register.Index,
               s->Register.File == TGSI_FILE_IMMEDIATE_ARRAY, &imm_type);

         is_scalar =
            (imm[s->Register.SwizzleX] == imm[s->Register.SwizzleY] &&
             imm[s->Register.SwizzleX] == imm[s->Register.SwizzleZ] &&
             imm[s->Register.SwizzleX] == imm[s->Register.SwizzleW]);

         if (is_scalar) {
            const enum toy_type type =
               ra_get_type(tgsi, tgsi_inst, src_index, FALSE);

            /* ignore imm_type */
            src = tsrc_imm_ud(imm[s->Register.SwizzleX]);
            src.type = type;
            src.absolute = s->Register.Absolute;
            src.negate = s->Register.Negate;
         }
         else {
            need_vrf = TRUE;
         }
      }
      break;
   default:
      assert(!"unhandled src file");
      src = tsrc_null();
      break;
   }

   if (need_vrf) {
      const enum toy_type type = ra_get_type(tgsi, tgsi_inst, src_index, FALSE);
      boolean is_new;
      int vrf;

      vrf = ra_map_reg(tgsi, s->Register.File,
            ra_src_dimension(s), ra_src_index(s), &is_new);

      src = ra_get_src_for_vrf(s, type, vrf);
   }

   return src;
}

static void
parse_instruction(struct toy_tgsi *tgsi,
                  const struct tgsi_full_instruction *tgsi_inst)
{
   struct toy_dst dst[TGSI_FULL_MAX_DST_REGISTERS];
   struct toy_src src[TGSI_FULL_MAX_SRC_REGISTERS];
   boolean dst_is_scratch[TGSI_FULL_MAX_DST_REGISTERS];
   toy_tgsi_translate translate;
   int i;

   /* convert TGSI registers to toy registers */
   for (i = 0; i < tgsi_inst->Instruction.NumSrcRegs; i++)
      src[i] = ra_get_src(tgsi, tgsi_inst, i);
   for (i = 0; i < tgsi_inst->Instruction.NumDstRegs; i++)
      dst[i] = ra_get_dst(tgsi, tgsi_inst, i, &dst_is_scratch[i]);

   /* translate the instruction */
   translate = tgsi->translate_table[tgsi_inst->Instruction.Opcode];
   translate(tgsi->tc, tgsi_inst, dst, src);

   /* write the result to the real destinations if needed */
   for (i = 0; i < tgsi_inst->Instruction.NumDstRegs; i++) {
      const struct tgsi_full_dst_register *d = &tgsi_inst->Dst[i];

      if (!dst_is_scratch[i])
         continue;

      /* TODO */
      assert(tgsi_inst->Instruction.Saturate != TGSI_SAT_MINUS_PLUS_ONE);

      tgsi->tc->templ.saturate = tgsi_inst->Instruction.Saturate;

      /* emit indirect store */
      if (ra_dst_is_indirect(d)) {
         struct toy_inst *inst;

         inst = tc_add(tgsi->tc);
         inst->opcode = TOY_OPCODE_TGSI_INDIRECT_STORE;
         inst->dst = dst[i];

         init_tgsi_reg(tgsi, inst, d->Register.File, d->Register.Index,
               (d->Register.Indirect) ? &d->Indirect : NULL,
               (d->Register.Dimension) ? &d->Dimension : NULL,
               (d->Dimension.Indirect) ? &d->DimIndirect : NULL);
      }
      else {
         const enum toy_type type = ra_get_type(tgsi, tgsi_inst, i, TRUE);
         struct toy_dst real_dst;
         int vrf;

         vrf = ra_map_reg(tgsi, d->Register.File,
               ra_dst_dimension(d), ra_dst_index(d), NULL);
         real_dst = tdst_full(TOY_FILE_VRF, type, TOY_RECT_LINEAR,
               FALSE, 0, d->Register.WriteMask, vrf * TOY_REG_WIDTH);

         if (tgsi->aos) {
            tc_MOV(tgsi->tc, real_dst, tsrc_from(dst[i]));
         }
         else {
            struct toy_dst tdst[4];
            struct toy_src tsrc[4];
            int j;

            tdst_transpose(real_dst, tdst);
            tsrc_transpose(tsrc_from(dst[i]), tsrc);

            for (j = 0; j < 4; j++)
               tc_MOV(tgsi->tc, tdst[j], tsrc[j]);
         }
      }

      tgsi->tc->templ.saturate = FALSE;
   }

   switch (tgsi_inst->Instruction.Opcode) {
   case TGSI_OPCODE_KIL:
   case TGSI_OPCODE_KILP:
      tgsi->uses_kill = TRUE;
      break;
   }

   /* remember channels written */
   for (i = 0; i < tgsi_inst->Instruction.NumDstRegs; i++) {
      const struct tgsi_full_dst_register *d = &tgsi_inst->Dst[i];

      if (d->Register.File != TGSI_FILE_OUTPUT)
         continue;
      for (i = 0; i < tgsi->num_outputs; i++) {
         if (tgsi->outputs[i].index == d->Register.Index) {
            tgsi->outputs[i].undefined_mask &= ~d->Register.WriteMask;
            break;
         }
      }
   }
}

static void
decl_add_in(struct toy_tgsi *tgsi, const struct tgsi_full_declaration *decl)
{
   static const struct tgsi_declaration_interp default_interp = {
      TGSI_INTERPOLATE_PERSPECTIVE, FALSE, 0,
   };
   const struct tgsi_declaration_interp *interp =
      (decl->Declaration.Interpolate) ? &decl->Interp: &default_interp;
   int index;

   if (decl->Range.Last >= Elements(tgsi->inputs)) {
      debug_printf("invalid IN\n");
      return;
   }

   for (index = decl->Range.First; index <= decl->Range.Last; index++) {
      const int slot = tgsi->num_inputs++;

      tgsi->inputs[slot].index = index;
      tgsi->inputs[slot].usage_mask = decl->Declaration.UsageMask;
      if (decl->Declaration.Semantic) {
         tgsi->inputs[slot].semantic_name = decl->Semantic.Name;
         tgsi->inputs[slot].semantic_index = decl->Semantic.Index;
      }
      else {
         tgsi->inputs[slot].semantic_name = TGSI_SEMANTIC_GENERIC;
         tgsi->inputs[slot].semantic_index = index;
      }
      tgsi->inputs[slot].interp = interp->Interpolate;
      tgsi->inputs[slot].centroid = interp->Centroid;
   }
}

static void
decl_add_out(struct toy_tgsi *tgsi, const struct tgsi_full_declaration *decl)
{
   int index;

   if (decl->Range.Last >= Elements(tgsi->outputs)) {
      debug_printf("invalid OUT\n");
      return;
   }

   assert(decl->Declaration.Semantic);

   for (index = decl->Range.First; index <= decl->Range.Last; index++) {
      const int slot = tgsi->num_outputs++;

      tgsi->outputs[slot].index = index;
      tgsi->outputs[slot].undefined_mask = TOY_WRITEMASK_XYZW;
      tgsi->outputs[slot].usage_mask = decl->Declaration.UsageMask;
      tgsi->outputs[slot].semantic_name = decl->Semantic.Name;
      tgsi->outputs[slot].semantic_index = decl->Semantic.Index;
   }
}

static void
decl_add_immx(struct toy_tgsi *tgsi, const struct tgsi_full_declaration *decl)
{
   int idx, ch, offset;

   if (decl->Range.Last >= tgsi->imm_data.immx_size) {
      uint32_t (*new_immx)[4];

      new_immx = REALLOC(tgsi->imm_data.immx,
            tgsi->imm_data.immx_size * sizeof(new_immx[0]),
            (decl->Range.Last + 1) * sizeof(new_immx[0]));
      if (!new_immx)
         return;

      tgsi->imm_data.immx = new_immx;
      tgsi->imm_data.immx_size = decl->Range.Last + 1;
   }

   offset = 0;
   for (idx = decl->Range.First; idx <= decl->Range.Last; idx++) {
      for (ch = 0; ch < 4; ch++)
         tgsi->imm_data.immx[idx][ch] = decl->ImmediateData.u[offset++].Uint;
   }
}

/**
 * Emit an instruction to fetch the value of a TGSI register.
 */
static void
fetch_source(struct toy_tgsi *tgsi, enum tgsi_file_type file, int dim, int idx)
{
   struct toy_dst dst;
   int vrf;
   enum toy_opcode opcode;
   enum toy_type type = TOY_TYPE_F;

   switch (file) {
   case TGSI_FILE_INPUT:
      opcode = TOY_OPCODE_TGSI_IN;
      break;
   case TGSI_FILE_CONSTANT:
      opcode = TOY_OPCODE_TGSI_CONST;
      break;
   case TGSI_FILE_SYSTEM_VALUE:
      opcode = TOY_OPCODE_TGSI_SV;
      break;
   case TGSI_FILE_IMMEDIATE_ARRAY:
      opcode = TOY_OPCODE_TGSI_IMMX;
      break;
   case TGSI_FILE_IMMEDIATE:
      opcode = TOY_OPCODE_TGSI_IMM;
      toy_tgsi_get_imm(tgsi, idx, FALSE, &type);
      break;
   default:
      /* no need to fetch */
      return;
      break;
   }

   vrf = ra_map_reg(tgsi, file, dim, idx, NULL);
   dst = tdst(TOY_FILE_VRF, vrf, 0);
   dst = tdst_type(dst, type);

   tc_add2(tgsi->tc, opcode, dst, tsrc_imm_d(dim), tsrc_imm_d(idx));
}

static void
parse_declaration(struct toy_tgsi *tgsi,
                  const struct tgsi_full_declaration *decl)
{
   int i;

   switch (decl->Declaration.File) {
   case TGSI_FILE_INPUT:
      decl_add_in(tgsi, decl);
      break;
   case TGSI_FILE_OUTPUT:
      decl_add_out(tgsi, decl);
      break;
   case TGSI_FILE_IMMEDIATE_ARRAY:
      decl_add_immx(tgsi, decl);
      break;
   case TGSI_FILE_IMMEDIATE:
      /* immediates should be declared with TGSI_TOKEN_TYPE_IMMEDIATE */
      assert(!"unexpected immediate declaration");
      break;
   case TGSI_FILE_NULL:
   case TGSI_FILE_CONSTANT:
   case TGSI_FILE_TEMPORARY:
   case TGSI_FILE_SAMPLER:
   case TGSI_FILE_PREDICATE:
   case TGSI_FILE_SYSTEM_VALUE:
   case TGSI_FILE_TEMPORARY_ARRAY:
   case TGSI_FILE_ADDRESS:
   case TGSI_FILE_RESOURCE:
   case TGSI_FILE_SAMPLER_VIEW:
      /* nothing to do */
      break;
   default:
      assert(!"unhandled TGSI file");
      break;
   }

   for (i = decl->Range.First; i <= decl->Range.Last; i++) {
      const int dim = (decl->Declaration.Dimension) ? decl->Dim.Index2D : 0;
      fetch_source(tgsi, decl->Declaration.File, dim, i);
   }
}

static int
add_imm(struct toy_tgsi *tgsi, enum toy_type type, const uint32_t *buf)
{
   /* reallocate the buffer if necessary */
   if (tgsi->imm_data.cur >= tgsi->imm_data.size) {
      const int cur_size = tgsi->imm_data.size;
      int new_size;
      enum toy_type *new_types;
      uint32_t (*new_buf)[4];

      new_size = (cur_size) ? cur_size << 1 : 16;
      while (new_size <= tgsi->imm_data.cur)
         new_size <<= 1;

      new_buf = REALLOC(tgsi->imm_data.buf,
            cur_size * sizeof(new_buf[0]),
            new_size * sizeof(new_buf[0]));
      new_types = REALLOC(tgsi->imm_data.types,
            cur_size * sizeof(new_types[0]),
            new_size * sizeof(new_types[0]));
      if (!new_buf || !new_types) {
         if (new_buf)
            FREE(new_buf);
         if (new_types)
            FREE(new_types);
         return -1;
      }

      tgsi->imm_data.buf = new_buf;
      tgsi->imm_data.types = new_types;
      tgsi->imm_data.size = new_size;
   }

   tgsi->imm_data.types[tgsi->imm_data.cur] = type;
   memcpy(&tgsi->imm_data.buf[tgsi->imm_data.cur],
         buf, sizeof(tgsi->imm_data.buf[0]));

   return tgsi->imm_data.cur++;
}

static void
parse_immediate(struct toy_tgsi *tgsi, const struct tgsi_full_immediate *imm)
{
   enum toy_type type;
   uint32_t imm_buf[4];
   int idx;

   switch (imm->Immediate.DataType) {
   case TGSI_IMM_FLOAT32:
      type = TOY_TYPE_F;
      imm_buf[0] = fui(imm->u[0].Float);
      imm_buf[1] = fui(imm->u[1].Float);
      imm_buf[2] = fui(imm->u[2].Float);
      imm_buf[3] = fui(imm->u[3].Float);
      break;
   case TGSI_IMM_INT32:
      type = TOY_TYPE_D;
      imm_buf[0] = (uint32_t) imm->u[0].Int;
      imm_buf[1] = (uint32_t) imm->u[1].Int;
      imm_buf[2] = (uint32_t) imm->u[2].Int;
      imm_buf[3] = (uint32_t) imm->u[3].Int;
      break;
   case TGSI_IMM_UINT32:
      type = TOY_TYPE_UD;
      imm_buf[0] = imm->u[0].Uint;
      imm_buf[1] = imm->u[1].Uint;
      imm_buf[2] = imm->u[2].Uint;
      imm_buf[3] = imm->u[3].Uint;
      break;
   default:
      assert(!"unhandled TGSI imm type");
      type = TOY_TYPE_F;
      memset(imm_buf, 0, sizeof(imm_buf));
      break;
   }

   idx = add_imm(tgsi, type, imm_buf);

   fetch_source(tgsi, TGSI_FILE_IMMEDIATE, 0, idx);
}

static void
parse_property(struct toy_tgsi *tgsi, const struct tgsi_full_property *prop)
{
   switch (prop->Property.PropertyName) {
   case TGSI_PROPERTY_VS_PROHIBIT_UCPS:
      tgsi->props.vs_prohibit_ucps = !!prop->u[0].Data;
      break;
   case TGSI_PROPERTY_FS_COORD_ORIGIN:
      tgsi->props.fs_coord_origin = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_FS_COORD_PIXEL_CENTER:
      tgsi->props.fs_coord_pixel_center = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS:
      tgsi->props.fs_color0_writes_all = !!prop->u[0].Data;
      break;
   case TGSI_PROPERTY_FS_DEPTH_LAYOUT:
      tgsi->props.fs_depth_layout = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_GS_INPUT_PRIM:
      tgsi->props.gs_input_prim = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_GS_OUTPUT_PRIM:
      tgsi->props.gs_output_prim = prop->u[0].Data;
      break;
   case TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES:
      tgsi->props.gs_max_output_vertices = prop->u[0].Data;
      break;
   default:
      assert(!"unhandled TGSI property");
      break;
   }
}

static void
parse_token(struct toy_tgsi *tgsi, const union tgsi_full_token *token)
{
   switch (token->Token.Type) {
   case TGSI_TOKEN_TYPE_DECLARATION:
      parse_declaration(tgsi, &token->FullDeclaration);
      break;
   case TGSI_TOKEN_TYPE_IMMEDIATE:
      parse_immediate(tgsi, &token->FullImmediate);
      break;
   case TGSI_TOKEN_TYPE_INSTRUCTION:
      parse_instruction(tgsi, &token->FullInstruction);
      break;
   case TGSI_TOKEN_TYPE_PROPERTY:
      parse_property(tgsi, &token->FullProperty);
      break;
   default:
      assert(!"unhandled TGSI token type");
      break;
   }
}

static enum pipe_error
dump_reg_mapping(void *key, void *val, void *data)
{
   int tgsi_file, tgsi_dim, tgsi_index;
   uint32_t sig, vrf;

   sig = (uint32_t) pointer_to_intptr(key);
   vrf = (uint32_t) pointer_to_intptr(val);

   /* see ra_get_map_key() */
   tgsi_file =  (sig >> 28) & 0xf;
   tgsi_dim =   (sig >> 16) & 0xfff;
   tgsi_index = (sig >> 0)  & 0xffff;

   if (tgsi_dim) {
      debug_printf("  v%d:\t%s[%d][%d]\n", vrf,
            tgsi_file_names[tgsi_file], tgsi_dim, tgsi_index);
   }
   else {
      debug_printf("  v%d:\t%s[%d]\n", vrf,
            tgsi_file_names[tgsi_file], tgsi_index);
   }

   return PIPE_OK;
}

/**
 * Dump the TGSI translator, currently only the register mapping.
 */
void
toy_tgsi_dump(const struct toy_tgsi *tgsi)
{
   util_hash_table_foreach(tgsi->reg_mapping, dump_reg_mapping, NULL);
}

/**
 * Clean up the TGSI translator.
 */
void
toy_tgsi_cleanup(struct toy_tgsi *tgsi)
{
   FREE(tgsi->imm_data.buf);
   FREE(tgsi->imm_data.types);
   FREE(tgsi->imm_data.immx);

   util_hash_table_destroy(tgsi->reg_mapping);
}

static unsigned
reg_mapping_hash(void *key)
{
   return (unsigned) pointer_to_intptr(key);
}

static int
reg_mapping_compare(void *key1, void *key2)
{
   return (key1 != key2);
}

/**
 * Initialize the TGSI translator.
 */
static boolean
init_tgsi(struct toy_tgsi *tgsi, struct toy_compiler *tc, boolean aos)
{
   memset(tgsi, 0, sizeof(*tgsi));

   tgsi->tc = tc;
   tgsi->aos = aos;
   tgsi->translate_table = (aos) ? aos_translate_table : soa_translate_table;

   /* create a mapping of TGSI registers to VRF reigsters */
   tgsi->reg_mapping =
      util_hash_table_create(reg_mapping_hash, reg_mapping_compare);

   return TRUE;
}

/**
 * Translate TGSI tokens into toy instructions.
 */
boolean
toy_compiler_translate_tgsi(struct toy_compiler *tc,
                            const struct tgsi_token *tokens, boolean aos,
                            struct toy_tgsi *tgsi)
{
   struct tgsi_parse_context parse;

   if (!init_tgsi(tgsi, tc, aos))
      return FALSE;

   tgsi_parse_init(&parse, tokens);
   while (!tgsi_parse_end_of_tokens(&parse)) {
      tgsi_parse_token(&parse);
      parse_token(tgsi, &parse.FullToken);
   }
   tgsi_parse_free(&parse);

   return TRUE;
}

/**
 * Map the TGSI register to VRF register.
 */
int
toy_tgsi_get_vrf(const struct toy_tgsi *tgsi,
                 enum tgsi_file_type file, int dimension, int index)
{
   void *key, *val;

   key = ra_get_map_key(file, dimension, index);

   val = util_hash_table_get(tgsi->reg_mapping, key);

   return (val) ? pointer_to_intptr(val) : -1;
}
