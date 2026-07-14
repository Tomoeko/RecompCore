// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/x64Emitter.h"

#include <cstring>

#include "Common/CPUDetect.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/x64Reg.h"

namespace Gen
{

void XEmitter::JMP(const u8* addr, bool force_near_padding)
{
  u64 fn = (u64)addr;
  s64 distance = (s64)(fn - ((u64)code + SHORT_JMP_LEN));
  if (distance < -0x80 || distance >= 0x80)
  {
    distance = (s64)(fn - ((u64)code + NEAR_JMP_LEN));
    ASSERT_MSG(DYNA_REC, distance >= -0x80000000LL && distance < 0x80000000LL,
               "Jump target too far away ({}), needs indirect register", distance);
    Write8(0xE9);
    Write32((u32)(s32)distance);
  }
  else
  {
    Write8(0xEB);
    Write8((u8)(s8)distance);
    if (force_near_padding)
    {
      for (int i = 0; i < NEAR_JMP_LEN - SHORT_JMP_LEN; i++)
      {
        // INT3 is more efficient than NOP if never executed, as it stops CPU speculation.
        INT3();
      }
    }
  }
}

void XEmitter::JMPptr(const OpArg& arg2)
{
  OpArg arg = arg2;
  if (arg.IsImm())
    ASSERT_MSG(DYNA_REC, 0, "JMPptr - Imm argument");
  arg.operandReg = 4;
  arg.WriteREX(this, 0, 0);
  Write8(0xFF);
  arg.WriteRest(this);
}

void XEmitter::JMPself()
{
  Write8(0xEB);
  Write8(0xFE);
}

void XEmitter::CALLptr(OpArg arg)
{
  if (arg.IsImm())
    ASSERT_MSG(DYNA_REC, 0, "CALLptr - Imm argument");
  arg.operandReg = 2;
  arg.WriteREX(this, 0, 0);
  Write8(0xFF);
  arg.WriteRest(this);
}

void XEmitter::CALL(const void* fnptr)
{
  u64 distance = u64(fnptr) - (u64(code) + 5);
  ASSERT_MSG(DYNA_REC, distance < 0x0000000080000000ULL || distance >= 0xFFFFFFFF80000000ULL,
             "CALL out of range ({} calls {})", fmt::ptr(code), fmt::ptr(fnptr));
  Write8(0xE8);
  Write32(u32(distance));
}

FixupBranch XEmitter::CALL()
{
  FixupBranch branch;
  branch.type = FixupBranch::Type::Branch32Bit;
  branch.ptr = code + 5;
  Write8(0xE8);
  Write32(0);

  // If we couldn't write the full call instruction, indicate that in the returned FixupBranch by
  // setting the branch's address to null. This will prevent a later SetJumpTarget() from writing to
  // invalid memory.
  if (HasWriteFailed())
    branch.ptr = nullptr;

  return branch;
}

FixupBranch XEmitter::J(const Jump jump)
{
  FixupBranch branch;
  const bool is_near_jump = jump == Jump::Near;
  branch.type = is_near_jump ? FixupBranch::Type::Branch32Bit : FixupBranch::Type::Branch8Bit;
  branch.ptr = code + (is_near_jump ? 5 : 2);
  if (!is_near_jump)
  {
    // 8 bits will do
    Write8(0xEB);
    Write8(0);
  }
  else
  {
    Write8(0xE9);
    Write32(0);
  }

  // If we couldn't write the full jump instruction, indicate that in the returned FixupBranch by
  // setting the branch's address to null. This will prevent a later SetJumpTarget() from writing to
  // invalid memory.
  if (HasWriteFailed())
    branch.ptr = nullptr;

  return branch;
}

FixupBranch XEmitter::J_CC(CCFlags conditionCode, const Jump jump)
{
  FixupBranch branch;
  const bool is_near_jump = jump == Jump::Near;
  branch.type = is_near_jump ? FixupBranch::Type::Branch32Bit : FixupBranch::Type::Branch8Bit;
  branch.ptr = code + (is_near_jump ? 6 : 2);
  if (!is_near_jump)
  {
    // 8 bits will do
    Write8(0x70 + conditionCode);
    Write8(0);
  }
  else
  {
    Write8(0x0F);
    Write8(0x80 + conditionCode);
    Write32(0);
  }

  // If we couldn't write the full jump instruction, indicate that in the returned FixupBranch by
  // setting the branch's address to null. This will prevent a later SetJumpTarget() from writing to
  // invalid memory.
  if (HasWriteFailed())
    branch.ptr = nullptr;

  return branch;
}

void XEmitter::J_CC(CCFlags conditionCode, const u8* addr)
{
  u64 fn = (u64)addr;
  s64 distance = (s64)(fn - ((u64)code + 2));
  if (distance < -0x80 || distance >= 0x80)
  {
    distance = (s64)(fn - ((u64)code + 6));
    ASSERT_MSG(DYNA_REC, distance >= -0x80000000LL && distance < 0x80000000LL,
               "Jump target too far away ({}), needs indirect register", distance);
    Write8(0x0F);
    Write8(0x80 + conditionCode);
    Write32((u32)(s32)distance);
  }
  else
  {
    Write8(0x70 + conditionCode);
    Write8((u8)(s8)distance);
  }
}

void XEmitter::SetJumpTarget(const FixupBranch& branch)
{
  if (!branch.ptr)
    return;

  if (branch.type == FixupBranch::Type::Branch8Bit)
  {
    s64 distance = (s64)(code - branch.ptr);
    ASSERT_MSG(DYNA_REC, distance >= -0x80 && distance < 0x80,
               "Jump::Short target too far away ({}), needs Jump::Near", distance);
    branch.ptr[-1] = (u8)(s8)distance;
  }
  else if (branch.type == FixupBranch::Type::Branch32Bit)
  {
    s64 distance = (s64)(code - branch.ptr);
    ASSERT_MSG(DYNA_REC, distance >= -0x80000000LL && distance < 0x80000000LL,
               "Jump::Near target too far away ({}), needs indirect register", distance);

    s32 valid_distance = static_cast<s32>(distance);
    std::memcpy(&branch.ptr[-4], &valid_distance, sizeof(s32));
  }
}

void XEmitter::RET()
{
  Write8(0xC3);
}

void XEmitter::RET_FAST()
{
  Write8(0xF3);
  Write8(0xC3);
}

void XEmitter::UD2()
{
  Write8(0x0F);
  Write8(0x0B);
}

}  // namespace Gen
