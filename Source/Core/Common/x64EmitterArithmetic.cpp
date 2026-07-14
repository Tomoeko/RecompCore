// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/x64Emitter.h"

#include <cstring>

#include "Common/CPUDetect.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/x64Reg.h"

namespace Gen

#define DEFINE_NORMAL_OP(name, op) \
  void XEmitter::name(int bits, const OpArg& a1, const OpArg& a2) \
  { \
    CheckFlags(); \
    WriteNormalOp(bits, op, a1, a2); \
  }

#define DEFINE_MULDIV_OP(name, ext) \
  void XEmitter::name(int bits, const OpArg& src) \
  { \
    WriteMulDivType(bits, src, ext); \
  }

{

void XEmitter::XCHG_AHAL()
{
  Write8(0x86);
  Write8(0xe0);
  // alt. 86 c4
}

void XEmitter::LFENCE()
{
  Write8(0x0F);
  Write8(0xAE);
  Write8(0xE8);
}

void XEmitter::MFENCE()
{
  Write8(0x0F);
  Write8(0xAE);
  Write8(0xF0);
}

void XEmitter::SFENCE()
{
  Write8(0x0F);
  Write8(0xAE);
  Write8(0xF8);
}

void XEmitter::CWD(int bits)
{
  if (bits == 16)
    Write8(0x66);
  Rex(bits == 64, 0, 0, 0);
  Write8(0x99);
}

void XEmitter::CBW(int bits)
{
  if (bits == 8)
    Write8(0x66);
  Rex(bits == 32, 0, 0, 0);
  Write8(0x98);
}

void XEmitter::BSWAP(int bits, X64Reg reg)
{
  if (bits >= 32)
  {
    WriteSimple2Byte(bits, 0x0F, 0xC8, reg);
  }
  else if (bits == 16)
  {
    ROL(16, R(reg), Imm8(8));
  }
  else if (bits == 8)
  {
    // Do nothing - can't bswap a single byte...
  }
  else
  {
    ASSERT_MSG(DYNA_REC, 0, "BSWAP - Wrong number of bits");
  }
}

DEFINE_MULDIV_OP(NEG, 3)

DEFINE_MULDIV_OP(NOT, 2)

void XEmitter::MOVBE(int bits, X64Reg dest, const OpArg& src)
{
  WriteMOVBE(bits, 0xF0, dest, src);
}

void XEmitter::MOVBE(int bits, const OpArg& dest, X64Reg src)
{
  WriteMOVBE(bits, 0xF1, src, dest);
}

void XEmitter::LoadAndSwap(int size, X64Reg dst, const OpArg& src, bool sign_extend, MovInfo* info)
{
  if (info)
  {
    info->address = GetWritableCodePtr();
    info->nonAtomicSwapStore = false;
  }

  switch (size)
  {
  case 8:
    if (sign_extend)
      MOVSX(32, 8, dst, src);
    else
      MOVZX(32, 8, dst, src);
    break;
  case 16:
    MOVZX(32, 16, dst, src);
    if (sign_extend)
    {
      BSWAP(32, dst);
      SAR(32, R(dst), Imm8(16));
    }
    else
    {
      ROL(16, R(dst), Imm8(8));
    }
    break;
  case 32:
  case 64:
    if (cpu_info.bMOVBE)
    {
      MOVBE(size, dst, src);
    }
    else
    {
      MOV(size, R(dst), src);
      BSWAP(size, dst);
    }
    break;
  }
}

void XEmitter::SwapAndStore(int size, const OpArg& dst, X64Reg src, MovInfo* info)
{
  if (cpu_info.bMOVBE)
  {
    if (info)
    {
      info->address = GetWritableCodePtr();
      info->nonAtomicSwapStore = false;
    }
    MOVBE(size, dst, src);
  }
  else
  {
    BSWAP(size, src);
    if (info)
    {
      info->address = GetWritableCodePtr();
      info->nonAtomicSwapStore = true;
      info->nonAtomicSwapStoreSrc = src;
    }
    MOV(size, dst, R(src));
  }
}

void XEmitter::LEA(int bits, X64Reg dest, OpArg src)
{
  ASSERT_MSG(DYNA_REC, !src.IsImm(), "LEA - Imm argument");
  src.operandReg = (u8)dest;
  if (bits == 16)
    Write8(0x66);  // TODO: performance warning
  src.WriteREX(this, bits, bits);
  Write8(0x8D);
  src.WriteRest(this, 0, INVALID_REG, bits == 64);
}

void XEmitter::WriteNormalOp(int bits, NormalOp op, const OpArg& a1, const OpArg& a2)
{
  if (a1.IsImm())
  {
    // Booh! Can't write to an imm
    ASSERT_MSG(DYNA_REC, 0, "WriteNormalOp - a1 cannot be imm");
    return;
  }
  if (a2.IsImm())
  {
    a1.WriteNormalOp(this, true, op, a2, bits);
  }
  else
  {
    if (a1.IsSimpleReg())
    {
      a2.WriteNormalOp(this, false, op, a1, bits);
    }
    else
    {
      ASSERT_MSG(DYNA_REC, a2.IsSimpleReg() || a2.IsImm(),
                 "WriteNormalOp - a1 and a2 cannot both be memory");
      a1.WriteNormalOp(this, true, op, a2, bits);
    }
  }
}

DEFINE_NORMAL_OP(ADD, NormalOp::ADD)

DEFINE_NORMAL_OP(ADC, NormalOp::ADC)

DEFINE_NORMAL_OP(SUB, NormalOp::SUB)

DEFINE_NORMAL_OP(SBB, NormalOp::SBB)

DEFINE_NORMAL_OP(AND, NormalOp::AND)

DEFINE_NORMAL_OP(OR, NormalOp::OR)

DEFINE_NORMAL_OP(XOR, NormalOp::XOR)

void XEmitter::MOV(int bits, const OpArg& a1, const OpArg& a2)
{
  if (bits == 64 && a1.IsSimpleReg() &&
      ((a2.scale == SCALE_IMM64 && a2.offset == static_cast<u32>(a2.offset)) ||
       (a2.scale == SCALE_IMM32 && static_cast<s32>(a2.offset) >= 0)))
  {
    WriteNormalOp(32, NormalOp::MOV, a1, a2.AsImm32());
    return;
  }
  CheckFlags();
  WriteNormalOp(bits, NormalOp::MOV, a1, a2);
}

DEFINE_NORMAL_OP(TEST, NormalOp::TEST)

DEFINE_NORMAL_OP(CMP, NormalOp::CMP)

void XEmitter::XCHG(int bits, const OpArg& a1, const OpArg& a2)
{
  WriteNormalOp(bits, NormalOp::XCHG, a1, a2);
}

void XEmitter::CMP_or_TEST(int bits, const OpArg& a1, const OpArg& a2)
{
  CheckFlags();
  if (a1.IsSimpleReg() && a2.IsZero())  // turn 'CMP reg, 0' into shorter 'TEST reg, reg'
  {
    WriteNormalOp(bits, NormalOp::TEST, a1, a1);
  }
  else
  {
    WriteNormalOp(bits, NormalOp::CMP, a1, a2);
  }
}

void XEmitter::MOV_sum(int bits, X64Reg dest, const OpArg& a1, const OpArg& a2)
{
  // This stomps on flags, so ensure they aren't locked
  DEBUG_ASSERT(!flags_locked);

  // Zero shortcuts (note that this can generate no code in the case where a1 == dest && a2 == zero
  // or a2 == dest && a1 == zero)
  if (a1.IsZero())
  {
    if (!a2.IsSimpleReg() || a2.GetSimpleReg() != dest)
    {
      MOV(bits, R(dest), a2);
    }
    return;
  }
  if (a2.IsZero())
  {
    if (!a1.IsSimpleReg() || a1.GetSimpleReg() != dest)
    {
      MOV(bits, R(dest), a1);
    }
    return;
  }

  // If dest == a1 or dest == a2 we can simplify this
  if (a1.IsSimpleReg() && a1.GetSimpleReg() == dest)
  {
    ADD(bits, R(dest), a2);
    return;
  }

  if (a2.IsSimpleReg() && a2.GetSimpleReg() == dest)
  {
    ADD(bits, R(dest), a1);
    return;
  }

  // TODO: 32-bit optimizations may apply to other bit sizes (confirm)
  if (bits == 32)
  {
    if (a1.IsImm() && a2.IsImm())
    {
      MOV(32, R(dest), Imm32(a1.Imm32() + a2.Imm32()));
      return;
    }

    if (a1.IsSimpleReg() && a2.IsSimpleReg())
    {
      LEA(32, dest, MRegSum(a1.GetSimpleReg(), a2.GetSimpleReg()));
      return;
    }

    if (a1.IsSimpleReg() && a2.IsImm())
    {
      LEA(32, dest, MDisp(a1.GetSimpleReg(), a2.Imm32()));
      return;
    }

    if (a1.IsImm() && a2.IsSimpleReg())
    {
      LEA(32, dest, MDisp(a2.GetSimpleReg(), a1.Imm32()));
      return;
    }
  }

  // Fallback
  MOV(bits, R(dest), a1);
  ADD(bits, R(dest), a2);
}

void XEmitter::LOCK()
{
  Write8(0xF0);
}

void XEmitter::REP()
{
  Write8(0xF3);
}

void XEmitter::REPNE()
{
  Write8(0xF2);
}

void XEmitter::FSOverride()
{
  Write8(0x64);
}

void XEmitter::GSOverride()
{
  Write8(0x65);
}

}  // namespace Gen


