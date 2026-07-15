// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include "Common/Arm64EmitterTypes.h"

namespace Arm64Gen
{
class ARM64XEmitter;

float FPImm8ToFloat(u8 bits);
std::optional<u8> FPImm8FromFloat(float value);

class ARM64FloatEmitter
{
public:
  ARM64FloatEmitter(ARM64XEmitter* emit) : m_emit(emit) {}
  void LDR(u8 size, IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void STR(u8 size, IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);

  // Loadstore unscaled
  void LDUR(u8 size, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void STUR(u8 size, ARM64Reg Rt, ARM64Reg Rn, s32 imm);

  // Loadstore single structure
  void LD1(u8 size, ARM64Reg Rt, u8 index, ARM64Reg Rn);
  void LD1(u8 size, ARM64Reg Rt, u8 index, ARM64Reg Rn, ARM64Reg Rm);
  void LD1R(u8 size, ARM64Reg Rt, ARM64Reg Rn);
  void LD2R(u8 size, ARM64Reg Rt, ARM64Reg Rn);
  void LD1R(u8 size, ARM64Reg Rt, ARM64Reg Rn, ARM64Reg Rm);
  void LD2R(u8 size, ARM64Reg Rt, ARM64Reg Rn, ARM64Reg Rm);
  void ST1(u8 size, ARM64Reg Rt, u8 index, ARM64Reg Rn);
  void ST1(u8 size, ARM64Reg Rt, u8 index, ARM64Reg Rn, ARM64Reg Rm);

  // Loadstore multiple structure
  void LD1(u8 size, u8 count, ARM64Reg Rt, ARM64Reg Rn);
  void LD1(u8 size, u8 count, IndexType type, ARM64Reg Rt, ARM64Reg Rn, ARM64Reg Rm = ARM64Reg::SP);
  void ST1(u8 size, u8 count, ARM64Reg Rt, ARM64Reg Rn);
  void ST1(u8 size, u8 count, IndexType type, ARM64Reg Rt, ARM64Reg Rn, ARM64Reg Rm = ARM64Reg::SP);

  // Loadstore paired
  void LDP(u8 size, IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, s32 imm);
  void STP(u8 size, IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, s32 imm);

  // Loadstore register offset
  void STR(u8 size, ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void LDR(u8 size, ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);

  // Scalar - 1 Source
  void FABS(ARM64Reg Rd, ARM64Reg Rn);
  void FNEG(ARM64Reg Rd, ARM64Reg Rn);
  void FSQRT(ARM64Reg Rd, ARM64Reg Rn);
  void FRINTI(ARM64Reg Rd, ARM64Reg Rn);
  void FMOV(ARM64Reg Rd, ARM64Reg Rn, bool top = false);  // Also generalized move between GPR/FP
  void FRECPE(ARM64Reg Rd, ARM64Reg Rn);
  void FRSQRTE(ARM64Reg Rd, ARM64Reg Rn);

  // Scalar - pairwise
  void FADDP(ARM64Reg Rd, ARM64Reg Rn);
  void FMAXP(ARM64Reg Rd, ARM64Reg Rn);
  void FMINP(ARM64Reg Rd, ARM64Reg Rn);
  void FMAXNMP(ARM64Reg Rd, ARM64Reg Rn);
  void FMINNMP(ARM64Reg Rd, ARM64Reg Rn);

  // Scalar - 2 Source
  void ADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FMUL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FSUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FDIV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FMAX(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FMIN(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FMAXNM(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FMINNM(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FNMUL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);

  // Scalar - 3 Source. Note - the accumulator is last on ARM!
  void FMADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);
  void FMSUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);
  void FNMADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);
  void FNMSUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);

  // Scalar floating point immediate
  void FMOV(ARM64Reg Rd, uint8_t imm8);

  // Vector
  void ADD(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void AND(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void BIC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void BIF(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void BIT(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void BSL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void DUP(u8 size, ARM64Reg Rd, ARM64Reg Rn, u8 index);
  void EOR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FABS(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FADD(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FMAX(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FMLA(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FMLS(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FMIN(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FCVTL(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FCVTL2(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FCVTN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn);
  void FCVTN2(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn);
  void FCVTZS(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FCVTZU(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FDIV(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FMUL(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FNEG(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FRECPE(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FRSQRTE(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FSUB(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void NOT(ARM64Reg Rd, ARM64Reg Rn);
  void ORR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void ORN(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void MOV(ARM64Reg Rd, ARM64Reg Rn) { ORR(Rd, Rn, Rn); }
  void REV16(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void REV32(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void REV64(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void SCVTF(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void UCVTF(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void SCVTF(u8 size, ARM64Reg Rd, ARM64Reg Rn, int scale);
  void UCVTF(u8 size, ARM64Reg Rd, ARM64Reg Rn, int scale);
  void SQXTN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn);
  void SQXTN2(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn);
  void UQXTN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn);
  void UQXTN2(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn);
  void XTN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn);
  void XTN2(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn);

  // Move
  void DUP(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void INS(u8 size, ARM64Reg Rd, u8 index, ARM64Reg Rn);
  void INS(u8 size, ARM64Reg Rd, u8 index1, ARM64Reg Rn, u8 index2);
  void UMOV(u8 size, ARM64Reg Rd, ARM64Reg Rn, u8 index);
  void SMOV(u8 size, ARM64Reg Rd, ARM64Reg Rn, u8 index);

  // One source
  void FCVT(u8 size_to, u8 size_from, ARM64Reg Rd, ARM64Reg Rn);

  // Scalar convert float to int, in a lot of variants.
  // Note that the scalar version of this operation has two encodings, one that goes to an integer
  // register
  // and one that outputs to a scalar fp register.
  void FCVTS(ARM64Reg Rd, ARM64Reg Rn, RoundingMode round);
  void FCVTU(ARM64Reg Rd, ARM64Reg Rn, RoundingMode round);

  // Scalar convert int to float. No rounding mode specifier necessary.
  void SCVTF(ARM64Reg Rd, ARM64Reg Rn);
  void UCVTF(ARM64Reg Rd, ARM64Reg Rn);

  // Scalar fixed point to float. scale is the number of fractional bits.
  void SCVTF(ARM64Reg Rd, ARM64Reg Rn, int scale);
  void UCVTF(ARM64Reg Rd, ARM64Reg Rn, int scale);

  // Comparison
  void CMEQ(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CMEQ(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void CMGE(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CMGE(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void CMGT(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CMGT(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void CMHI(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CMHS(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CMLE(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void CMLT(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void CMTST(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);

  // Float comparison
  void FCMP(ARM64Reg Rn, ARM64Reg Rm);
  void FCMP(ARM64Reg Rn);
  void FCMPE(ARM64Reg Rn, ARM64Reg Rm);
  void FCMPE(ARM64Reg Rn);
  void FCMEQ(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FCMEQ(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FCMGE(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FCMGE(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FCMGT(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FCMGT(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FCMLE(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FCMLT(u8 size, ARM64Reg Rd, ARM64Reg Rn);
  void FACGE(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void FACGT(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);

  // Conditional select
  void FCSEL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond);

  // Permute
  void UZP1(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void TRN1(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void ZIP1(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void UZP2(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void TRN2(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void ZIP2(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);

  // Extract
  void EXT(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, u32 index);

  // Scalar shift by immediate
  void SHL(ARM64Reg Rd, ARM64Reg Rn, u32 shift);
  void URSHR(ARM64Reg Rd, ARM64Reg Rn, u32 shift);

  // Vector shift by immediate
  void SHL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift);
  void SSHLL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift);
  void SSHLL2(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift);
  void SSHR(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift);
  void URSHR(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift);
  void USHLL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift);
  void USHLL2(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift);
  void SHRN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift);
  void SHRN2(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift);
  void SXTL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn);
  void SXTL2(u8 src_size, ARM64Reg Rd, ARM64Reg Rn);
  void UXTL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn);
  void UXTL2(u8 src_size, ARM64Reg Rd, ARM64Reg Rn);

  // vector x indexed element
  void FMUL(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, u8 index);
  void FMLA(u8 esize, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, u8 index);

  // Modified Immediate
  void MOVI(u8 size, ARM64Reg Rd, u64 imm, u8 shift = 0);
  void ORR(u8 size, ARM64Reg Rd, u8 imm, u8 shift = 0);
  void BIC(u8 size, ARM64Reg Rd, u8 imm, u8 shift = 0);

  void MOVI2F(ARM64Reg Rd, float value, ARM64Reg scratch = ARM64Reg::INVALID_REG,
              bool negate = false);
  void MOVI2FDUP(ARM64Reg Rd, float value, ARM64Reg scratch = ARM64Reg::INVALID_REG);

  // ABI related
  void ABI_PushRegisters(BitSet32 registers, ARM64Reg tmp = ARM64Reg::INVALID_REG);
  void ABI_PopRegisters(BitSet32 registers, ARM64Reg tmp = ARM64Reg::INVALID_REG);

private:
  ARM64XEmitter* m_emit;
  inline void Write32(u32 value) { m_emit->Write32(value); }
  // Emitting functions
  void EmitLoadStoreImmediate(u8 size, u32 opc, IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void EmitScalar2Source(bool M, bool S, u32 type, u32 opcode, ARM64Reg Rd, ARM64Reg Rn,
                         ARM64Reg Rm);
  void EmitScalarThreeSame(bool U, u32 size, u32 opcode, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void EmitThreeSame(bool U, u32 size, u32 opcode, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void EmitCopy(bool Q, u32 op, u32 imm5, u32 imm4, ARM64Reg Rd, ARM64Reg Rn);
  void EmitScalar2RegMisc(bool U, u32 size, u32 opcode, ARM64Reg Rd, ARM64Reg Rn);
  void EmitScalarPairwise(bool U, u32 size, u32 opcode, ARM64Reg Rd, ARM64Reg Rn);
  void Emit2RegMisc(bool Q, bool U, u32 size, u32 opcode, ARM64Reg Rd, ARM64Reg Rn);
  void EmitLoadStoreSingleStructure(bool L, bool R, u32 opcode, bool S, u32 size, ARM64Reg Rt,
                                    ARM64Reg Rn);
  void EmitLoadStoreSingleStructure(bool L, bool R, u32 opcode, bool S, u32 size, ARM64Reg Rt,
                                    ARM64Reg Rn, ARM64Reg Rm);
  void Emit1Source(bool M, bool S, u32 type, u32 opcode, ARM64Reg Rd, ARM64Reg Rn);
  void EmitConversion(bool sf, bool S, u32 type, u32 rmode, u32 opcode, ARM64Reg Rd, ARM64Reg Rn);
  void EmitConversion2(bool sf, bool S, bool direction, u32 type, u32 rmode, u32 opcode, int scale,
                       ARM64Reg Rd, ARM64Reg Rn);
  void EmitCompare(bool M, bool S, u32 op, u32 opcode2, ARM64Reg Rn, ARM64Reg Rm);
  void EmitCondSelect(bool M, bool S, CCFlags cond, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void EmitPermute(u32 size, u32 op, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void EmitExtract(u32 imm4, u32 op, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void EmitScalarImm(bool M, bool S, u32 type, u32 imm5, ARM64Reg Rd, u32 imm8);
  void EmitShiftImm(bool Q, bool U, u32 imm, u32 opcode, ARM64Reg Rd, ARM64Reg Rn);
  void EmitScalarShiftImm(bool U, u32 imm, u32 opcode, ARM64Reg Rd, ARM64Reg Rn);
  void EmitLoadStoreMultipleStructure(u32 size, bool L, u32 opcode, ARM64Reg Rt, ARM64Reg Rn);
  void EmitLoadStoreMultipleStructurePost(u32 size, bool L, u32 opcode, ARM64Reg Rt, ARM64Reg Rn,
                                          ARM64Reg Rm);
  void EmitScalar1Source(bool M, bool S, u32 type, u32 opcode, ARM64Reg Rd, ARM64Reg Rn);
  void EmitVectorxElement(bool U, u32 size, bool L, u32 opcode, bool H, ARM64Reg Rd, ARM64Reg Rn,
                          ARM64Reg Rm);
  void EmitLoadStoreUnscaled(u32 size, u32 op, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void EmitConvertScalarToInt(ARM64Reg Rd, ARM64Reg Rn, RoundingMode round, bool sign);
  void EmitScalar3Source(bool isDouble, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra,
                         int opcode);
  void EncodeLoadStorePair(u32 size, bool load, IndexType type, ARM64Reg Rt, ARM64Reg Rt2,
                           ARM64Reg Rn, s32 imm);
  void EncodeLoadStoreRegisterOffset(u32 size, bool load, ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void EncodeModImm(bool Q, u8 op, u8 cmode, u8 o2, ARM64Reg Rd, u8 abcdefgh);

  void ORR_BIC(u8 size, ARM64Reg Rd, u8 imm, u8 shift, u8 op);

  void SSHLL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift, bool upper);
  void USHLL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift, bool upper);
  void SHRN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift, bool upper);
  void SXTL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, bool upper);
  void UXTL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, bool upper);
};

}  // namespace Arm64Gen
