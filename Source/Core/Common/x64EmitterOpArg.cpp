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

struct NormalOpDef
{
  u8 toRm8, toRm32, fromRm8, fromRm32, imm8, imm32, simm8, eaximm8, eaximm32, ext;
};

// 0xCC is code for invalid combination of immediates
static const NormalOpDef normalops[11] = {
    {0x00, 0x01, 0x02, 0x03, 0x80, 0x81, 0x83, 0x04, 0x05, 0},  // ADD
    {0x10, 0x11, 0x12, 0x13, 0x80, 0x81, 0x83, 0x14, 0x15, 2},  // ADC

    {0x28, 0x29, 0x2A, 0x2B, 0x80, 0x81, 0x83, 0x2C, 0x2D, 5},  // SUB
    {0x18, 0x19, 0x1A, 0x1B, 0x80, 0x81, 0x83, 0x1C, 0x1D, 3},  // SBB

    {0x20, 0x21, 0x22, 0x23, 0x80, 0x81, 0x83, 0x24, 0x25, 4},  // AND
    {0x08, 0x09, 0x0A, 0x0B, 0x80, 0x81, 0x83, 0x0C, 0x0D, 1},  // OR

    {0x30, 0x31, 0x32, 0x33, 0x80, 0x81, 0x83, 0x34, 0x35, 6},  // XOR
    {0x88, 0x89, 0x8A, 0x8B, 0xC6, 0xC7, 0xCC, 0xCC, 0xCC, 0},  // MOV

    {0x84, 0x85, 0x84, 0x85, 0xF6, 0xF7, 0xCC, 0xA8, 0xA9, 0},  // TEST (to == from)
    {0x38, 0x39, 0x3A, 0x3B, 0x80, 0x81, 0x83, 0x3C, 0x3D, 7},  // CMP

    {0x86, 0x87, 0x86, 0x87, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 7},  // XCHG
};

void OpArg::WriteREX(XEmitter* emit, int opBits, int bits, int customOp) const
{
  if (customOp == -1)
    customOp = operandReg;
  u8 op = 0x40;
  // REX.W (whether operation is a 64-bit operation)
  if (opBits == 64)
    op |= 8;
  // REX.R (whether ModR/M reg field refers to R8-R15.
  if (customOp & 8)
    op |= 4;
  // REX.X (whether ModR/M SIB index field refers to R8-R15)
  if (indexReg & 8)
    op |= 2;
  // REX.B (whether ModR/M rm or SIB base or opcode reg field refers to R8-R15)
  if (offsetOrBaseReg & 8)
    op |= 1;
  // Write REX if wr have REX bits to write, or if the operation accesses
  // SIL, DIL, BPL, or SPL.
  if (op != 0x40 || (scale == SCALE_NONE && bits == 8 && (offsetOrBaseReg & 0x10c) == 4) ||
      (opBits == 8 && (customOp & 0x10c) == 4))
  {
    emit->Write8(op);
    // Check the operation doesn't access AH, BH, CH, or DH.
    DEBUG_ASSERT((offsetOrBaseReg & 0x100) == 0);
    DEBUG_ASSERT((customOp & 0x100) == 0);
  }
}

void OpArg::WriteVEX(XEmitter* emit, X64Reg regOp1, X64Reg regOp2, int L, int pp, int mmmmm,
                     int W) const
{
  int R = !(regOp1 & 8);
  int X = !(indexReg & 8);
  int B = !(offsetOrBaseReg & 8);

  u8 vvvv = (regOp2 == X64Reg::INVALID_REG) ? 0xf : (regOp2 ^ 0xf);

  // do we need any VEX fields that only appear in the three-byte form?
  if (X == 1 && B == 1 && W == 0 && mmmmm == 1)
  {
    u8 RvvvvLpp = (R << 7) | (vvvv << 3) | (L << 2) | pp;
    emit->Write8(0xC5);
    emit->Write8(RvvvvLpp);
  }
  else
  {
    u8 RXBmmmmm = (R << 7) | (X << 6) | (B << 5) | mmmmm;
    u8 WvvvvLpp = (W << 7) | (vvvv << 3) | (L << 2) | pp;
    emit->Write8(0xC4);
    emit->Write8(RXBmmmmm);
    emit->Write8(WvvvvLpp);
  }
}

void OpArg::WriteRest(XEmitter* emit, int extraBytes, X64Reg _operandReg,
                      bool warn_64bit_offset) const
{
  if (_operandReg == INVALID_REG)
    _operandReg = (X64Reg)this->operandReg;
  int mod = 0;
  int ireg = indexReg;
  bool SIB = false;
  int _offsetOrBaseReg = this->offsetOrBaseReg;

  if (scale == SCALE_RIP)  // Also, on 32-bit, just an immediate address
  {
    // Oh, RIP addressing.
    _offsetOrBaseReg = 5;
    emit->WriteModRM(0, _operandReg, _offsetOrBaseReg);
    // TODO : add some checks
    u64 ripAddr = (u64)emit->GetCodePtr() + 4 + extraBytes;
    s64 distance = (s64)offset - (s64)ripAddr;
    ASSERT_MSG(DYNA_REC,
               (distance < 0x80000000LL && distance >= -0x80000000LL) || !warn_64bit_offset,
               "WriteRest: op out of range ({:#x} uses {:#x})", ripAddr, offset);
    s32 offs = (s32)distance;
    emit->Write32((u32)offs);
    return;
  }

  if (scale == SCALE_NONE)
  {
    // Oh, no memory, Just a reg.
    mod = 3;  // 11
  }
  else if (scale >= SCALE_NOBASE_2 && scale <= SCALE_NOBASE_8)
  {
    SIB = true;
    mod = 0;
    _offsetOrBaseReg = 5;
    // Always has 32-bit displacement
  }
  else
  {
    if (scale != SCALE_ATREG)
    {
      SIB = true;
    }
    else if ((_offsetOrBaseReg & 7) == 4)
    {
      // Special case for which SCALE_ATREG needs SIB
      SIB = true;
      ireg = _offsetOrBaseReg;
    }

    // Okay, we're fine. Just disp encoding.
    // We need displacement. Which size?
    int ioff = (int)(s64)offset;
    if (ioff == 0 && (_offsetOrBaseReg & 7) != 5)
    {
      mod = 0;  // No displacement
    }
    else if (ioff >= -128 && ioff <= 127)
    {
      mod = 1;  // 8-bit displacement
    }
    else
    {
      mod = 2;  // 32-bit displacement
    }
  }

  // Okay. Time to do the actual writing
  // ModRM byte:
  int oreg = _offsetOrBaseReg;
  if (SIB)
    oreg = 4;

  emit->WriteModRM(mod, _operandReg, oreg);

  if (SIB)
  {
    // SIB byte
    int ss;
    switch (scale)
    {
    case SCALE_NONE:
      _offsetOrBaseReg = 4;
      ss = 0;
      break;  // RSP
    case SCALE_1:
      ss = 0;
      break;
    case SCALE_2:
      ss = 1;
      break;
    case SCALE_4:
      ss = 2;
      break;
    case SCALE_8:
      ss = 3;
      break;
    case SCALE_NOBASE_2:
      ss = 1;
      break;
    case SCALE_NOBASE_4:
      ss = 2;
      break;
    case SCALE_NOBASE_8:
      ss = 3;
      break;
    case SCALE_ATREG:
      ss = 0;
      break;
    default:
      ASSERT_MSG(DYNA_REC, 0, "Invalid scale for SIB byte");
      ss = 0;
      break;
    }
    emit->Write8((u8)((ss << 6) | ((ireg & 7) << 3) | (_offsetOrBaseReg & 7)));
  }

  if (mod == 1)  // 8-bit disp
  {
    emit->Write8((u8)(s8)(s32)offset);
  }
  else if (mod == 2 || (scale >= SCALE_NOBASE_2 && scale <= SCALE_NOBASE_8))  // 32-bit disp
  {
    emit->Write32((u32)offset);
  }
}

void OpArg::WriteSingleByteOp(XEmitter* emit, u8 op, X64Reg _operandReg, int bits)
{
  if (bits == 16)
    emit->Write8(0x66);

  this->operandReg = (u8)_operandReg;
  WriteREX(emit, bits, bits);
  emit->Write8(op);
  WriteRest(emit);
}

void OpArg::WriteNormalOp(XEmitter* emit, bool toRM, NormalOp op, const OpArg& operand,
                          int bits) const
{
  X64Reg _operandReg;
  if (IsImm())
  {
    ASSERT_MSG(DYNA_REC, 0, "WriteNormalOp - Imm argument, wrong order");
  }

  if (bits == 16)
    emit->Write8(0x66);

  int immToWrite = 0;
  const NormalOpDef& op_def = normalops[static_cast<int>(op)];

  if (operand.IsImm())
  {
    WriteREX(emit, bits, bits);

    if (!toRM)
    {
      ASSERT_MSG(DYNA_REC, 0, "WriteNormalOp - Writing to Imm (!toRM)");
    }

    if (operand.scale == SCALE_IMM8 && bits == 8)
    {
      // op al, imm8
      if (!scale && offsetOrBaseReg == AL && op_def.eaximm8 != 0xCC)
      {
        emit->Write8(op_def.eaximm8);
        emit->Write8((u8)operand.offset);
        return;
      }
      // mov reg, imm8
      if (!scale && op == NormalOp::MOV)
      {
        emit->Write8(0xB0 + (offsetOrBaseReg & 7));
        emit->Write8((u8)operand.offset);
        return;
      }
      // op r/m8, imm8
      emit->Write8(op_def.imm8);
      immToWrite = 8;
    }
    else if ((operand.scale == SCALE_IMM16 && bits == 16) ||
             (operand.scale == SCALE_IMM32 && bits == 32) ||
             (operand.scale == SCALE_IMM32 && bits == 64))
    {
      // Try to save immediate size if we can, but first check to see
      // if the instruction supports simm8.
      // op r/m, imm8
      if (op_def.simm8 != 0xCC &&
          ((operand.scale == SCALE_IMM16 && (s16)operand.offset == (s8)operand.offset) ||
           (operand.scale == SCALE_IMM32 && (s32)operand.offset == (s8)operand.offset)))
      {
        emit->Write8(op_def.simm8);
        immToWrite = 8;
      }
      else
      {
        // mov reg, imm
        if (!scale && op == NormalOp::MOV && bits != 64)
        {
          emit->Write8(0xB8 + (offsetOrBaseReg & 7));
          if (bits == 16)
            emit->Write16((u16)operand.offset);
          else
            emit->Write32((u32)operand.offset);
          return;
        }
        // op eax, imm
        if (!scale && offsetOrBaseReg == EAX && op_def.eaximm32 != 0xCC)
        {
          emit->Write8(op_def.eaximm32);
          if (bits == 16)
            emit->Write16((u16)operand.offset);
          else
            emit->Write32((u32)operand.offset);
          return;
        }
        // op r/m, imm
        emit->Write8(op_def.imm32);
        immToWrite = bits == 16 ? 16 : 32;
      }
    }
    else if ((operand.scale == SCALE_IMM8 && bits == 16) ||
             (operand.scale == SCALE_IMM8 && bits == 32) ||
             (operand.scale == SCALE_IMM8 && bits == 64))
    {
      // op r/m, imm8
      emit->Write8(op_def.simm8);
      immToWrite = 8;
    }
    else if (operand.scale == SCALE_IMM64 && bits == 64)
    {
      if (scale)
      {
        ASSERT_MSG(DYNA_REC, 0,
                   "WriteNormalOp - MOV with 64-bit imm requires register destination");
      }
      // mov reg64, imm64
      else if (op == NormalOp::MOV)
      {
        // movabs reg64, imm64 (10 bytes)
        if (static_cast<s64>(operand.offset) != static_cast<s32>(operand.offset))
        {
          emit->Write8(0xB8 + (offsetOrBaseReg & 7));
          emit->Write64(operand.offset);
          return;
        }
        // mov reg64, simm32 (7 bytes)
        emit->Write8(op_def.imm32);
        immToWrite = 32;
      }
      else
      {
        ASSERT_MSG(DYNA_REC, 0, "WriteNormalOp - Only MOV can take 64-bit imm");
      }
    }
    else
    {
      ASSERT_MSG(DYNA_REC, 0, "WriteNormalOp - Unhandled case {} {}", operand.scale, bits);
    }

    // pass extension in REG of ModRM
    _operandReg = static_cast<X64Reg>(op_def.ext);
  }
  else
  {
    _operandReg = (X64Reg)operand.offsetOrBaseReg;
    WriteREX(emit, bits, bits, _operandReg);
    // op r/m, reg
    if (toRM)
    {
      emit->Write8(bits == 8 ? op_def.toRm8 : op_def.toRm32);
    }
    // op reg, r/m
    else
    {
      emit->Write8(bits == 8 ? op_def.fromRm8 : op_def.fromRm32);
    }
  }
  WriteRest(emit, immToWrite >> 3, _operandReg);
  switch (immToWrite)
  {
  case 0:
    break;
  case 8:
    emit->Write8((u8)operand.offset);
    break;
  case 16:
    emit->Write16((u16)operand.offset);
    break;
  case 32:
    emit->Write32((u32)operand.offset);
    break;
  default:
    ASSERT_MSG(DYNA_REC, 0, "WriteNormalOp - Unhandled case");
  }
}

}  // namespace Gen
