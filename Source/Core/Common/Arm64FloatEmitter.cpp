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
float FPImm8ToFloat(u8 bits)
{
  const u32 sign = bits >> 7;
  const u32 bit6 = (bits >> 6) & 1;
  const u32 exp = ((!bit6) << 7) | (0x7C * bit6) | ((bits >> 4) & 3);
  const u32 mantissa = (bits & 0xF) << 19;
  const u32 f = (sign << 31) | (exp << 23) | mantissa;

  return std::bit_cast<float>(f);
}

std::optional<u8> FPImm8FromFloat(float value)
{
  const u32 f = std::bit_cast<u32>(value);
  const u32 mantissa4 = (f & 0x7FFFFF) >> 19;
  const u32 exponent = (f >> 23) & 0xFF;
  const u32 sign = f >> 31;

  if ((exponent >> 7) == ((exponent >> 6) & 1))
    return std::nullopt;

  const u8 imm8 = (sign << 7) | ((!(exponent >> 7)) << 6) | ((exponent & 3) << 4) | mantissa4;
  const float new_float = FPImm8ToFloat(imm8);

  if (new_float != value)
    return std::nullopt;

  return imm8;
}

// Float Emitter
void ARM64FloatEmitter::EmitLoadStoreImmediate(u8 size, u32 opc, IndexType type, ARM64Reg Rt,
                                               ARM64Reg Rn, s32 imm)
{
  u32 encoded_size = 0;
  u32 encoded_imm = 0;

  if (size == 8)
    encoded_size = 0;
  else if (size == 16)
    encoded_size = 1;
  else if (size == 32)
    encoded_size = 2;
  else if (size == 64)
    encoded_size = 3;
  else if (size == 128)
    encoded_size = 0;

  if (type == IndexType::Unsigned)
  {
    ASSERT_MSG(DYNA_REC, imm >= 0, "(IndexType::Unsigned) immediate offset must be positive! ({})",
               imm);
    if (size == 16)
    {
      ASSERT_MSG(DYNA_REC, (imm & 0x1) == 0, "16-bit load/store must use aligned offset: {}", imm);
      imm >>= 1;
    }
    else if (size == 32)
    {
      ASSERT_MSG(DYNA_REC, (imm & 0x3) == 0, "32-bit load/store must use aligned offset: {}", imm);
      imm >>= 2;
    }
    else if (size == 64)
    {
      ASSERT_MSG(DYNA_REC, (imm & 0x7) == 0, "64-bit load/store must use aligned offset: {}", imm);
      imm >>= 3;
    }
    else if (size == 128)
    {
      ASSERT_MSG(DYNA_REC, (imm & 0xf) == 0, "128-bit load/store must use aligned offset: {}", imm);
      imm >>= 4;
    }
    ASSERT_MSG(DYNA_REC, imm <= 0xFFF, "Immediate value is too big: {}", imm);
    encoded_imm = (imm & 0xFFF);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, !(imm < -256 || imm > 255),
               "immediate offset must be within range of -256 to 256! {}", imm);
    encoded_imm = (imm & 0x1FF) << 2;
    if (type == IndexType::Post)
      encoded_imm |= 1;
    else
      encoded_imm |= 3;
  }

  Write32((encoded_size << 30) | (0xF << 26) | (type == IndexType::Unsigned ? (1 << 24) : 0) |
          (size == 128 ? (1 << 23) : 0) | (opc << 22) | (encoded_imm << 10) | (DecodeReg(Rn) << 5) |
          DecodeReg(Rt));
}

void ARM64FloatEmitter::EmitScalar2Source(bool M, bool S, u32 type, u32 opcode, ARM64Reg Rd,
                                          ARM64Reg Rn, ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, !IsQuad(Rd), "Only double and single registers are supported!");

  Write32((M << 31) | (S << 29) | (0b11110001 << 21) | (type << 22) | (DecodeReg(Rm) << 16) |
          (opcode << 12) | (1 << 11) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitScalarThreeSame(bool U, u32 size, u32 opcode, ARM64Reg Rd, ARM64Reg Rn,
                                            ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, !IsQuad(Rd), "Only double and single registers are supported!");

  Write32((1 << 30) | (U << 29) | (0b11110001 << 21) | (size << 22) | (DecodeReg(Rm) << 16) |
          (opcode << 11) | (1 << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitThreeSame(bool U, u32 size, u32 opcode, ARM64Reg Rd, ARM64Reg Rn,
                                      ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, !IsSingle(Rd), "Singles are not supported!");
  bool quad = IsQuad(Rd);

  Write32((quad << 30) | (U << 29) | (0b1110001 << 21) | (size << 22) | (DecodeReg(Rm) << 16) |
          (opcode << 11) | (1 << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitCopy(bool Q, u32 op, u32 imm5, u32 imm4, ARM64Reg Rd, ARM64Reg Rn)
{
  Write32((Q << 30) | (op << 29) | (0b111 << 25) | (imm5 << 16) | (imm4 << 11) | (1 << 10) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitScalar2RegMisc(bool U, u32 size, u32 opcode, ARM64Reg Rd, ARM64Reg Rn)
{
  Write32((1 << 30) | (U << 29) | (0b11110001 << 21) | (size << 22) | (opcode << 12) | (1 << 11) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitScalarPairwise(bool U, u32 size, u32 opcode, ARM64Reg Rd, ARM64Reg Rn)
{
  Write32((1 << 30) | (U << 29) | (0b111100011 << 20) | (size << 22) | (opcode << 12) | (1 << 11) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::Emit2RegMisc(bool Q, bool U, u32 size, u32 opcode, ARM64Reg Rd, ARM64Reg Rn)
{
  ASSERT_MSG(DYNA_REC, !IsSingle(Rd), "Singles are not supported!");

  Write32((Q << 30) | (U << 29) | (0b1110001 << 21) | (size << 22) | (opcode << 12) | (1 << 11) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitLoadStoreSingleStructure(bool L, bool R, u32 opcode, bool S, u32 size,
                                                     ARM64Reg Rt, ARM64Reg Rn)
{
  ASSERT_MSG(DYNA_REC, !IsSingle(Rt), "Singles are not supported!");
  bool quad = IsQuad(Rt);

  Write32((quad << 30) | (0b1101 << 24) | (L << 22) | (R << 21) | (opcode << 13) | (S << 12) |
          (size << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rt));
}

void ARM64FloatEmitter::EmitLoadStoreSingleStructure(bool L, bool R, u32 opcode, bool S, u32 size,
                                                     ARM64Reg Rt, ARM64Reg Rn, ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, !IsSingle(Rt), "Singles are not supported!");
  bool quad = IsQuad(Rt);

  Write32((quad << 30) | (0x1B << 23) | (L << 22) | (R << 21) | (DecodeReg(Rm) << 16) |
          (opcode << 13) | (S << 12) | (size << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rt));
}

void ARM64FloatEmitter::Emit1Source(bool M, bool S, u32 type, u32 opcode, ARM64Reg Rd, ARM64Reg Rn)
{
  ASSERT_MSG(DYNA_REC, !IsQuad(Rd), "Vector is not supported!");

  Write32((M << 31) | (S << 29) | (0xF1 << 21) | (type << 22) | (opcode << 15) | (1 << 14) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitConversion(bool sf, bool S, u32 type, u32 rmode, u32 opcode,
                                       ARM64Reg Rd, ARM64Reg Rn)
{
  ASSERT_MSG(DYNA_REC, Rn <= ARM64Reg::SP, "Only GPRs are supported as source!");

  Write32((sf << 31) | (S << 29) | (0xF1 << 21) | (type << 22) | (rmode << 19) | (opcode << 16) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitConvertScalarToInt(ARM64Reg Rd, ARM64Reg Rn, RoundingMode round,
                                               bool sign)
{
  DEBUG_ASSERT_MSG(DYNA_REC, IsScalar(Rn), "fcvts: Rn must be floating point");
  if (IsGPR(Rd))
  {
    // Use the encoding that transfers the result to a GPR.
    const bool sf = Is64Bit(Rd);
    const int type = IsDouble(Rn) ? 1 : 0;
    int opcode = (sign ? 1 : 0);
    int rmode = 0;
    switch (round)
    {
    case RoundingMode::A:
      rmode = 0;
      opcode |= 4;
      break;
    case RoundingMode::P:
      rmode = 1;
      break;
    case RoundingMode::M:
      rmode = 2;
      break;
    case RoundingMode::Z:
      rmode = 3;
      break;
    case RoundingMode::N:
      rmode = 0;
      break;
    }
    EmitConversion2(sf, 0, true, type, rmode, opcode, 0, Rd, Rn);
  }
  else
  {
    // Use the encoding (vector, single) that keeps the result in the fp register.
    int sz = IsDouble(Rn);
    int opcode = 0;
    switch (round)
    {
    case RoundingMode::A:
      opcode = 0x1C;
      break;
    case RoundingMode::N:
      opcode = 0x1A;
      break;
    case RoundingMode::M:
      opcode = 0x1B;
      break;
    case RoundingMode::P:
      opcode = 0x1A;
      sz |= 2;
      break;
    case RoundingMode::Z:
      opcode = 0x1B;
      sz |= 2;
      break;
    }
    Write32((0x5E << 24) | (sign << 29) | (sz << 22) | (1 << 21) | (opcode << 12) | (2 << 10) |
            (DecodeReg(Rn) << 5) | DecodeReg(Rd));
  }
}

void ARM64FloatEmitter::EmitConversion2(bool sf, bool S, bool direction, u32 type, u32 rmode,
                                        u32 opcode, int scale, ARM64Reg Rd, ARM64Reg Rn)
{
  Write32((sf << 31) | (S << 29) | (0xF0 << 21) | (direction << 21) | (type << 22) | (rmode << 19) |
          (opcode << 16) | (scale << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitCompare(bool M, bool S, u32 op, u32 opcode2, ARM64Reg Rn, ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, !IsQuad(Rn), "Vector is not supported!");
  bool is_double = IsDouble(Rn);

  Write32((M << 31) | (S << 29) | (0xF1 << 21) | (is_double << 22) | (DecodeReg(Rm) << 16) |
          (op << 14) | (1 << 13) | (DecodeReg(Rn) << 5) | opcode2);
}

void ARM64FloatEmitter::EmitCondSelect(bool M, bool S, CCFlags cond, ARM64Reg Rd, ARM64Reg Rn,
                                       ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, !IsQuad(Rd), "Vector is not supported!");
  bool is_double = IsDouble(Rd);

  Write32((M << 31) | (S << 29) | (0xF1 << 21) | (is_double << 22) | (DecodeReg(Rm) << 16) |
          (cond << 12) | (3 << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitPermute(u32 size, u32 op, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, !IsSingle(Rd), "Singles are not supported!");

  bool quad = IsQuad(Rd);

  u32 encoded_size = 0;
  if (size == 16)
    encoded_size = 1;
  else if (size == 32)
    encoded_size = 2;
  else if (size == 64)
    encoded_size = 3;

  Write32((quad << 30) | (7 << 25) | (encoded_size << 22) | (DecodeReg(Rm) << 16) | (op << 12) |
          (1 << 11) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitExtract(u32 imm4, u32 op, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, !IsSingle(Rd), "Singles are not supported!");

  bool quad = IsQuad(Rd);

  Write32((quad << 30) | (23 << 25) | (op << 22) | (DecodeReg(Rm) << 16) | (imm4 << 11) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitScalarImm(bool M, bool S, u32 type, u32 imm5, ARM64Reg Rd, u32 imm8)
{
  ASSERT_MSG(DYNA_REC, !IsQuad(Rd), "Vector is not supported!");

  bool is_double = !IsSingle(Rd);

  Write32((M << 31) | (S << 29) | (0xF1 << 21) | (is_double << 22) | (type << 22) | (imm8 << 13) |
          (1 << 12) | (imm5 << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitShiftImm(bool Q, bool U, u32 imm, u32 opcode, ARM64Reg Rd, ARM64Reg Rn)
{
  ASSERT_MSG(DYNA_REC, (imm & 0b1111000) != 0, "Can't have zero immh");

  Write32((Q << 30) | (U << 29) | (0xF << 24) | (imm << 16) | (opcode << 11) | (1 << 10) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitScalarShiftImm(bool U, u32 imm, u32 opcode, ARM64Reg Rd, ARM64Reg Rn)
{
  Write32((1 << 30) | (U << 29) | (0x3E << 23) | (imm << 16) | (opcode << 11) | (1 << 10) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitLoadStoreMultipleStructure(u32 size, bool L, u32 opcode, ARM64Reg Rt,
                                                       ARM64Reg Rn)
{
  bool quad = IsQuad(Rt);
  u32 encoded_size = 0;

  if (size == 16)
    encoded_size = 1;
  else if (size == 32)
    encoded_size = 2;
  else if (size == 64)
    encoded_size = 3;

  Write32((quad << 30) | (3 << 26) | (L << 22) | (opcode << 12) | (encoded_size << 10) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rt));
}

void ARM64FloatEmitter::EmitLoadStoreMultipleStructurePost(u32 size, bool L, u32 opcode,
                                                           ARM64Reg Rt, ARM64Reg Rn, ARM64Reg Rm)
{
  bool quad = IsQuad(Rt);
  u32 encoded_size = 0;

  if (size == 16)
    encoded_size = 1;
  else if (size == 32)
    encoded_size = 2;
  else if (size == 64)
    encoded_size = 3;

  Write32((quad << 30) | (0b11001 << 23) | (L << 22) | (DecodeReg(Rm) << 16) | (opcode << 12) |
          (encoded_size << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rt));
}

void ARM64FloatEmitter::EmitScalar1Source(bool M, bool S, u32 type, u32 opcode, ARM64Reg Rd,
                                          ARM64Reg Rn)
{
  ASSERT_MSG(DYNA_REC, !IsQuad(Rd), "Vector is not supported!");

  Write32((M << 31) | (S << 29) | (0xF1 << 21) | (type << 22) | (opcode << 15) | (1 << 14) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitVectorxElement(bool U, u32 size, bool L, u32 opcode, bool H,
                                           ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  bool quad = IsQuad(Rd);

  Write32((quad << 30) | (U << 29) | (0xF << 24) | (size << 22) | (L << 21) |
          (DecodeReg(Rm) << 16) | (opcode << 12) | (H << 11) | (DecodeReg(Rn) << 5) |
          DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitLoadStoreUnscaled(u32 size, u32 op, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  ASSERT_MSG(DYNA_REC, !(imm < -256 || imm > 255), "received too large offset: {}", imm);

  Write32((size << 30) | (0xF << 26) | (op << 22) | ((imm & 0x1FF) << 12) | (DecodeReg(Rn) << 5) |
          DecodeReg(Rt));
}

void ARM64FloatEmitter::EncodeModImm(bool Q, u8 op, u8 cmode, u8 o2, ARM64Reg Rd, u8 abcdefgh)
{
  union
  {
    u8 hex;
    struct
    {
      unsigned defgh : 5;
      unsigned abc : 3;
    };
  } v;
  v.hex = abcdefgh;
  Write32((Q << 30) | (op << 29) | (0xF << 24) | (v.abc << 16) | (cmode << 12) | (o2 << 11) |
          (1 << 10) | (v.defgh << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::EmitScalar3Source(bool isDouble, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm,
                                          ARM64Reg Ra, int opcode)
{
  int type = isDouble ? 1 : 0;
  int o1 = opcode >> 1;
  int o0 = opcode & 1;
  m_emit->Write32((0x1F << 24) | (type << 22) | (o1 << 21) | (DecodeReg(Rm) << 16) | (o0 << 15) |
                  (DecodeReg(Ra) << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64FloatEmitter::ORR_BIC(u8 size, ARM64Reg Rd, u8 imm, u8 shift, u8 op)
{
  bool Q = IsQuad(Rd);
  u8 cmode = 1;
  if (size == 16)
  {
    ASSERT_MSG(DYNA_REC, shift == 0 || shift == 8, "size16 only supports shift of 0 or 8! {}",
               shift);

    if (shift == 8)
      cmode |= 2;
  }
  else if (size == 32)
  {
    ASSERT_MSG(DYNA_REC, shift == 0 || shift == 8 || shift == 16 || shift == 24,
               "size32 only supports shift of 0, 8, 16, or 24! ({})", shift);
    // XXX: Implement support for MOVI - shifting ones variant
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
  else
  {
    ASSERT_MSG(DYNA_REC, false, "Only size of 16 or 32 is supported! ({})", size);
  }
  EncodeModImm(Q, op, cmode, 0, Rd, imm);
}

}  // namespace Arm64Gen
