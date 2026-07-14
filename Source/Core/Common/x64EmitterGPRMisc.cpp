// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/x64Emitter.h"

#include <cstring>

#include "Common/CPUDetect.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/x64Reg.h"

namespace Gen

#define DEFINE_MULDIV_OP(name, ext) \
  void XEmitter::name(int bits, const OpArg& src) \
  { \
    WriteMulDivType(bits, src, ext); \
  }

#define DEFINE_SHIFT_OP(name, ext) \
  void XEmitter::name(int bits, const OpArg& dest, const OpArg& shift) \
  { \
    WriteShift(bits, dest, shift, ext); \
  }

#define DEFINE_BITTEST_OP(name, ext) \
  void XEmitter::name(int bits, const OpArg& dest, const OpArg& index) \
  { \
    WriteBitTest(bits, dest, index, ext); \
  }

#define DEFINE_BITSEARCH_OP(name, op) \
  void XEmitter::name(int bits, X64Reg dest, const OpArg& src) \
  { \
    WriteBitSearchType(bits, dest, src, op); \
  }

{

void XEmitter::SETcc(CCFlags flag, OpArg dest)
{
  ASSERT_MSG(DYNA_REC, !dest.IsImm(), "SETcc - Imm argument");
  dest.operandReg = 0;
  dest.WriteREX(this, 0, 8);
  Write8(0x0F);
  Write8(0x90 + (u8)flag);
  dest.WriteRest(this);
}

void XEmitter::CMOVcc(int bits, X64Reg dest, OpArg src, CCFlags flag)
{
  ASSERT_MSG(DYNA_REC, !src.IsImm(), "CMOVcc - Imm argument");
  ASSERT_MSG(DYNA_REC, bits != 8, "CMOVcc - 8 bits unsupported");
  if (bits == 16)
    Write8(0x66);
  src.operandReg = dest;
  src.WriteREX(this, bits, bits);
  Write8(0x0F);
  Write8(0x40 + (u8)flag);
  src.WriteRest(this);
}

void XEmitter::WriteMulDivType(int bits, OpArg src, int ext)
{
  ASSERT_MSG(DYNA_REC, !src.IsImm(), "WriteMulDivType - Imm argument");
  CheckFlags();
  src.operandReg = ext;
  if (bits == 16)
    Write8(0x66);
  src.WriteREX(this, bits, bits, 0);
  if (bits == 8)
  {
    Write8(0xF6);
  }
  else
  {
    Write8(0xF7);
  }
  src.WriteRest(this);
}

DEFINE_MULDIV_OP(MUL, 4)

DEFINE_MULDIV_OP(DIV, 6)

DEFINE_MULDIV_OP(IMUL, 5)

DEFINE_MULDIV_OP(IDIV, 7)

void XEmitter::WriteBitSearchType(int bits, X64Reg dest, OpArg src, u8 byte2, bool rep)
{
  ASSERT_MSG(DYNA_REC, !src.IsImm(), "WriteBitSearchType - Imm argument");
  CheckFlags();
  src.operandReg = (u8)dest;
  if (bits == 16)
    Write8(0x66);
  if (rep)
    Write8(0xF3);
  src.WriteREX(this, bits, bits);
  Write8(0x0F);
  Write8(byte2);
  src.WriteRest(this);
}

void XEmitter::MOVNTI(int bits, const OpArg& dest, X64Reg src)
{
  if (bits <= 16)
    ASSERT_MSG(DYNA_REC, 0, "MOVNTI - bits<=16");
  WriteBitSearchType(bits, src, dest, 0xC3);
}

DEFINE_BITSEARCH_OP(BSF, 0xBC)

DEFINE_BITSEARCH_OP(BSR, 0xBD)

void XEmitter::TZCNT(int bits, X64Reg dest, const OpArg& src)
{
  CheckFlags();
  if (!cpu_info.bBMI1)
    PanicAlertFmt("Trying to use BMI1 on a system that doesn't support it. Bad programmer.");
  WriteBitSearchType(bits, dest, src, 0xBC, true);
}

void XEmitter::LZCNT(int bits, X64Reg dest, const OpArg& src)
{
  CheckFlags();
  if (!cpu_info.bLZCNT)
    PanicAlertFmt("Trying to use LZCNT on a system that doesn't support it. Bad programmer.");
  WriteBitSearchType(bits, dest, src, 0xBD, true);
}

void XEmitter::MOVSX(int dbits, int sbits, X64Reg dest, OpArg src)
{
  ASSERT_MSG(DYNA_REC, !src.IsImm(), "MOVSX - Imm argument");
  if (dbits == sbits)
  {
    MOV(dbits, R(dest), src);
    return;
  }
  src.operandReg = (u8)dest;
  if (dbits == 16)
    Write8(0x66);
  src.WriteREX(this, dbits, sbits);
  if (sbits == 8)
  {
    Write8(0x0F);
    Write8(0xBE);
  }
  else if (sbits == 16)
  {
    Write8(0x0F);
    Write8(0xBF);
  }
  else if (sbits == 32 && dbits == 64)
  {
    Write8(0x63);
  }
  else
  {
    Crash();
  }
  src.WriteRest(this);
}

void XEmitter::MOVZX(int dbits, int sbits, X64Reg dest, OpArg src)
{
  ASSERT_MSG(DYNA_REC, !src.IsImm(), "MOVZX - Imm argument");
  if (dbits == sbits)
  {
    MOV(dbits, R(dest), src);
    return;
  }
  src.operandReg = (u8)dest;
  if (dbits == 16)
    Write8(0x66);
  // the 32bit result is automatically zero extended to 64bit
  src.WriteREX(this, dbits == 64 ? 32 : dbits, sbits);
  if (sbits == 8)
  {
    Write8(0x0F);
    Write8(0xB6);
  }
  else if (sbits == 16)
  {
    Write8(0x0F);
    Write8(0xB7);
  }
  else if (sbits == 32 && dbits == 64)
  {
    Write8(0x8B);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, 0, "MOVZX - Invalid size");
  }
  src.WriteRest(this);
}

void XEmitter::WriteMOVBE(int bits, u8 op, X64Reg reg, const OpArg& arg)
{
  ASSERT_MSG(DYNA_REC, cpu_info.bMOVBE, "Generating MOVBE on a system that does not support it.");
  if (bits == 8)
  {
    MOV(8, op & 1 ? arg : R(reg), op & 1 ? R(reg) : arg);
    return;
  }
  if (bits == 16)
    Write8(0x66);
  ASSERT_MSG(DYNA_REC, !arg.IsSimpleReg() && !arg.IsImm(), "MOVBE: need r<-m or m<-r!");
  arg.WriteREX(this, bits, bits, reg);
  Write8(0x0F);
  Write8(0x38);
  Write8(op);
  arg.WriteRest(this, 0, reg);
}

void XEmitter::WriteShift(int bits, OpArg dest, const OpArg& shift, int ext)
{
  CheckFlags();
  bool writeImm = false;
  if (dest.IsImm())
  {
    ASSERT_MSG(DYNA_REC, 0, "WriteShift - can't shift imms");
  }
  if ((shift.IsSimpleReg() && shift.GetSimpleReg() != ECX) ||
      (shift.IsImm() && shift.GetImmBits() != 8))
  {
    ASSERT_MSG(DYNA_REC, 0, "WriteShift - illegal argument");
  }
  dest.operandReg = ext;
  if (bits == 16)
    Write8(0x66);
  dest.WriteREX(this, bits, bits, 0);
  if (shift.GetImmBits() == 8)
  {
    // ok an imm
    u8 imm = (u8)shift.offset;
    if (imm == 1)
    {
      Write8(bits == 8 ? 0xD0 : 0xD1);
    }
    else
    {
      writeImm = true;
      Write8(bits == 8 ? 0xC0 : 0xC1);
    }
  }
  else
  {
    Write8(bits == 8 ? 0xD2 : 0xD3);
  }
  dest.WriteRest(this, writeImm ? 1 : 0);
  if (writeImm)
    Write8((u8)shift.offset);
}

DEFINE_SHIFT_OP(ROL, 0)

DEFINE_SHIFT_OP(ROR, 1)

DEFINE_SHIFT_OP(RCL, 2)

DEFINE_SHIFT_OP(RCR, 3)

DEFINE_SHIFT_OP(SHL, 4)

DEFINE_SHIFT_OP(SHR, 5)

DEFINE_SHIFT_OP(SAR, 7)

void XEmitter::WriteBitTest(int bits, const OpArg& dest, const OpArg& index, int ext)
{
  CheckFlags();
  if (dest.IsImm())
  {
    ASSERT_MSG(DYNA_REC, 0, "WriteBitTest - can't test imms");
  }
  if ((index.IsImm() && index.GetImmBits() != 8))
  {
    ASSERT_MSG(DYNA_REC, 0, "WriteBitTest - illegal argument");
  }
  if (bits == 16)
    Write8(0x66);
  if (index.IsImm())
  {
    dest.WriteREX(this, bits, bits);
    Write8(0x0F);
    Write8(0xBA);
    dest.WriteRest(this, 1, (X64Reg)ext);
    Write8((u8)index.offset);
  }
  else
  {
    X64Reg operand = index.GetSimpleReg();
    dest.WriteREX(this, bits, bits, operand);
    Write8(0x0F);
    Write8(0x83 + 8 * ext);
    dest.WriteRest(this, 1, operand);
  }
}

DEFINE_BITTEST_OP(BT, 4)

DEFINE_BITTEST_OP(BTS, 5)

DEFINE_BITTEST_OP(BTR, 6)

DEFINE_BITTEST_OP(BTC, 7)

void XEmitter::SHRD(int bits, const OpArg& dest, const OpArg& src, const OpArg& shift)
{
  CheckFlags();
  if (dest.IsImm())
  {
    ASSERT_MSG(DYNA_REC, 0, "SHRD - can't use imms as destination");
  }
  if (!src.IsSimpleReg())
  {
    ASSERT_MSG(DYNA_REC, 0, "SHRD - must use simple register as source");
  }
  if ((shift.IsSimpleReg() && shift.GetSimpleReg() != ECX) ||
      (shift.IsImm() && shift.GetImmBits() != 8))
  {
    ASSERT_MSG(DYNA_REC, 0, "SHRD - illegal shift");
  }
  if (bits == 16)
    Write8(0x66);
  X64Reg operand = src.GetSimpleReg();
  dest.WriteREX(this, bits, bits, operand);
  if (shift.GetImmBits() == 8)
  {
    Write8(0x0F);
    Write8(0xAC);
    dest.WriteRest(this, 1, operand);
    Write8((u8)shift.offset);
  }
  else
  {
    Write8(0x0F);
    Write8(0xAD);
    dest.WriteRest(this, 0, operand);
  }
}

void XEmitter::SHLD(int bits, const OpArg& dest, const OpArg& src, const OpArg& shift)
{
  CheckFlags();
  if (dest.IsImm())
  {
    ASSERT_MSG(DYNA_REC, 0, "SHLD - can't use imms as destination");
  }
  if (!src.IsSimpleReg())
  {
    ASSERT_MSG(DYNA_REC, 0, "SHLD - must use simple register as source");
  }
  if ((shift.IsSimpleReg() && shift.GetSimpleReg() != ECX) ||
      (shift.IsImm() && shift.GetImmBits() != 8))
  {
    ASSERT_MSG(DYNA_REC, 0, "SHLD - illegal shift");
  }
  if (bits == 16)
    Write8(0x66);
  X64Reg operand = src.GetSimpleReg();
  dest.WriteREX(this, bits, bits, operand);
  if (shift.GetImmBits() == 8)
  {
    Write8(0x0F);
    Write8(0xA4);
    dest.WriteRest(this, 1, operand);
    Write8((u8)shift.offset);
  }
  else
  {
    Write8(0x0F);
    Write8(0xA5);
    dest.WriteRest(this, 0, operand);
  }
}

void XEmitter::IMUL(int bits, X64Reg regOp, const OpArg& a1, const OpArg& a2)
{
  CheckFlags();
  if (bits == 8)
  {
    ASSERT_MSG(DYNA_REC, 0, "IMUL - illegal bit size!");
    return;
  }

  if (a1.IsImm())
  {
    ASSERT_MSG(DYNA_REC, 0, "IMUL - second arg cannot be imm!");
    return;
  }

  if (!a2.IsImm())
  {
    ASSERT_MSG(DYNA_REC, 0, "IMUL - third arg must be imm!");
    return;
  }

  if (bits == 16)
    Write8(0x66);
  a1.WriteREX(this, bits, bits, regOp);

  if (a2.GetImmBits() == 8 || (a2.GetImmBits() == 16 && (s8)a2.offset == (s16)a2.offset) ||
      (a2.GetImmBits() == 32 && (s8)a2.offset == (s32)a2.offset))
  {
    Write8(0x6B);
    a1.WriteRest(this, 1, regOp);
    Write8((u8)a2.offset);
  }
  else
  {
    Write8(0x69);
    if (a2.GetImmBits() == 16 && bits == 16)
    {
      a1.WriteRest(this, 2, regOp);
      Write16((u16)a2.offset);
    }
    else if (a2.GetImmBits() == 32 && (bits == 32 || bits == 64))
    {
      a1.WriteRest(this, 4, regOp);
      Write32((u32)a2.offset);
    }
    else
    {
      ASSERT_MSG(DYNA_REC, 0, "IMUL - unhandled case!");
    }
  }
}

void XEmitter::IMUL(int bits, X64Reg regOp, const OpArg& a)
{
  CheckFlags();
  if (bits == 8)
  {
    ASSERT_MSG(DYNA_REC, 0, "IMUL - illegal bit size!");
    return;
  }

  if (a.IsImm())
  {
    IMUL(bits, regOp, R(regOp), a);
    return;
  }

  if (bits == 16)
    Write8(0x66);
  a.WriteREX(this, bits, bits, regOp);
  Write8(0x0F);
  Write8(0xAF);
  a.WriteRest(this, 0, regOp);
}

void XEmitter::SARX(int bits, X64Reg regOp1, const OpArg& arg, X64Reg regOp2)
{
  WriteBMI2Op(bits, 0xF3, 0x38F7, regOp1, regOp2, arg);
}

void XEmitter::SHLX(int bits, X64Reg regOp1, const OpArg& arg, X64Reg regOp2)
{
  WriteBMI2Op(bits, 0x66, 0x38F7, regOp1, regOp2, arg);
}

void XEmitter::SHRX(int bits, X64Reg regOp1, const OpArg& arg, X64Reg regOp2)
{
  WriteBMI2Op(bits, 0xF2, 0x38F7, regOp1, regOp2, arg);
}

void XEmitter::RORX(int bits, X64Reg regOp, const OpArg& arg, u8 rotate)
{
  WriteBMI2Op(bits, 0xF2, 0x3AF0, regOp, INVALID_REG, arg, 1);
  Write8(rotate);
}

void XEmitter::MULX(int bits, X64Reg regOp1, X64Reg regOp2, const OpArg& arg)
{
  WriteBMI2Op(bits, 0xF2, 0x38F6, regOp2, regOp1, arg);
}

void XEmitter::BZHI(int bits, X64Reg regOp1, const OpArg& arg, X64Reg regOp2)
{
  CheckFlags();
  WriteBMI2Op(bits, 0x00, 0x38F5, regOp1, regOp2, arg);
}

void XEmitter::BLSR(int bits, X64Reg regOp, const OpArg& arg)
{
  WriteBMI1Op(bits, 0x00, 0x38F3, (X64Reg)0x1, regOp, arg);
}

void XEmitter::BLSMSK(int bits, X64Reg regOp, const OpArg& arg)
{
  WriteBMI1Op(bits, 0x00, 0x38F3, (X64Reg)0x2, regOp, arg);
}

void XEmitter::BLSI(int bits, X64Reg regOp, const OpArg& arg)
{
  WriteBMI1Op(bits, 0x00, 0x38F3, (X64Reg)0x3, regOp, arg);
}

void XEmitter::BEXTR(int bits, X64Reg regOp1, const OpArg& arg, X64Reg regOp2)
{
  WriteBMI1Op(bits, 0x00, 0x38F7, regOp1, regOp2, arg);
}

void XEmitter::PDEP(int bits, X64Reg regOp1, X64Reg regOp2, const OpArg& arg)
{
  WriteBMI2Op(bits, 0xF2, 0x38F5, regOp1, regOp2, arg);
}

void XEmitter::PEXT(int bits, X64Reg regOp1, X64Reg regOp2, const OpArg& arg)
{
  WriteBMI2Op(bits, 0xF3, 0x38F5, regOp1, regOp2, arg);
}

void XEmitter::ANDN(int bits, X64Reg regOp1, X64Reg regOp2, const OpArg& arg)
{
  WriteBMI1Op(bits, 0x00, 0x38F2, regOp1, regOp2, arg);
}

void XEmitter::RDTSC()
{
  Write8(0x0F);
  Write8(0x31);
}
}  // namespace Gen

