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
namespace
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
}  // Anonymous namespace

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

void ARM64FloatEmitter::FCVTS(ARM64Reg Rd, ARM64Reg Rn, RoundingMode round)
{
  EmitConvertScalarToInt(Rd, Rn, round, false);
}

void ARM64FloatEmitter::FCVTU(ARM64Reg Rd, ARM64Reg Rn, RoundingMode round)
{
  EmitConvertScalarToInt(Rd, Rn, round, true);
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

void ARM64FloatEmitter::EncodeLoadStorePair(u32 size, bool load, IndexType type, ARM64Reg Rt,
                                            ARM64Reg Rt2, ARM64Reg Rn, s32 imm)
{
  u32 type_encode = 0;
  u32 opc = 0;

  switch (type)
  {
  case IndexType::Signed:
    type_encode = 0b010;
    break;
  case IndexType::Post:
    type_encode = 0b001;
    break;
  case IndexType::Pre:
    type_encode = 0b011;
    break;
  case IndexType::Unsigned:
    ASSERT_MSG(DYNA_REC, false, "IndexType::Unsigned is unsupported!");
    break;
  }

  if (size == 128)
  {
    ASSERT_MSG(DYNA_REC, !(imm & 0xF), "Invalid offset {:#x}! (size {})", imm, size);
    opc = 2;
    imm >>= 4;
  }
  else if (size == 64)
  {
    ASSERT_MSG(DYNA_REC, !(imm & 0x7), "Invalid offset {:#x}! (size {})", imm, size);
    opc = 1;
    imm >>= 3;
  }
  else if (size == 32)
  {
    ASSERT_MSG(DYNA_REC, !(imm & 0x3), "Invalid offset {:#x}! (size {})", imm, size);
    opc = 0;
    imm >>= 2;
  }

  ASSERT_MSG(DYNA_REC, imm >= -64 && imm < 64, "imm too large for load/store pair! {}", imm);

  Write32((opc << 30) | (0b1011 << 26) | (type_encode << 23) | (load << 22) | ((imm & 0x7F) << 15) |
          (DecodeReg(Rt2) << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rt));
}

void ARM64FloatEmitter::EncodeLoadStoreRegisterOffset(u32 size, bool load, ARM64Reg Rt, ARM64Reg Rn,
                                                      ArithOption Rm)
{
  ASSERT_MSG(DYNA_REC, Rm.IsExtended(), "Must contain an extended reg as Rm!");

  u32 encoded_size = 0;
  u32 encoded_op = 0;

  if (size == 8)
  {
    encoded_size = 0;
    encoded_op = 0;
  }
  else if (size == 16)
  {
    encoded_size = 1;
    encoded_op = 0;
  }
  else if (size == 32)
  {
    encoded_size = 2;
    encoded_op = 0;
  }
  else if (size == 64)
  {
    encoded_size = 3;
    encoded_op = 0;
  }
  else if (size == 128)
  {
    encoded_size = 0;
    encoded_op = 2;
  }

  if (load)
    encoded_op |= 1;

  const int decoded_Rm = DecodeReg(Rm.GetReg());

  Write32((encoded_size << 30) | (encoded_op << 22) | (0b111100001 << 21) | (decoded_Rm << 16) |
          Rm.GetData() | (1 << 11) | (DecodeReg(Rn) << 5) | DecodeReg(Rt));
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

void ARM64FloatEmitter::LDR(u8 size, IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EmitLoadStoreImmediate(size, 1, type, Rt, Rn, imm);
}
void ARM64FloatEmitter::STR(u8 size, IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EmitLoadStoreImmediate(size, 0, type, Rt, Rn, imm);
}

// Loadstore unscaled
void ARM64FloatEmitter::LDUR(u8 size, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  u32 encoded_size = 0;
  u32 encoded_op = 0;

  if (size == 8)
  {
    encoded_size = 0;
    encoded_op = 1;
  }
  else if (size == 16)
  {
    encoded_size = 1;
    encoded_op = 1;
  }
  else if (size == 32)
  {
    encoded_size = 2;
    encoded_op = 1;
  }
  else if (size == 64)
  {
    encoded_size = 3;
    encoded_op = 1;
  }
  else if (size == 128)
  {
    encoded_size = 0;
    encoded_op = 3;
  }

  EmitLoadStoreUnscaled(encoded_size, encoded_op, Rt, Rn, imm);
}
void ARM64FloatEmitter::STUR(u8 size, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  u32 encoded_size = 0;
  u32 encoded_op = 0;

  if (size == 8)
  {
    encoded_size = 0;
    encoded_op = 0;
  }
  else if (size == 16)
  {
    encoded_size = 1;
    encoded_op = 0;
  }
  else if (size == 32)
  {
    encoded_size = 2;
    encoded_op = 0;
  }
  else if (size == 64)
  {
    encoded_size = 3;
    encoded_op = 0;
  }
  else if (size == 128)
  {
    encoded_size = 0;
    encoded_op = 2;
  }

  EmitLoadStoreUnscaled(encoded_size, encoded_op, Rt, Rn, imm);
}

// Loadstore single structure
void ARM64FloatEmitter::LD1(u8 size, ARM64Reg Rt, u8 index, ARM64Reg Rn)
{
  bool S = 0;
  u32 opcode = 0;
  u32 encoded_size = 0;
  ARM64Reg encoded_reg = ARM64Reg::INVALID_REG;

  if (size == 8)
  {
    S = (index & 4) != 0;
    opcode = 0;
    encoded_size = index & 3;
    if (index & 8)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 16)
  {
    S = (index & 2) != 0;
    opcode = 2;
    encoded_size = (index & 1) << 1;
    if (index & 4)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 32)
  {
    S = (index & 1) != 0;
    opcode = 4;
    encoded_size = 0;
    if (index & 2)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 64)
  {
    S = 0;
    opcode = 4;
    encoded_size = 1;
    if (index == 1)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }

  EmitLoadStoreSingleStructure(1, 0, opcode, S, encoded_size, encoded_reg, Rn);
}

void ARM64FloatEmitter::LD1(u8 size, ARM64Reg Rt, u8 index, ARM64Reg Rn, ARM64Reg Rm)
{
  bool S = 0;
  u32 opcode = 0;
  u32 encoded_size = 0;
  ARM64Reg encoded_reg = ARM64Reg::INVALID_REG;

  if (size == 8)
  {
    S = (index & 4) != 0;
    opcode = 0;
    encoded_size = index & 3;
    if (index & 8)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 16)
  {
    S = (index & 2) != 0;
    opcode = 2;
    encoded_size = (index & 1) << 1;
    if (index & 4)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 32)
  {
    S = (index & 1) != 0;
    opcode = 4;
    encoded_size = 0;
    if (index & 2)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 64)
  {
    S = 0;
    opcode = 4;
    encoded_size = 1;
    if (index == 1)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }

  EmitLoadStoreSingleStructure(1, 0, opcode, S, encoded_size, encoded_reg, Rn, Rm);
}

void ARM64FloatEmitter::LD1R(u8 size, ARM64Reg Rt, ARM64Reg Rn)
{
  EmitLoadStoreSingleStructure(1, 0, 6, 0, MathUtil::IntLog2(size) - 3, Rt, Rn);
}
void ARM64FloatEmitter::LD2R(u8 size, ARM64Reg Rt, ARM64Reg Rn)
{
  EmitLoadStoreSingleStructure(1, 1, 6, 0, MathUtil::IntLog2(size) - 3, Rt, Rn);
}
void ARM64FloatEmitter::LD1R(u8 size, ARM64Reg Rt, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitLoadStoreSingleStructure(1, 0, 6, 0, MathUtil::IntLog2(size) - 3, Rt, Rn, Rm);
}
void ARM64FloatEmitter::LD2R(u8 size, ARM64Reg Rt, ARM64Reg Rn, ARM64Reg Rm)
{
  EmitLoadStoreSingleStructure(1, 1, 6, 0, MathUtil::IntLog2(size) - 3, Rt, Rn, Rm);
}

void ARM64FloatEmitter::ST1(u8 size, ARM64Reg Rt, u8 index, ARM64Reg Rn)
{
  bool S = 0;
  u32 opcode = 0;
  u32 encoded_size = 0;
  ARM64Reg encoded_reg = ARM64Reg::INVALID_REG;

  if (size == 8)
  {
    S = (index & 4) != 0;
    opcode = 0;
    encoded_size = index & 3;
    if (index & 8)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 16)
  {
    S = (index & 2) != 0;
    opcode = 2;
    encoded_size = (index & 1) << 1;
    if (index & 4)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 32)
  {
    S = (index & 1) != 0;
    opcode = 4;
    encoded_size = 0;
    if (index & 2)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 64)
  {
    S = 0;
    opcode = 4;
    encoded_size = 1;
    if (index == 1)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }

  EmitLoadStoreSingleStructure(0, 0, opcode, S, encoded_size, encoded_reg, Rn);
}

void ARM64FloatEmitter::ST1(u8 size, ARM64Reg Rt, u8 index, ARM64Reg Rn, ARM64Reg Rm)
{
  bool S = 0;
  u32 opcode = 0;
  u32 encoded_size = 0;
  ARM64Reg encoded_reg = ARM64Reg::INVALID_REG;

  if (size == 8)
  {
    S = (index & 4) != 0;
    opcode = 0;
    encoded_size = index & 3;
    if (index & 8)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 16)
  {
    S = (index & 2) != 0;
    opcode = 2;
    encoded_size = (index & 1) << 1;
    if (index & 4)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 32)
  {
    S = (index & 1) != 0;
    opcode = 4;
    encoded_size = 0;
    if (index & 2)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }
  else if (size == 64)
  {
    S = 0;
    opcode = 4;
    encoded_size = 1;
    if (index == 1)
      encoded_reg = EncodeRegToQuad(Rt);
    else
      encoded_reg = EncodeRegToDouble(Rt);
  }

  EmitLoadStoreSingleStructure(0, 0, opcode, S, encoded_size, encoded_reg, Rn, Rm);
}

// Loadstore multiple structure
void ARM64FloatEmitter::LD1(u8 size, u8 count, ARM64Reg Rt, ARM64Reg Rn)
{
  ASSERT_MSG(DYNA_REC, !(count == 0 || count > 4), "Must have a count of 1 to 4 registers! ({})",
             count);
  u32 opcode = 0;
  if (count == 1)
    opcode = 0b111;
  else if (count == 2)
    opcode = 0b1010;
  else if (count == 3)
    opcode = 0b0110;
  else if (count == 4)
    opcode = 0b0010;
  EmitLoadStoreMultipleStructure(size, 1, opcode, Rt, Rn);
}
void ARM64FloatEmitter::LD1(u8 size, u8 count, IndexType type, ARM64Reg Rt, ARM64Reg Rn,
                            ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, !(count == 0 || count > 4), "Must have a count of 1 to 4 registers! ({})",
             count);
  ASSERT_MSG(DYNA_REC, type == IndexType::Post, "Only post indexing is supported!");

  u32 opcode = 0;
  if (count == 1)
    opcode = 0b111;
  else if (count == 2)
    opcode = 0b1010;
  else if (count == 3)
    opcode = 0b0110;
  else if (count == 4)
    opcode = 0b0010;
  EmitLoadStoreMultipleStructurePost(size, 1, opcode, Rt, Rn, Rm);
}
void ARM64FloatEmitter::ST1(u8 size, u8 count, ARM64Reg Rt, ARM64Reg Rn)
{
  ASSERT_MSG(DYNA_REC, !(count == 0 || count > 4), "Must have a count of 1 to 4 registers! ({})",
             count);
  u32 opcode = 0;
  if (count == 1)
    opcode = 0b111;
  else if (count == 2)
    opcode = 0b1010;
  else if (count == 3)
    opcode = 0b0110;
  else if (count == 4)
    opcode = 0b0010;
  EmitLoadStoreMultipleStructure(size, 0, opcode, Rt, Rn);
}
void ARM64FloatEmitter::ST1(u8 size, u8 count, IndexType type, ARM64Reg Rt, ARM64Reg Rn,
                            ARM64Reg Rm)
{
  ASSERT_MSG(DYNA_REC, !(count == 0 || count > 4), "Must have a count of 1 to 4 registers! ({})",
             count);
  ASSERT_MSG(DYNA_REC, type == IndexType::Post, "Only post indexing is supported!");

  u32 opcode = 0;
  if (count == 1)
    opcode = 0b111;
  else if (count == 2)
    opcode = 0b1010;
  else if (count == 3)
    opcode = 0b0110;
  else if (count == 4)
    opcode = 0b0010;
  EmitLoadStoreMultipleStructurePost(size, 0, opcode, Rt, Rn, Rm);
}

// Scalar - 1 Source
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

// Loadstore paired
void ARM64FloatEmitter::LDP(u8 size, IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn,
                            s32 imm)
{
  EncodeLoadStorePair(size, true, type, Rt, Rt2, Rn, imm);
}
void ARM64FloatEmitter::STP(u8 size, IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn,
                            s32 imm)
{
  EncodeLoadStorePair(size, false, type, Rt, Rt2, Rn, imm);
}

// Loadstore register offset
void ARM64FloatEmitter::STR(u8 size, ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  EncodeLoadStoreRegisterOffset(size, false, Rt, Rn, Rm);
}
void ARM64FloatEmitter::LDR(u8 size, ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  EncodeLoadStoreRegisterOffset(size, true, Rt, Rn, Rm);
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

// Scalar - pairwise
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

// Scalar - 2 Source
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

void ARM64FloatEmitter::EmitScalar3Source(bool isDouble, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm,
                                          ARM64Reg Ra, int opcode)
{
  int type = isDouble ? 1 : 0;
  int o1 = opcode >> 1;
  int o0 = opcode & 1;
  m_emit->Write32((0x1F << 24) | (type << 22) | (o1 << 21) | (DecodeReg(Rm) << 16) | (o0 << 15) |
                  (DecodeReg(Ra) << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

// Scalar floating point immediate
void ARM64FloatEmitter::FMOV(ARM64Reg Rd, uint8_t imm8)
{
  EmitScalarImm(0, 0, 0, 0, Rd, imm8);
}

// Vector
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

// Move
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

// One source
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

// Comparison
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

// Float comparison
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

// Permute
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

// Extract
void ARM64FloatEmitter::EXT(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, u32 index)
{
  EmitExtract(index, 0, Rd, Rn, Rm);
}

// Scalar shift by immediate
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

// Vector shift by immediate
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

// vector x indexed element
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

// Modified Immediate
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

void ARM64FloatEmitter::ORR(u8 size, ARM64Reg Rd, u8 imm, u8 shift)
{
  ORR_BIC(size, Rd, imm, shift, 0);
}

void ARM64FloatEmitter::BIC(u8 size, ARM64Reg Rd, u8 imm, u8 shift)
{
  ORR_BIC(size, Rd, imm, shift, 1);
}

void ARM64FloatEmitter::ABI_PushRegisters(BitSet32 registers, ARM64Reg tmp)
{
  bool bundled_loadstore = false;

  for (int i = 0; i < 32; ++i)
  {
    if (!registers[i])
      continue;

    int count = 0;
    while (++count < 4 && (i + count) < 32 && registers[i + count])
    {
    }
    if (count > 1)
    {
      bundled_loadstore = true;
      break;
    }
  }

  if (bundled_loadstore && tmp != ARM64Reg::INVALID_REG)
  {
    DEBUG_ASSERT_MSG(DYNA_REC, Is64Bit(tmp), "Expected a 64-bit temporary register!");

    int num_regs = registers.Count();
    m_emit->SUB(ARM64Reg::SP, ARM64Reg::SP, num_regs * 16);
    m_emit->ADD(tmp, ARM64Reg::SP, 0);
    std::vector<ARM64Reg> island_regs;
    for (int i = 0; i < 32; ++i)
    {
      if (!registers[i])
        continue;

      int count = 0;

      // 0 = true
      // 1 < 4 && registers[i + 1] true!
      // 2 < 4 && registers[i + 2] true!
      // 3 < 4 && registers[i + 3] true!
      // 4 < 4 && registers[i + 4] false!
      while (++count < 4 && (i + count) < 32 && registers[i + count])
      {
      }

      if (count == 1)
        island_regs.push_back(ARM64Reg::Q0 + i);
      else
        ST1(64, count, IndexType::Post, ARM64Reg::Q0 + i, tmp);

      i += count - 1;
    }

    // Handle island registers
    std::vector<ARM64Reg> pair_regs;
    for (auto& it : island_regs)
    {
      pair_regs.push_back(it);
      if (pair_regs.size() == 2)
      {
        STP(128, IndexType::Post, pair_regs[0], pair_regs[1], tmp, 32);
        pair_regs.clear();
      }
    }
    if (pair_regs.size())
      STR(128, IndexType::Post, pair_regs[0], tmp, 16);
  }
  else
  {
    std::vector<ARM64Reg> pair_regs;
    for (auto it : registers)
    {
      pair_regs.push_back(ARM64Reg::Q0 + it);
      if (pair_regs.size() == 2)
      {
        STP(128, IndexType::Pre, pair_regs[0], pair_regs[1], ARM64Reg::SP, -32);
        pair_regs.clear();
      }
    }
    if (pair_regs.size())
      STR(128, IndexType::Pre, pair_regs[0], ARM64Reg::SP, -16);
  }
}
void ARM64FloatEmitter::ABI_PopRegisters(BitSet32 registers, ARM64Reg tmp)
{
  bool bundled_loadstore = false;
  int num_regs = registers.Count();

  for (int i = 0; i < 32; ++i)
  {
    if (!registers[i])
      continue;

    int count = 0;
    while (++count < 4 && (i + count) < 32 && registers[i + count])
    {
    }
    if (count > 1)
    {
      bundled_loadstore = true;
      break;
    }
  }

  if (bundled_loadstore && tmp != ARM64Reg::INVALID_REG)
  {
    // The temporary register is only used to indicate that we can use this code path
    std::vector<ARM64Reg> island_regs;
    for (int i = 0; i < 32; ++i)
    {
      if (!registers[i])
        continue;

      int count = 0;
      while (++count < 4 && (i + count) < 32 && registers[i + count])
      {
      }

      if (count == 1)
        island_regs.push_back(ARM64Reg::Q0 + i);
      else
        LD1(64, count, IndexType::Post, ARM64Reg::Q0 + i, ARM64Reg::SP);

      i += count - 1;
    }

    // Handle island registers
    std::vector<ARM64Reg> pair_regs;
    for (auto& it : island_regs)
    {
      pair_regs.push_back(it);
      if (pair_regs.size() == 2)
      {
        LDP(128, IndexType::Post, pair_regs[0], pair_regs[1], ARM64Reg::SP, 32);
        pair_regs.clear();
      }
    }
    if (pair_regs.size())
      LDR(128, IndexType::Post, pair_regs[0], ARM64Reg::SP, 16);
  }
  else
  {
    bool odd = num_regs % 2;
    std::vector<ARM64Reg> pair_regs;
    for (int i = 31; i >= 0; --i)
    {
      if (!registers[i])
        continue;

      if (odd)
      {
        // First load must be a regular LDR if odd
        odd = false;
        LDR(128, IndexType::Post, ARM64Reg::Q0 + i, ARM64Reg::SP, 16);
      }
      else
      {
        pair_regs.push_back(ARM64Reg::Q0 + i);
        if (pair_regs.size() == 2)
        {
          LDP(128, IndexType::Post, pair_regs[1], pair_regs[0], ARM64Reg::SP, 32);
          pair_regs.clear();
        }
      }
    }
  }
}

void ARM64XEmitter::ANDI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  if (!Is64Bit(Rn))
  {
    // To handle 32-bit logical immediates, the very easiest thing is to repeat
    // the input value twice to make a 64-bit word. The correct encoding of that
    // as a logical immediate will also be the correct encoding of the 32-bit
    // value.
    //
    // Doing this here instead of in the LogicalImm constructor makes it easier
    // to check if the input is all ones.

    imm = (imm << 32) | (imm & 0xFFFFFFFF);
  }

  if (imm == 0)
  {
    MOVZ(Rd, 0);
  }
  else if ((~imm) == 0)
  {
    if (Rd != Rn)
      MOV(Rd, Rn);
  }
  else if (const auto result = LogicalImm(imm, GPRSize::B64))
  {
    AND(Rd, Rn, result);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, scratch != ARM64Reg::INVALID_REG,
               "ANDI2R - failed to construct logical immediate value from {:#10x}, need scratch",
               imm);
    MOVI2R(scratch, imm);
    AND(Rd, Rn, scratch);
  }
}

void ARM64XEmitter::ORRI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  if (!Is64Bit(Rn))
  {
    // To handle 32-bit logical immediates, the very easiest thing is to repeat
    // the input value twice to make a 64-bit word. The correct encoding of that
    // as a logical immediate will also be the correct encoding of the 32-bit
    // value.
    //
    // Doing this here instead of in the LogicalImm constructor makes it easier
    // to check if the input is all ones.

    imm = (imm << 32) | (imm & 0xFFFFFFFF);
  }

  if (imm == 0)
  {
    if (Rd != Rn)
      MOV(Rd, Rn);
  }
  else if ((~imm) == 0)
  {
    MOVN(Rd, 0);
  }
  else if (const auto result = LogicalImm(imm, GPRSize::B64))
  {
    ORR(Rd, Rn, result);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, scratch != ARM64Reg::INVALID_REG,
               "ORRI2R - failed to construct logical immediate value from {:#10x}, need scratch",
               imm);
    MOVI2R(scratch, imm);
    ORR(Rd, Rn, scratch);
  }
}

void ARM64XEmitter::EORI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  if (!Is64Bit(Rn))
  {
    // To handle 32-bit logical immediates, the very easiest thing is to repeat
    // the input value twice to make a 64-bit word. The correct encoding of that
    // as a logical immediate will also be the correct encoding of the 32-bit
    // value.
    //
    // Doing this here instead of in the LogicalImm constructor makes it easier
    // to check if the input is all ones.

    imm = (imm << 32) | (imm & 0xFFFFFFFF);
  }

  if (imm == 0)
  {
    if (Rd != Rn)
      MOV(Rd, Rn);
  }
  else if ((~imm) == 0)
  {
    MVN(Rd, Rn);
  }
  else if (const auto result = LogicalImm(imm, GPRSize::B64))
  {
    EOR(Rd, Rn, result);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, scratch != ARM64Reg::INVALID_REG,
               "EORI2R - failed to construct logical immediate value from {:#10x}, need scratch",
               imm);
    MOVI2R(scratch, imm);
    EOR(Rd, Rn, scratch);
  }
}

void ARM64XEmitter::ANDSI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  if (!Is64Bit(Rn))
  {
    // To handle 32-bit logical immediates, the very easiest thing is to repeat
    // the input value twice to make a 64-bit word. The correct encoding of that
    // as a logical immediate will also be the correct encoding of the 32-bit
    // value.
    //
    // Doing this here instead of in the LogicalImm constructor makes it easier
    // to check if the input is all ones.

    imm = (imm << 32) | (imm & 0xFFFFFFFF);
  }

  if (imm == 0)
  {
    ANDS(Rd, Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR,
         Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR);
  }
  else if ((~imm) == 0)
  {
    ANDS(Rd, Rn, Rn);
  }
  else if (const auto result = LogicalImm(imm, GPRSize::B64))
  {
    ANDS(Rd, Rn, result);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, scratch != ARM64Reg::INVALID_REG,
               "ANDSI2R - failed to construct logical immediate value from {:#10x}, need scratch",
               imm);
    MOVI2R(scratch, imm);
    ANDS(Rd, Rn, scratch);
  }
}

void ARM64XEmitter::AddImmediate(ARM64Reg Rd, ARM64Reg Rn, u64 imm, bool shift, bool negative,
                                 bool flags)
{
  if (!negative)
  {
    if (!flags)
      ADD(Rd, Rn, imm, shift);
    else
      ADDS(Rd, Rn, imm, shift);
  }
  else
  {
    if (!flags)
      SUB(Rd, Rn, imm, shift);
    else
      SUBS(Rd, Rn, imm, shift);
  }
}

void ARM64XEmitter::ADDI2R_internal(ARM64Reg Rd, ARM64Reg Rn, u64 imm, bool negative, bool flags,
                                    ARM64Reg scratch)
{
  DEBUG_ASSERT(Is64Bit(Rd) == Is64Bit(Rn));

  if (!Is64Bit(Rd))
    imm &= 0xFFFFFFFFULL;

  bool has_scratch = scratch != ARM64Reg::INVALID_REG;
  u64 imm_neg = Is64Bit(Rd) ? u64(-s64(imm)) : u64(-s64(imm)) & 0xFFFFFFFFuLL;
  bool neg_neg = negative ? false : true;

  // Special path for zeroes
  if (imm == 0 && !flags)
  {
    if (Rd == Rn)
    {
      return;
    }
    else if (DecodeReg(Rd) != DecodeReg(ARM64Reg::SP) && DecodeReg(Rn) != DecodeReg(ARM64Reg::SP))
    {
      MOV(Rd, Rn);
      return;
    }
  }

  // Regular fast paths, aarch64 immediate instructions
  // Try them all first
  if (imm <= 0xFFF)
  {
    AddImmediate(Rd, Rn, imm, false, negative, flags);
    return;
  }
  if (imm <= 0xFFFFFF && (imm & 0xFFF) == 0)
  {
    AddImmediate(Rd, Rn, imm >> 12, true, negative, flags);
    return;
  }
  if (imm_neg <= 0xFFF)
  {
    AddImmediate(Rd, Rn, imm_neg, false, neg_neg, flags);
    return;
  }
  if (imm_neg <= 0xFFFFFF && (imm_neg & 0xFFF) == 0)
  {
    AddImmediate(Rd, Rn, imm_neg >> 12, true, neg_neg, flags);
    return;
  }

  // ADD+ADD is slower than MOVK+ADD, but inplace.
  // But it supports a few more bits, so use it to avoid MOVK+MOVK+ADD.
  // As this splits the addition in two parts, this must not be done on setting flags.
  if (!flags && (imm >= 0x10000u || !has_scratch) && imm < 0x1000000u)
  {
    AddImmediate(Rd, Rn, imm & 0xFFF, false, negative, false);
    AddImmediate(Rd, Rd, imm >> 12, true, negative, false);
    return;
  }
  if (!flags && (imm_neg >= 0x10000u || !has_scratch) && imm_neg < 0x1000000u)
  {
    AddImmediate(Rd, Rn, imm_neg & 0xFFF, false, neg_neg, false);
    AddImmediate(Rd, Rd, imm_neg >> 12, true, neg_neg, false);
    return;
  }

  ASSERT_MSG(DYNA_REC, has_scratch,
             "ADDI2R - failed to construct arithmetic immediate value from {:#10x}, need scratch",
             imm);

  negative ^= MOVI2R2(scratch, imm, imm_neg);
  if (!negative)
  {
    if (!flags)
      ADD(Rd, Rn, scratch);
    else
      ADDS(Rd, Rn, scratch);
  }
  else
  {
    if (!flags)
      SUB(Rd, Rn, scratch);
    else
      SUBS(Rd, Rn, scratch);
  }
}

void ARM64XEmitter::ADDI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  ADDI2R_internal(Rd, Rn, imm, false, false, scratch);
}

void ARM64XEmitter::ADDSI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  ADDI2R_internal(Rd, Rn, imm, false, true, scratch);
}

void ARM64XEmitter::SUBI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  ADDI2R_internal(Rd, Rn, imm, true, false, scratch);
}

void ARM64XEmitter::SUBSI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  ADDI2R_internal(Rd, Rn, imm, true, true, scratch);
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

// TODO: Quite a few values could be generated easily using the MOVI instruction and friends.
void ARM64FloatEmitter::MOVI2FDUP(ARM64Reg Rd, float value, ARM64Reg scratch)
{
  // TODO: Make it work with more element sizes
  // TODO: Optimize - there are shorter solution for many values
  ARM64Reg s = ARM64Reg::S0 + DecodeReg(Rd);
  MOVI2F(s, value, scratch);
  DUP(32, Rd, Rd, 0);
}

}  // namespace Arm64Gen
