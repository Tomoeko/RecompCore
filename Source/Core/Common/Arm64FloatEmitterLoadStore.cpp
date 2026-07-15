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

void ARM64FloatEmitter::LDR(u8 size, IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EmitLoadStoreImmediate(size, 1, type, Rt, Rn, imm);
}

void ARM64FloatEmitter::STR(u8 size, IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EmitLoadStoreImmediate(size, 0, type, Rt, Rn, imm);
}

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

void ARM64FloatEmitter::STR(u8 size, ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  EncodeLoadStoreRegisterOffset(size, false, Rt, Rn, Rm);
}

void ARM64FloatEmitter::LDR(u8 size, ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  EncodeLoadStoreRegisterOffset(size, true, Rt, Rn, Rm);
}
}  // namespace Arm64Gen
