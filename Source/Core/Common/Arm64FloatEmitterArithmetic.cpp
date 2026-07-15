// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/Arm64Emitter.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "Common/Align.h"
#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/MathUtil.h"
#include "Common/SmallVector.h"

#ifdef _WIN32
#include <Windows.h>
#endif
#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#endif

namespace Arm64Gen
{
void ARM64FloatEmitter::FCVTS(ARM64Reg Rd, ARM64Reg Rn, RoundingMode round)
{
  EmitConvertScalarToInt(Rd, Rn, round, false);
}

void ARM64FloatEmitter::FCVTU(ARM64Reg Rd, ARM64Reg Rn, RoundingMode round)
{
  EmitConvertScalarToInt(Rd, Rn, round, true);
}

void ARM64FloatEmitter::FMOV(ARM64Reg Rd, ARM64Reg Rn, bool top)
{
  if (IsScalar(Rd) && IsScalar(Rn))
  {
    EmitScalar1Source(0, 0, IsDouble(Rd), 0, Rd, Rn);
  }
  else if (IsGPR(Rd) != IsGPR(Rn))
  {
    const ARM64Reg gpr = IsGPR(Rn) ? Rn : Rd;
    const ARM64Reg fpr = IsGPR(Rn) ? Rd : Rn;

    const int sf = Is64Bit(gpr) ? 1 : 0;
    const int type = Is64Bit(gpr) ? (top ? 2 : 1) : 0;
    const int rmode = top ? 1 : 0;
    const int opcode = IsGPR(Rn) ? 7 : 6;

    ASSERT_MSG(DYNA_REC, !top || IsQuad(fpr), "FMOV: top can only be used with quads");

    // TODO: Should this check be more lenient? Sometimes you do want to do things like
    // read the lower 32 bits of a double
    ASSERT_MSG(DYNA_REC,
               (!Is64Bit(gpr) && IsSingle(fpr)) ||
                   (Is64Bit(gpr) && ((IsDouble(fpr) && !top) || (IsQuad(fpr) && top))),
               "FMOV: Mismatched sizes");

    Write32((sf << 31) | (0x1e << 24) | (type << 22) | (1 << 21) | (rmode << 19) | (opcode << 16) |
            (DecodeReg(Rn) << 5) | DecodeReg(Rd));
  }
  else
  {
    ASSERT_MSG(DYNA_REC, 0, "FMOV: Unsupported case");
  }
}

void ARM64FloatEmitter::FABS(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalar1Source(0, 0, IsDouble(Rd), 1, Rd, Rn);
}

void ARM64FloatEmitter::FNEG(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalar1Source(0, 0, IsDouble(Rd), 2, Rd, Rn);
}

void ARM64FloatEmitter::FSQRT(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalar1Source(0, 0, IsDouble(Rd), 3, Rd, Rn);
}

void ARM64FloatEmitter::FRINTI(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalar1Source(0, 0, IsDouble(Rd), 15, Rd, Rn);
}

void ARM64FloatEmitter::FRECPE(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalar2RegMisc(0, IsDouble(Rd) ? 3 : 2, 0x1D, Rd, Rn);
}

void ARM64FloatEmitter::FRSQRTE(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalar2RegMisc(1, IsDouble(Rd) ? 3 : 2, 0x1D, Rd, Rn);
}

void ARM64FloatEmitter::FADDP(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalarPairwise(1, IsDouble(Rd), 0b01101, Rd, Rn);
}

void ARM64FloatEmitter::FMAXP(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalarPairwise(1, IsDouble(Rd), 0b01111, Rd, Rn);
}

void ARM64FloatEmitter::FMINP(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalarPairwise(1, IsDouble(Rd) ? 3 : 2, 0b01111, Rd, Rn);
}

void ARM64FloatEmitter::FMAXNMP(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalarPairwise(1, IsDouble(Rd), 0b01100, Rd, Rn);
}

void ARM64FloatEmitter::FMINNMP(ARM64Reg Rd, ARM64Reg Rn)
{
  EmitScalarPairwise(1, IsDouble(Rd) ? 3 : 2, 0b01100, Rd, Rn);
}

void ARM64FloatEmitter::ADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, IsDouble(Rd), "Only double registers are supported!");
  EmitScalarThreeSame(0, 3, 0b10000, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitScalar2Source(0, 0, IsDouble(Rd), 2, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMUL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitScalar2Source(0, 0, IsDouble(Rd), 0, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FSUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitScalar2Source(0, 0, IsDouble(Rd), 3, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FDIV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitScalar2Source(0, 0, IsDouble(Rd), 1, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMAX(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitScalar2Source(0, 0, IsDouble(Rd), 4, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMIN(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitScalar2Source(0, 0, IsDouble(Rd), 5, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMAXNM(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitScalar2Source(0, 0, IsDouble(Rd), 6, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMINNM(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitScalar2Source(0, 0, IsDouble(Rd), 7, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FNMUL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitScalar2Source(0, 0, IsDouble(Rd), 8, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra)
{
  EmitScalar3Source(IsDouble(Rd), Rd, Rn, Rm, Ra, 0);
}

void ARM64FloatEmitter::FMSUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra)
{
  EmitScalar3Source(IsDouble(Rd), Rd, Rn, Rm, Ra, 1);
}

void ARM64FloatEmitter::FNMADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra)
{
  EmitScalar3Source(IsDouble(Rd), Rd, Rn, Rm, Ra, 2);
}

void ARM64FloatEmitter::FNMSUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra)
{
  EmitScalar3Source(IsDouble(Rd), Rd, Rn, Rm, Ra, 3);
}

void ARM64FloatEmitter::FMOV(ARM64Reg Rd, uint8_t imm8)
{
  EmitScalarImm(0, 0, 0, 0, Rd, imm8);
}

void ARM64FloatEmitter::ADD(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, MathUtil::IntLog2(size) - 3, 0b10000, Rd, Rn, Rm);
}

void ARM64FloatEmitter::AND(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, 0, 3, Rd, Rn, Rm);
}

void ARM64FloatEmitter::BIC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, 1, 3, Rd, Rn, Rm);
}

void ARM64FloatEmitter::BIF(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, 3, 3, Rd, Rn, Rm);
}

void ARM64FloatEmitter::BIT(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, 2, 3, Rd, Rn, Rm);
}

void ARM64FloatEmitter::BSL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, 1, 3, Rd, Rn, Rm);
}

void ARM64FloatEmitter::DUP(u8 size, ARM64Reg Rd, ARM64Reg Rn, u8 index)
{
  u32 imm5 = 0;

  if (size == 8)
  {
    imm5 = 1;
    imm5 |= index << 1;
  }
  else if (size == 16)
  {
    imm5 = 2;
    imm5 |= index << 2;
  }
  else if (size == 32)
  {
    imm5 = 4;
    imm5 |= index << 3;
  }
  else if (size == 64)
  {
    imm5 = 8;
    imm5 |= index << 4;
  }

  EmitCopy(IsQuad(Rd), 0, imm5, 0, Rd, Rn);
}

void ARM64FloatEmitter::EOR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, 0, 3, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FABS(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, 2 | (size >> 6), 0xF, Rd, Rn);
}

void ARM64FloatEmitter::FADD(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, size >> 6, 0x1A, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMAX(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, size >> 6, 0b11110, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMLA(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, size >> 6, 0x19, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMIN(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, 2 | size >> 6, 0b11110, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FCVTL(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(false, 0, size >> 6, 0x17, Rd, Rn);
}

void ARM64FloatEmitter::FCVTL2(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(true, 0, size >> 6, 0x17, Rd, Rn);
}

void ARM64FloatEmitter::FCVTN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, dest_size >> 5, 0x16, Rd, Rn);
}

void ARM64FloatEmitter::FCVTZS(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, 2 | (size >> 6), 0x1B, Rd, Rn);
}

void ARM64FloatEmitter::FCVTZU(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 1, 2 | (size >> 6), 0x1B, Rd, Rn);
}

void ARM64FloatEmitter::FDIV(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, size >> 6, 0x1F, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMUL(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, size >> 6, 0x1B, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FNEG(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 1, 2 | (size >> 6), 0xF, Rd, Rn);
}

void ARM64FloatEmitter::FRECPE(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, 2 | (size >> 6), 0x1D, Rd, Rn);
}

void ARM64FloatEmitter::FRSQRTE(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 1, 2 | (size >> 6), 0x1D, Rd, Rn);
}

void ARM64FloatEmitter::FSUB(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, 2 | (size >> 6), 0x1A, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMLS(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, 2 | (size >> 6), 0x19, Rd, Rn, Rm);
}

void ARM64FloatEmitter::NOT(ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 1, 0, 5, Rd, Rn);
}

void ARM64FloatEmitter::ORR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, 2, 3, Rd, Rn, Rm);
}

void ARM64FloatEmitter::ORN(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, 3, 3, Rd, Rn, Rm);
}

void ARM64FloatEmitter::REV16(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, MathUtil::IntLog2(size) - 3, 1, Rd, Rn);
}

void ARM64FloatEmitter::REV32(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 1, MathUtil::IntLog2(size) - 3, 0, Rd, Rn);
}

void ARM64FloatEmitter::REV64(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, MathUtil::IntLog2(size) - 3, 0, Rd, Rn);
}

void ARM64FloatEmitter::SCVTF(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, size >> 6, 0x1D, Rd, Rn);
}

void ARM64FloatEmitter::UCVTF(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 1, size >> 6, 0x1D, Rd, Rn);
}

void ARM64FloatEmitter::SCVTF(u8 size, ARM64Reg Rd, ARM64Reg Rn, int scale)
{
  EmitShiftImm(IsQuad(Rd), 0, size * 2 - scale, 0x1C, Rd, Rn);
}

void ARM64FloatEmitter::UCVTF(u8 size, ARM64Reg Rd, ARM64Reg Rn, int scale)
{
  EmitShiftImm(IsQuad(Rd), 1, size * 2 - scale, 0x1C, Rd, Rn);
}

void ARM64FloatEmitter::SQXTN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(false, 0, MathUtil::IntLog2(dest_size) - 3, 0b10100, Rd, Rn);
}

void ARM64FloatEmitter::SQXTN2(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(true, 0, MathUtil::IntLog2(dest_size) - 3, 0b10100, Rd, Rn);
}

void ARM64FloatEmitter::UQXTN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(false, 1, MathUtil::IntLog2(dest_size) - 3, 0b10100, Rd, Rn);
}

void ARM64FloatEmitter::UQXTN2(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(true, 1, MathUtil::IntLog2(dest_size) - 3, 0b10100, Rd, Rn);
}

void ARM64FloatEmitter::XTN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(false, 0, MathUtil::IntLog2(dest_size) - 3, 0b10010, Rd, Rn);
}

void ARM64FloatEmitter::XTN2(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(true, 0, MathUtil::IntLog2(dest_size) - 3, 0b10010, Rd, Rn);
}

void ARM64FloatEmitter::DUP(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  u32 imm5 = 0;

  if (size == 8)
    imm5 = 1;
  else if (size == 16)
    imm5 = 2;
  else if (size == 32)
    imm5 = 4;
  else if (size == 64)
    imm5 = 8;

  EmitCopy(IsQuad(Rd), 0, imm5, 1, Rd, Rn);
}

void ARM64FloatEmitter::INS(u8 size, ARM64Reg Rd, u8 index, ARM64Reg Rn)
{
  u32 imm5 = 0;

  if (size == 8)
  {
    imm5 = 1;
    imm5 |= index << 1;
  }
  else if (size == 16)
  {
    imm5 = 2;
    imm5 |= index << 2;
  }
  else if (size == 32)
  {
    imm5 = 4;
    imm5 |= index << 3;
  }
  else if (size == 64)
  {
    imm5 = 8;
    imm5 |= index << 4;
  }

  EmitCopy(1, 0, imm5, 3, Rd, Rn);
}

void ARM64FloatEmitter::INS(u8 size, ARM64Reg Rd, u8 index1, ARM64Reg Rn, u8 index2)
{
  u32 imm5 = 0, imm4 = 0;

  if (size == 8)
  {
    imm5 = 1;
    imm5 |= index1 << 1;
    imm4 = index2;
  }
  else if (size == 16)
  {
    imm5 = 2;
    imm5 |= index1 << 2;
    imm4 = index2 << 1;
  }
  else if (size == 32)
  {
    imm5 = 4;
    imm5 |= index1 << 3;
    imm4 = index2 << 2;
  }
  else if (size == 64)
  {
    imm5 = 8;
    imm5 |= index1 << 4;
    imm4 = index2 << 3;
  }

  EmitCopy(1, 1, imm5, imm4, Rd, Rn);
}

void ARM64FloatEmitter::UMOV(u8 size, ARM64Reg Rd, ARM64Reg Rn, u8 index)
{
  bool b64Bit = Is64Bit(Rd);
  ASSERT_MSG(DYNA_REC, Rd < ARM64Reg::SP, "Destination must be a GPR!");
  ASSERT_MSG(DYNA_REC, !(b64Bit && size != 64),
             "Must have a size of 64 when destination is 64bit!");
  u32 imm5 = 0;

  if (size == 8)
  {
    imm5 = 1;
    imm5 |= index << 1;
  }
  else if (size == 16)
  {
    imm5 = 2;
    imm5 |= index << 2;
  }
  else if (size == 32)
  {
    imm5 = 4;
    imm5 |= index << 3;
  }
  else if (size == 64)
  {
    imm5 = 8;
    imm5 |= index << 4;
  }

  EmitCopy(b64Bit, 0, imm5, 7, Rd, Rn);
}

void ARM64FloatEmitter::SMOV(u8 size, ARM64Reg Rd, ARM64Reg Rn, u8 index)
{
  bool b64Bit = Is64Bit(Rd);
  ASSERT_MSG(DYNA_REC, Rd < ARM64Reg::SP, "Destination must be a GPR!");
  ASSERT_MSG(DYNA_REC, size != 64, "SMOV doesn't support 64bit destination. Use UMOV!");
  u32 imm5 = 0;

  if (size == 8)
  {
    imm5 = 1;
    imm5 |= index << 1;
  }
  else if (size == 16)
  {
    imm5 = 2;
    imm5 |= index << 2;
  }
  else if (size == 32)
  {
    imm5 = 4;
    imm5 |= index << 3;
  }

  EmitCopy(b64Bit, 0, imm5, 5, Rd, Rn);
}

void ARM64FloatEmitter::FCVT(u8 size_to, u8 size_from, ARM64Reg Rd, ARM64Reg Rn)
{
  u32 dst_encoding = 0;
  u32 src_encoding = 0;

  if (size_to == 16)
    dst_encoding = 3;
  else if (size_to == 32)
    dst_encoding = 0;
  else if (size_to == 64)
    dst_encoding = 1;

  if (size_from == 16)
    src_encoding = 3;
  else if (size_from == 32)
    src_encoding = 0;
  else if (size_from == 64)
    src_encoding = 1;

  Emit1Source(0, 0, src_encoding, 4 | dst_encoding, Rd, Rn);
}

void ARM64FloatEmitter::SCVTF(ARM64Reg Rd, ARM64Reg Rn)
{
  if (IsScalar(Rn))
  {
    // Source is in FP register (like destination!). We must use a vector encoding.
    bool sign = false;
    int sz = IsDouble(Rn);
    Write32((0x5e << 24) | (sign << 29) | (sz << 22) | (0x876 << 10) | (DecodeReg(Rn) << 5) |
            DecodeReg(Rd));
  }
  else
  {
    bool sf = Is64Bit(Rn);
    u32 type = 0;
    if (IsDouble(Rd))
      type = 1;
    EmitConversion(sf, 0, type, 0, 2, Rd, Rn);
  }
}

void ARM64FloatEmitter::UCVTF(ARM64Reg Rd, ARM64Reg Rn)
{
  if (IsScalar(Rn))
  {
    // Source is in FP register (like destination!). We must use a vector encoding.
    bool sign = true;
    int sz = IsDouble(Rn);
    Write32((0x5e << 24) | (sign << 29) | (sz << 22) | (0x876 << 10) | (DecodeReg(Rn) << 5) |
            DecodeReg(Rd));
  }
  else
  {
    bool sf = Is64Bit(Rn);
    u32 type = 0;
    if (IsDouble(Rd))
      type = 1;

    EmitConversion(sf, 0, type, 0, 3, Rd, Rn);
  }
}

void ARM64FloatEmitter::SCVTF(ARM64Reg Rd, ARM64Reg Rn, int scale)
{
  bool sf = Is64Bit(Rn);
  u32 type = 0;
  if (IsDouble(Rd))
    type = 1;

  EmitConversion2(sf, 0, false, type, 0, 2, 64 - scale, Rd, Rn);
}

void ARM64FloatEmitter::UCVTF(ARM64Reg Rd, ARM64Reg Rn, int scale)
{
  bool sf = Is64Bit(Rn);
  u32 type = 0;
  if (IsDouble(Rd))
    type = 1;

  EmitConversion2(sf, 0, false, type, 0, 3, 64 - scale, Rd, Rn);
}

void ARM64FloatEmitter::CMEQ(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, MathUtil::IntLog2(size) - 3, 0x11, Rd, Rn, Rm);
}

void ARM64FloatEmitter::CMEQ(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, MathUtil::IntLog2(size) - 3, 0x9, Rd, Rn);
}

void ARM64FloatEmitter::CMGE(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, MathUtil::IntLog2(size) - 3, 0x7, Rd, Rn, Rm);
}

void ARM64FloatEmitter::CMGE(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 1, MathUtil::IntLog2(size) - 3, 0x8, Rd, Rn);
}

void ARM64FloatEmitter::CMGT(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, MathUtil::IntLog2(size) - 3, 0x6, Rd, Rn, Rm);
}

void ARM64FloatEmitter::CMGT(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, MathUtil::IntLog2(size) - 3, 0x8, Rd, Rn);
}

void ARM64FloatEmitter::CMHI(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, MathUtil::IntLog2(size) - 3, 0x6, Rd, Rn, Rm);
}

void ARM64FloatEmitter::CMHS(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, MathUtil::IntLog2(size) - 3, 0x7, Rd, Rn, Rm);
}

void ARM64FloatEmitter::CMLE(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 1, MathUtil::IntLog2(size) - 3, 0x9, Rd, Rn);
}

void ARM64FloatEmitter::CMLT(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, MathUtil::IntLog2(size) - 3, 0xA, Rd, Rn);
}

void ARM64FloatEmitter::CMTST(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, MathUtil::IntLog2(size) - 3, 0x11, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FCMP(ARM64Reg Rn, ARM64Reg Rm)
{
  EmitCompare(0, 0, 0, 0, Rn, Rm);
}

void ARM64FloatEmitter::FCMP(ARM64Reg Rn)
{
  EmitCompare(0, 0, 0, 8, Rn, (ARM64Reg)0);
}

void ARM64FloatEmitter::FCMPE(ARM64Reg Rn, ARM64Reg Rm)
{
  EmitCompare(0, 0, 0, 0x10, Rn, Rm);
}

void ARM64FloatEmitter::FCMPE(ARM64Reg Rn)
{
  EmitCompare(0, 0, 0, 0x18, Rn, (ARM64Reg)0);
}

void ARM64FloatEmitter::FCMEQ(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(0, size >> 6, 0x1C, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FCMEQ(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, 2 | (size >> 6), 0xD, Rd, Rn);
}

void ARM64FloatEmitter::FCMGE(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, size >> 6, 0x1C, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FCMGE(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 1, 2 | (size >> 6), 0x0C, Rd, Rn);
}

void ARM64FloatEmitter::FCMGT(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, 2 | (size >> 6), 0x1C, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FCMGT(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, 2 | (size >> 6), 0x0C, Rd, Rn);
}

void ARM64FloatEmitter::FCMLE(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 1, 2 | (size >> 6), 0xD, Rd, Rn);
}

void ARM64FloatEmitter::FCMLT(u8 size, ARM64Reg Rd, ARM64Reg Rn)
{
  Emit2RegMisc(IsQuad(Rd), 0, 2 | (size >> 6), 0xE, Rd, Rn);
}

void ARM64FloatEmitter::FACGE(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, size >> 6, 0x1D, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FACGT(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitThreeSame(1, 2 | (size >> 6), 0x1D, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FCSEL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond)
{
  EmitCondSelect(0, 0, cond, Rd, Rn, Rm);
}

void ARM64FloatEmitter::UZP1(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitPermute(size, 0b001, Rd, Rn, Rm);
}

void ARM64FloatEmitter::TRN1(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitPermute(size, 0b010, Rd, Rn, Rm);
}

void ARM64FloatEmitter::ZIP1(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitPermute(size, 0b011, Rd, Rn, Rm);
}

void ARM64FloatEmitter::UZP2(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitPermute(size, 0b101, Rd, Rn, Rm);
}

void ARM64FloatEmitter::TRN2(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitPermute(size, 0b110, Rd, Rn, Rm);
}

void ARM64FloatEmitter::ZIP2(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitPermute(size, 0b111, Rd, Rn, Rm);
}

void ARM64FloatEmitter::EXT(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, u32 index)
{
  EmitExtract(index, 0, Rd, Rn, Rm);
}

void ARM64FloatEmitter::SHL(ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  constexpr size_t src_size = 64;
  ASSERT_MSG(DYNA_REC, IsDouble(Rd), "Only double registers are supported!");
  ASSERT_MSG(DYNA_REC, shift < src_size, "Shift amount must be less than the element size! {} {}",
             shift, src_size);
  EmitScalarShiftImm(0, src_size | shift, 0b01010, Rd, Rn);
}

void ARM64FloatEmitter::URSHR(ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  constexpr size_t src_size = 64;
  ASSERT_MSG(DYNA_REC, IsDouble(Rd), "Only double registers are supported!");
  ASSERT_MSG(DYNA_REC, shift < src_size, "Shift amount must be less than the element size! {} {}",
             shift, src_size);
  EmitScalarShiftImm(1, src_size * 2 - shift, 0b00100, Rd, Rn);
}

void ARM64FloatEmitter::SSHLL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  SSHLL(src_size, Rd, Rn, shift, false);
}

void ARM64FloatEmitter::SSHLL2(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  SSHLL(src_size, Rd, Rn, shift, true);
}

void ARM64FloatEmitter::SHRN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  SHRN(dest_size, Rd, Rn, shift, false);
}

void ARM64FloatEmitter::SHRN2(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  SHRN(dest_size, Rd, Rn, shift, true);
}

void ARM64FloatEmitter::USHLL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  USHLL(src_size, Rd, Rn, shift, false);
}

void ARM64FloatEmitter::USHLL2(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  USHLL(src_size, Rd, Rn, shift, true);
}

void ARM64FloatEmitter::SXTL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn)
{
  SXTL(src_size, Rd, Rn, false);
}

void ARM64FloatEmitter::SXTL2(u8 src_size, ARM64Reg Rd, ARM64Reg Rn)
{
  SXTL(src_size, Rd, Rn, true);
}

void ARM64FloatEmitter::UXTL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn)
{
  UXTL(src_size, Rd, Rn, false);
}

void ARM64FloatEmitter::UXTL2(u8 src_size, ARM64Reg Rd, ARM64Reg Rn)
{
  UXTL(src_size, Rd, Rn, true);
}

void ARM64FloatEmitter::SHL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  ASSERT_MSG(DYNA_REC, shift < src_size, "Shift amount must be less than the element size! {} {}",
             shift, src_size);
  EmitShiftImm(IsQuad(Rd), 0, src_size | shift, 0b01010, Rd, Rn);
}

void ARM64FloatEmitter::SSHLL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift, bool upper)
{
  ASSERT_MSG(DYNA_REC, shift < src_size, "Shift amount must be less than the element size! {} {}",
             shift, src_size);
  EmitShiftImm(upper, 0, src_size | shift, 0b10100, Rd, Rn);
}

void ARM64FloatEmitter::SSHR(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  ASSERT_MSG(DYNA_REC, shift < src_size, "Shift amount must be less than the element size! {} {}",
             shift, src_size);
  EmitShiftImm(IsQuad(Rd), 0, src_size * 2 - shift, 0b00000, Rd, Rn);
}

void ARM64FloatEmitter::URSHR(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift)
{
  ASSERT_MSG(DYNA_REC, shift < src_size, "Shift amount must be less than the element size! {} {}",
             shift, src_size);
  EmitShiftImm(IsQuad(Rd), 1, src_size * 2 - shift, 0b00100, Rd, Rn);
}

void ARM64FloatEmitter::USHLL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift, bool upper)
{
  ASSERT_MSG(DYNA_REC, shift < src_size, "Shift amount must be less than the element size! {} {}",
             shift, src_size);
  EmitShiftImm(upper, 1, src_size | shift, 0b10100, Rd, Rn);
}

void ARM64FloatEmitter::SHRN(u8 dest_size, ARM64Reg Rd, ARM64Reg Rn, u32 shift, bool upper)
{
  ASSERT_MSG(DYNA_REC, shift < dest_size, "Shift amount must be less than the element size! {} {}",
             shift, dest_size);
  EmitShiftImm(upper, 1, dest_size * 2 - shift, 0b10000, Rd, Rn);
}

void ARM64FloatEmitter::SXTL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, bool upper)
{
  SSHLL(src_size, Rd, Rn, 0, upper);
}

void ARM64FloatEmitter::UXTL(u8 src_size, ARM64Reg Rd, ARM64Reg Rn, bool upper)
{
  USHLL(src_size, Rd, Rn, 0, upper);
}

void ARM64FloatEmitter::FMUL(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, u8 index)
{
  ASSERT_MSG(DYNA_REC, size == 32 || size == 64, "Only 32bit or 64bit sizes are supported! {}",
             size);

  bool L = false;
  bool H = false;
  if (size == 32)
  {
    L = index & 1;
    H = (index >> 1) & 1;
  }
  else if (size == 64)
  {
    H = index == 1;
  }

  EmitVectorxElement(0, 2 | (size >> 6), L, 0x9, H, Rd, Rn, Rm);
}

void ARM64FloatEmitter::FMLA(u8 size, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, u8 index)
{
  ASSERT_MSG(DYNA_REC, size == 32 || size == 64, "Only 32bit or 64bit sizes are supported! {}",
             size);

  bool L = false;
  bool H = false;
  if (size == 32)
  {
    L = index & 1;
    H = (index >> 1) & 1;
  }
  else if (size == 64)
  {
    H = index == 1;
  }

  EmitVectorxElement(0, 2 | (size >> 6), L, 1, H, Rd, Rn, Rm);
}

void ARM64FloatEmitter::MOVI(u8 size, ARM64Reg Rd, u64 imm, u8 shift)
{
  bool Q = IsQuad(Rd);
  u8 cmode = 0;
  u8 op = 0;
  u8 abcdefgh = imm & 0xFF;
  if (size == 8)
  {
    ASSERT_MSG(DYNA_REC, shift == 0, "size8 doesn't support shift! ({})", shift);
    ASSERT_MSG(DYNA_REC, !(imm & ~0xFFULL), "size8 only supports 8bit values! ({})", imm);
  }
  else if (size == 16)
  {
    ASSERT_MSG(DYNA_REC, shift == 0 || shift == 8, "size16 only supports shift of 0 or 8! ({})",
               shift);
    ASSERT_MSG(DYNA_REC, !(imm & ~0xFFULL), "size16 only supports 8bit values! ({})", imm);

    if (shift == 8)
      cmode |= 2;
  }
  else if (size == 32)
  {
    ASSERT_MSG(DYNA_REC, shift == 0 || shift == 8 || shift == 16 || shift == 24,
               "size32 only supports shift of 0, 8, 16, or 24! ({})", shift);
    // XXX: Implement support for MOVI - shifting ones variant
    ASSERT_MSG(DYNA_REC, !(imm & ~0xFFULL), "size32 only supports 8bit values! ({})", imm);
    switch (shift)
    {
    case 8:
      cmode |= 2;
      break;
    case 16:
      cmode |= 4;
      break;
    case 24:
      cmode |= 6;
      break;
    default:
      break;
    }
  }
  else  // 64
  {
    ASSERT_MSG(DYNA_REC, shift == 0, "size64 doesn't support shift! ({})", shift);

    op = 1;
    cmode = 0xE;
    abcdefgh = 0;
    for (int i = 0; i < 8; ++i)
    {
      u8 tmp = (imm >> (i << 3)) & 0xFF;
      ASSERT_MSG(DYNA_REC, tmp == 0xFF || tmp == 0, "size64 Invalid immediate! ({} -> {})", imm,
                 tmp);
      if (tmp == 0xFF)
        abcdefgh |= (1 << i);
    }
  }
  EncodeModImm(Q, op, cmode, 0, Rd, abcdefgh);
}

void ARM64FloatEmitter::ORR(u8 size, ARM64Reg Rd, u8 imm, u8 shift)
{
  ORR_BIC(size, Rd, imm, shift, 0);
}

void ARM64FloatEmitter::BIC(u8 size, ARM64Reg Rd, u8 imm, u8 shift)
{
  ORR_BIC(size, Rd, imm, shift, 1);
}

void ARM64FloatEmitter::MOVI2F(ARM64Reg Rd, float value, ARM64Reg scratch, bool negate)
{
  ASSERT_MSG(DYNA_REC, !IsDouble(Rd), "MOVI2F does not yet support double precision");

  if (value == 0.0f)
  {
    FMOV(Rd, IsDouble(Rd) ? ARM64Reg::ZR : ARM64Reg::WZR);
    if (negate)
      FNEG(Rd, Rd);
    // TODO: There are some other values we could generate with the float-imm instruction, like
    // 1.0...
  }
  else if (const auto imm = FPImm8FromFloat(value))
  {
    FMOV(Rd, *imm);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, scratch != ARM64Reg::INVALID_REG,
               "Failed to find a way to generate FP immediate {} without scratch", value);
    if (negate)
      value = -value;

    const u32 ival = std::bit_cast<u32>(value);
    m_emit->MOVI2R(scratch, ival);
    FMOV(Rd, scratch);
  }
}

void ARM64FloatEmitter::MOVI2FDUP(ARM64Reg Rd, float value, ARM64Reg scratch)
{
  // TODO: Make it work with more element sizes
  // TODO: Optimize - there are shorter solution for many values
  ARM64Reg s = ARM64Reg::S0 + DecodeReg(Rd);
  MOVI2F(s, value, scratch);
  DUP(32, Rd, Rd, 0);
}
}  // namespace Arm64Gen
